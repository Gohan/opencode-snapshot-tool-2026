#include "ost/core/snapshot_scanner.h"

#include "ost/core/retention_policy.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimeZone>
#include <QThreadPool>
#include <QUuid>
#include <QtConcurrentMap>

#include <algorithm>
#include <functional>

namespace {
QString normalizedPath(const QString& path) {
  QString result = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
  result = result.toLower();
#endif
  return result;
}

bool within(const QString& path, const QString& parent) {
  const auto child = normalizedPath(path);
  auto root = normalizedPath(parent);
  if (child == root) return true;
  if (!root.endsWith(QDir::separator())) root += QDir::separator();
  return child.startsWith(root);
}

int parseCount(const QByteArray& output, const QByteArray& key) {
  for (const auto& line : output.split('\n')) {
    if (!line.startsWith(key + ':')) continue;
    return line.mid(key.size() + 1).trimmed().toInt();
  }
  return 0;
}
}  // namespace

namespace ost::core {
SnapshotScanner::SnapshotScanner(GitClient git) : git_(std::move(git)) {}

QStringList SnapshotScanner::discoverGitDirectories(const QString& snapshotRoot) {
  QStringList result;
  std::function<void(const QString&)> visit = [&](const QString& path) {
    const QDir directory(path);
    if (!directory.exists()) return;
    if (QFileInfo::exists(directory.filePath(QStringLiteral("HEAD"))) &&
        QFileInfo(directory.filePath(QStringLiteral("objects"))).isDir()) {
      result.push_back(QDir::cleanPath(directory.absolutePath()));
      return;
    }
    const auto children = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const auto& child : children) {
      if (child.fileName() == QStringLiteral("objects") || child.fileName() == QStringLiteral(".git")) continue;
      visit(child.absoluteFilePath());
    }
  };
  visit(snapshotRoot);
  std::sort(result.begin(), result.end());
  return result;
}

qint64 SnapshotScanner::directorySize(const QString& path) {
  qint64 total = 0;
  QDirIterator iterator(path, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    total += iterator.fileInfo().size();
  }
  return total;
}

QString SnapshotScanner::defaultDataDirectory() {
  const auto environment = QProcessEnvironment::systemEnvironment();
  if (environment.contains(QStringLiteral("OPENCODE_DATA_HOME")))
    return QDir::cleanPath(environment.value(QStringLiteral("OPENCODE_DATA_HOME")));
  if (environment.contains(QStringLiteral("XDG_DATA_HOME")))
    return QDir(environment.value(QStringLiteral("XDG_DATA_HOME"))).filePath(QStringLiteral("opencode"));
#ifdef Q_OS_MACOS
  return QDir(QDir::homePath()).filePath(QStringLiteral("Library/Application Support/opencode"));
#else
  return QDir(QDir::homePath()).filePath(QStringLiteral(".local/share/opencode"));
#endif
}

