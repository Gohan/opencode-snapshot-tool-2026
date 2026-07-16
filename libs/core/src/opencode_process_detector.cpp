#include "ost/core/opencode_process_detector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#elif defined(Q_OS_LINUX)
#include <dirent.h>
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/sysctl.h>
#endif

namespace {
using namespace ost::core;

QString normalizedPath(const QString& path) {
  if (path.isEmpty()) return {};
  QString result = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
  result = result.toLower();
#endif
  return QDir::fromNativeSeparators(result);
}

bool isOpenCodeExecutable(const QString& executable) {
  const auto name = QFileInfo(executable).fileName().toLower();
  return name == QStringLiteral("opencode") || name == QStringLiteral("opencode.exe") ||
         name == QStringLiteral("opencode-cli") || name == QStringLiteral("opencode-cli.exe");
}

bool isMultiProjectProcess(const OpenCodeProcessInfo& process) {
  const auto command = process.commandLine.toLower();
  if (command.contains(QRegularExpression(QStringLiteral("(?:^|\\s)(serve|web)(?:\\s|$)"))))
    return true;
#ifdef Q_OS_WIN
  const auto executable = QDir::fromNativeSeparators(process.executable).toLower();
  if (executable.contains(QStringLiteral("/appdata/local/opencode/"))) return true;
#endif
  return QFileInfo(process.executable).fileName().startsWith(QStringLiteral("opencode-cli"),
                                                              Qt::CaseInsensitive);
}

QString resolveWorktree(const QString& directory) {
  if (directory.isEmpty()) return {};
  QProcess git;
  git.start(QStringLiteral("git"), {QStringLiteral("-C"), directory,
                                     QStringLiteral("rev-parse"),
                                     QStringLiteral("--show-toplevel")});
  if (!git.waitForStarted(1000) || !git.waitForFinished(3000) || git.exitCode() != 0)
    return normalizedPath(directory);
  const auto output = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
  return output.isEmpty() ? normalizedPath(directory) : normalizedPath(output);
}

void addPid(RepositoryActivity& activity, qint64 pid, RepositoryActivityState state) {
  if (state == RepositoryActivityState::Active ||
      (state == RepositoryActivityState::PossiblyActive &&
       activity.state == RepositoryActivityState::Inactive))
    activity.state = state;
  if (!activity.processIds.contains(pid)) activity.processIds.push_back(pid);
}

#ifdef Q_OS_WIN
struct RemoteCurrentDirectory {
  UNICODE_STRING path;
  HANDLE handle;
};

struct RemoteProcessParametersPrefix {
  ULONG maximumLength;
  ULONG length;
  ULONG flags;
  ULONG debugFlags;
  HANDLE consoleHandle;
  ULONG consoleFlags;
  HANDLE standardInput;
  HANDLE standardOutput;
  HANDLE standardError;
  RemoteCurrentDirectory currentDirectory;
  UNICODE_STRING dllPath;
  UNICODE_STRING imagePath;
  UNICODE_STRING commandLine;
};

QString readRemoteString(HANDLE process, const UNICODE_STRING& value) {
  if (!value.Buffer || value.Length == 0) return {};
  std::wstring buffer(static_cast<size_t>(value.Length / sizeof(wchar_t)), L'\0');
  SIZE_T bytesRead = 0;
  if (!ReadProcessMemory(process, value.Buffer, buffer.data(), value.Length, &bytesRead) ||
      bytesRead != value.Length)
    return {};
  return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(buffer.size()));
}

bool readWindowsProcess(DWORD pid, OpenCodeProcessInfo& result) {
  const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                     FALSE, pid);
  if (!process) return false;
  wchar_t executable[32768]{};
  DWORD executableLength = static_cast<DWORD>(std::size(executable));
  if (QueryFullProcessImageNameW(process, 0, executable, &executableLength))
    result.executable = QString::fromWCharArray(executable, executableLength);

  using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID,
                                                        ULONG, PULONG);
  const auto query = reinterpret_cast<NtQueryInformationProcessFn>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
  PROCESS_BASIC_INFORMATION basic{};
  bool success = false;
  if (query && query(process, ProcessBasicInformation, &basic, sizeof(basic), nullptr) >= 0) {
    PEB peb{};
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(process, basic.PebBaseAddress, &peb, sizeof(peb), &bytesRead) &&
        peb.ProcessParameters) {
      RemoteProcessParametersPrefix parameters{};
      if (ReadProcessMemory(process, peb.ProcessParameters, &parameters, sizeof(parameters),
                            &bytesRead)) {
        result.workingDirectory = readRemoteString(process, parameters.currentDirectory.path);
        result.commandLine = readRemoteString(process, parameters.commandLine);
        success = !result.workingDirectory.isEmpty();
      }
    }
  }
  CloseHandle(process);
  return success;
}
#endif

}  // namespace