ScanResult SnapshotScanner::scan(const QString& snapshotRoot, const QString& databasePath,
                                 const RetentionSettings& settings,
                                 const std::function<void(const QString&)>& progress) const {
  ScanResult result;
  result.snapshotRoot = QDir::cleanPath(snapshotRoot);
  result.databasePath = QDir::cleanPath(databasePath);

  if (!QFileInfo(result.snapshotRoot).isDir()) {
    result.warnings.push_back(
        QStringLiteral("Snapshot directory does not exist: %1").arg(result.snapshotRoot));
    return result;
  }
  if (!git_.available())
    result.warnings.push_back(QStringLiteral("Git executable was not found; tree metadata is incomplete"));
  if (!QFileInfo(result.databasePath).isFile())
    result.warnings.push_back(
        QStringLiteral("OpenCode database does not exist: %1").arg(result.databasePath));

  const auto gitDirectories = discoverGitDirectories(result.snapshotRoot);
  if (progress) progress(QStringLiteral("Discovered %1 repositories").arg(gitDirectories.size()));
  QThreadPool scanPool;
  scanPool.setMaxThreadCount(std::clamp(static_cast<int>(gitDirectories.size()), 1, 4));
  const auto scannedRepositories = QtConcurrent::blockingMapped(
      &scanPool, gitDirectories, [this, &result, &progress](const QString& gitDir) {
        RepositoryInfo repository;
        repository.gitDir = gitDir;
        repository.relativePath = QDir(result.snapshotRoot).relativeFilePath(gitDir);
        repository.projectId = QDir::fromNativeSeparators(repository.relativePath).section('/', 0, 0);
        if (progress) progress(QStringLiteral("Scanning %1").arg(repository.relativePath));

        QSettings gitConfig(QDir(gitDir).filePath(QStringLiteral("config")), QSettings::IniFormat);
        repository.worktree = gitConfig.value(QStringLiteral("core/worktree")).toString();
        if (repository.worktree.isEmpty()) {
          const auto worktree = git_.run(gitDir, {QStringLiteral("config"), QStringLiteral("--get"),
                                                  QStringLiteral("core.worktree")}, {}, 5000);
          repository.worktree = QString::fromUtf8(worktree.output.trimmed());
        }

        const auto lfsPrefix = QDir::fromNativeSeparators(
            QDir(gitDir).filePath(QStringLiteral("lfs/objects/")));
        const auto objectsPrefix = QDir::fromNativeSeparators(
            QDir(gitDir).filePath(QStringLiteral("objects/")));
        QDirIterator files(gitDir, QDir::Files | QDir::Hidden | QDir::System,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
          files.next();
          const auto info = files.fileInfo();
          const auto filePath = QDir::fromNativeSeparators(info.absoluteFilePath());
          const bool temporaryPack = info.fileName().startsWith(QStringLiteral("tmp_pack_")) &&
                                     info.dir().dirName() == QStringLiteral("pack");
          repository.actualBytes += info.size();
          if (filePath.startsWith(lfsPrefix))
            repository.lfsBytes += info.size();
          else if (temporaryPack)
            repository.tempPackBytes += info.size();
          else if (filePath.startsWith(objectsPrefix))
            repository.gitObjectBytes += info.size();
          repository.largestFiles.push_back(
              {QDir(gitDir).relativeFilePath(info.absoluteFilePath()), info.size()});
          std::sort(repository.largestFiles.begin(), repository.largestFiles.end(),
                    [](const auto& left, const auto& right) { return left.bytes > right.bytes; });
          if (repository.largestFiles.size() > 5) repository.largestFiles.resize(5);
        }

        const auto counts = git_.run(gitDir, {QStringLiteral("count-objects"), QStringLiteral("-v")}, {}, 15000);
        repository.looseObjects = parseCount(counts.output, QByteArrayLiteral("count"));
        repository.packedObjects = parseCount(counts.output, QByteArrayLiteral("in-pack"));
        return repository;
      });

  QHash<QString, QVector<int>> repositoriesByProject;
  for (const auto& repository : scannedRepositories) {
    result.totalBytes += repository.actualBytes;
    const int index = result.repositories.size();
    result.repositories.push_back(repository);
    repositoriesByProject[repository.projectId].push_back(index);
  }

  QVector<SnapshotInfo> databaseSnapshots;
  if (progress) progress(QStringLiteral("Reading OpenCode session metadata"));
  const auto connectionName = QStringLiteral("ost-scan-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  {
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(result.databasePath);
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (database.open()) {
      QSqlQuery query(database);
      query.exec(QStringLiteral(
          "SELECT p.time_created,p.data,p.session_id,s.project_id,s.directory,s.title "
          "FROM part p JOIN session s ON s.id=p.session_id "
          "WHERE p.data LIKE '%\"snapshot\"%' ORDER BY p.time_created"));
      QHash<QString, int> grouped;
      QHash<QString, QSet<QString>> sessionsByGroup;
      while (query.next()) {
        const auto json = QJsonDocument::fromJson(query.value(1).toByteArray());
        const auto hash = json.object().value(QStringLiteral("snapshot")).toString();
        if (hash.size() != 40) continue;
        const auto project = query.value(3).toString();
        const auto directory = query.value(4).toString();
        const auto key = project + QChar(0x1f) + normalizedPath(directory) + QChar(0x1f) + hash;
        const auto timestamp = QDateTime::fromMSecsSinceEpoch(query.value(0).toLongLong(), QTimeZone::UTC);
        int itemIndex = grouped.value(key, -1);
        if (itemIndex < 0) {
          SnapshotInfo item;
          item.hash = hash;
          item.projectId = project;
          item.directory = directory;
          item.firstSeen = timestamp;
          item.lastSeen = timestamp;
          databaseSnapshots.push_back(item);
          itemIndex = databaseSnapshots.size() - 1;
          grouped.insert(key, itemIndex);
        }
        auto& item = databaseSnapshots[itemIndex];
        item.firstSeen = std::min(item.firstSeen, timestamp);
        item.lastSeen = std::max(item.lastSeen, timestamp);
        ++item.references;
        sessionsByGroup[key].insert(query.value(2).toString());
        item.sessions = sessionsByGroup[key].size();
        const auto title = query.value(5).toString();
        if (!title.isEmpty() && !item.titles.contains(title)) item.titles.push_back(title);
      }
      database.close();
    } else if (QFileInfo(result.databasePath).isFile()) {
      result.warnings.push_back(
          QStringLiteral("Could not open OpenCode database read-only: %1")
              .arg(database.lastError().text()));
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  if (progress) progress(QStringLiteral("Loaded %1 distinct database snapshots").arg(databaseSnapshots.size()));

  QVector<QVector<int>> candidatesBySnapshot(databaseSnapshots.size());
  QHash<int, QStringList> hashesByRepository;
  for (int itemIndex = 0; itemIndex < databaseSnapshots.size(); ++itemIndex) {
    const auto& item = databaseSnapshots[itemIndex];
    QVector<int> candidates;
    const auto appendCandidate = [&](int index) {
      if (!candidates.contains(index)) candidates.push_back(index);
    };

    // Prefer the repository encoded by the current project id, but do not trust
    // identity alone: OpenCode can migrate a worktree to a new project id while
    // older tree objects remain in the legacy snapshot store.
    for (const int index : repositoriesByProject.value(item.projectId)) {
      const auto& repository = result.repositories[index];
      if (repository.worktree.isEmpty()) continue;
      if (normalizedPath(repository.worktree) == normalizedPath(item.directory) ||
          within(item.directory, repository.worktree))
        appendCandidate(index);
    }
    for (int index = 0; index < result.repositories.size(); ++index) {
      const auto& repository = result.repositories[index];
      if (repository.worktree.isEmpty()) continue;
      if (normalizedPath(repository.worktree) == normalizedPath(item.directory) ||
          within(item.directory, repository.worktree))
        appendCandidate(index);
    }

    candidatesBySnapshot[itemIndex] = candidates;
    for (const int index : candidates) hashesByRepository[index].push_back(item.hash);
  }

  QHash<int, QSet<QString>> available;
  int checkedRepository = 0;
  const int repositoriesToCheck = hashesByRepository.size();
  for (auto it = hashesByRepository.cbegin(); it != hashesByRepository.cend(); ++it) {
    QStringList hashes = it.value();
    if (hashes.isEmpty()) continue;
    hashes.removeDuplicates();
    const QByteArray input = hashes.join(QChar('\n')).toLatin1() + '\n';
    const int repoIndex = it.key();
    if (progress) progress(QStringLiteral("Verifying snapshot repository %1 of %2")
                             .arg(++checkedRepository).arg(repositoriesToCheck));
    const auto checked = git_.run(result.repositories[repoIndex].gitDir,
                                  {QStringLiteral("cat-file"),
                                   QStringLiteral("--batch-check=%(objectname) %(objecttype)")}, input, 30000);
    for (const auto& row : checked.output.split('\n')) {
      const auto fields = row.trimmed().split(' ');
      if (fields.size() == 2 && fields[1] == "tree")
        available[repoIndex].insert(QString::fromLatin1(fields[0]));
    }
  }

  for (int itemIndex = 0; itemIndex < databaseSnapshots.size(); ++itemIndex) {
    auto item = databaseSnapshots[itemIndex];
    int chosen = -1;
    for (const int index : candidatesBySnapshot[itemIndex]) {
      if (available[index].contains(item.hash)) {
        chosen = index;
        break;
      }
    }
    if (chosen < 0) {
      ++result.unmappedDatabaseRecords;
      continue;
    }
    item.exists = true;
    result.repositories[chosen].snapshots.push_back(item);
  }

  QVector<int> repositoryIndices;
  repositoryIndices.reserve(result.repositories.size());
  for (int index = 0; index < result.repositories.size(); ++index)
    repositoryIndices.push_back(index);
  QThreadPool indexPool;
  indexPool.setMaxThreadCount(std::clamp(static_cast<int>(repositoryIndices.size()), 1, 4));
  const auto currentTrees = QtConcurrent::blockingMapped(
      &indexPool, repositoryIndices, [this, &result](int index) {
        const auto& repository = result.repositories[index];
        const auto tree = git_.run(repository.gitDir, {QStringLiteral("write-tree")}, {}, 5000);
        return tree.ok() ? QString::fromLatin1(tree.output.trimmed()) : QString{};
      });

  for (int index = 0; index < result.repositories.size(); ++index) {
    auto& repository = result.repositories[index];
    const auto& hash = currentTrees[index];
    const bool present = std::any_of(repository.snapshots.cbegin(), repository.snapshots.cend(),
                                     [&](const auto& item) { return item.hash == hash; });
    if (hash.size() == 40 && !present) {
      SnapshotInfo item;
      item.hash = hash;
      item.projectId = repository.projectId;
      item.directory = repository.worktree;
      item.source = SnapshotSource::CurrentIndex;
      item.exists = true;
      item.lastSeen = QFileInfo(QDir(repository.gitDir).filePath(QStringLiteral("index"))).lastModified().toUTC();
      item.firstSeen = item.lastSeen;
      item.titles = {QStringLiteral("Current Git index tree")};
      repository.snapshots.push_back(item);
    }
    std::sort(repository.snapshots.begin(), repository.snapshots.end(),
              [](const auto& left, const auto& right) { return left.lastSeen > right.lastSeen; });
  }

  RetentionPolicy::apply(result.repositories, settings);
  if (result.unmappedDatabaseRecords > 0)
    result.warnings.push_back(QStringLiteral("%1 database snapshot records could not be mapped to a Git repository")
                                  .arg(result.unmappedDatabaseRecords));
  if (progress) progress(QStringLiteral("Finalizing retention policy"));
  return result;
}
}  // namespace ost::core