namespace ost::core {

QVector<OpenCodeProcessInfo> OpenCodeProcessDetector::processes() const {
  QVector<OpenCodeProcessInfo> result;
#ifdef Q_OS_WIN
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return result;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      const auto name = QString::fromWCharArray(entry.szExeFile);
      if (!isOpenCodeExecutable(name)) continue;
      OpenCodeProcessInfo process;
      process.pid = entry.th32ProcessID;
      process.executable = name;
      process.accessible = readWindowsProcess(entry.th32ProcessID, process);
      process.multiProject = isMultiProjectProcess(process);
      if (process.accessible) process.worktree = resolveWorktree(process.workingDirectory);
      result.push_back(std::move(process));
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
#elif defined(Q_OS_LINUX)
  const QDir proc(QStringLiteral("/proc"));
  for (const auto& name : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool numeric = false;
    const auto pid = name.toLongLong(&numeric);
    if (!numeric) continue;
    const auto root = proc.filePath(name);
    const auto executable = QFileInfo(root + QStringLiteral("/exe")).symLinkTarget();
    if (!isOpenCodeExecutable(executable)) continue;
    OpenCodeProcessInfo process;
    process.pid = pid;
    process.executable = executable;
    process.workingDirectory = QFileInfo(root + QStringLiteral("/cwd")).symLinkTarget();
    QFile commandFile(root + QStringLiteral("/cmdline"));
    if (commandFile.open(QIODevice::ReadOnly)) {
      auto command = commandFile.readAll();
      command.replace('\0', ' ');
      process.commandLine = QString::fromLocal8Bit(command).trimmed();
    }
    process.accessible = !process.workingDirectory.isEmpty();
    process.multiProject = isMultiProjectProcess(process);
    if (process.accessible) process.worktree = resolveWorktree(process.workingDirectory);
    result.push_back(std::move(process));
  }
#elif defined(Q_OS_MACOS)
  QVector<pid_t> pids(PROC_ALL_PIDS * 4);
  const int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                                  static_cast<int>(pids.size() * sizeof(pid_t)));
  pids.resize(std::max(0, bytes / static_cast<int>(sizeof(pid_t))));
  for (const auto pid : pids) {
    char executable[PROC_PIDPATHINFO_MAXSIZE]{};
    if (proc_pidpath(pid, executable, sizeof(executable)) <= 0 ||
        !isOpenCodeExecutable(QString::fromLocal8Bit(executable)))
      continue;
    proc_vnodepathinfo paths{};
    OpenCodeProcessInfo process;
    process.pid = pid;
    process.executable = QString::fromLocal8Bit(executable);
    if (proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &paths, sizeof(paths)) == sizeof(paths))
      process.workingDirectory = QString::fromLocal8Bit(paths.pvi_cdir.vip_path);
    process.accessible = !process.workingDirectory.isEmpty();
    process.multiProject = !process.accessible;
    if (process.accessible) process.worktree = resolveWorktree(process.workingDirectory);
    result.push_back(std::move(process));
  }
#endif
  return result;
}

QVector<ProjectBinding> OpenCodeProcessDetector::projectBindings(
    const QString& databasePath) const {
  QVector<ProjectBinding> result;
  if (!QFileInfo(databasePath).isFile()) return result;
  const auto connectionName =
      QStringLiteral("ost-processes-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  {
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(databasePath);
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (database.open()) {
      QSqlQuery query(database);
      if (query.exec(QStringLiteral("SELECT id,worktree,sandboxes FROM project"))) {
        while (query.next()) {
          ProjectBinding binding;
          binding.projectId = query.value(0).toString();
          const auto worktree = query.value(1).toString();
          if (!worktree.isEmpty()) binding.worktrees.push_back(normalizedPath(worktree));
          const auto sandboxes = QJsonDocument::fromJson(query.value(2).toByteArray()).array();
          for (const auto& sandbox : sandboxes) {
            const auto path = normalizedPath(sandbox.toString());
            if (!path.isEmpty() && !binding.worktrees.contains(path)) binding.worktrees.push_back(path);
          }
          if (!binding.projectId.isEmpty()) result.push_back(std::move(binding));
        }
      }
      database.close();
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  return result;
}

void OpenCodeProcessDetector::detect(QVector<RepositoryInfo>& repositories,
                                     const QString& databasePath) const {
  classify(repositories, processes(), projectBindings(databasePath));
}

void OpenCodeProcessDetector::classify(QVector<RepositoryInfo>& repositories,
                                       const QVector<OpenCodeProcessInfo>& processes,
                                       const QVector<ProjectBinding>& projects) {
  for (auto& repository : repositories) repository.activity = {};

  for (const auto& process : processes) {
    if (!process.accessible || process.multiProject || process.worktree.isEmpty()) {
      for (auto& repository : repositories)
        addPid(repository.activity, process.pid, RepositoryActivityState::PossiblyActive);
      continue;
    }

    const auto worktree = normalizedPath(process.worktree);
    QVector<int> matches;
    for (int index = 0; index < repositories.size(); ++index)
      if (normalizedPath(repositories[index].worktree) == worktree) matches.push_back(index);
    if (matches.isEmpty()) continue;

    QString currentProject;
    for (const auto& project : projects) {
      const auto found = std::any_of(project.worktrees.cbegin(), project.worktrees.cend(),
                                     [&worktree](const QString& path) {
                                       return normalizedPath(path) == worktree;
                                     });
      if (found) {
        currentProject = project.projectId;
        break;
      }
    }

    if (!currentProject.isEmpty()) {
      QVector<int> currentMatches;
      for (const int index : matches)
        if (repositories[index].projectId == currentProject) currentMatches.push_back(index);
      if (currentMatches.size() == 1) {
        addPid(repositories[currentMatches.front()].activity, process.pid,
               RepositoryActivityState::Active);
        continue;
      }
      if (currentMatches.isEmpty()) {
        // The database identifies another project id as the current owner of
        // this worktree. A legacy store with the old id is not used by this
        // process, even when it is inspected without the current store beside it.
        continue;
      }
      for (const int index : currentMatches)
        addPid(repositories[index].activity, process.pid,
               RepositoryActivityState::PossiblyActive);
      continue;
    } else if (matches.size() == 1) {
      addPid(repositories[matches.front()].activity, process.pid,
             RepositoryActivityState::Active);
      continue;
    }

    for (const int index : matches)
      addPid(repositories[index].activity, process.pid,
             RepositoryActivityState::PossiblyActive);
  }

  for (auto& repository : repositories)
    std::sort(repository.activity.processIds.begin(), repository.activity.processIds.end());
}

}  // namespace ost::core
