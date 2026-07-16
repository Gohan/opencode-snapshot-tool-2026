#include "ost/core/snapshot_cleaner.h"

#include "ost/core/opencode_process_detector.h"
#include "ost/core/snapshot_scanner.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QThread>

#include <algorithm>

namespace {
constexpr auto kRefPrefix = "refs/opencode-snapshot-tool/keep/";

QVector<QFileInfo> lfsObjects(const QString& gitDir) {
  QVector<QFileInfo> result;
  const auto root = QDir(gitDir).filePath(QStringLiteral("lfs/objects"));
  QDirIterator iterator(root, QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    if (iterator.fileName().size() == 64) result.push_back(iterator.fileInfo());
  }
  return result;
}

bool lfsReferenced(const ost::core::GitClient& git, const QString& gitDir,
                   const QString& oid, const QVector<QString>& keepHashes) {
  const auto needle = QStringLiteral("oid sha256:%1").arg(oid);
  for (const auto& tree : keepHashes) {
    const auto result = git.run(gitDir, {QStringLiteral("grep"), QStringLiteral("-q"),
                                         QStringLiteral("-F"), QStringLiteral("-e"), needle, tree});
    if (result.exitCode == 0) return true;
    if (result.exitCode != 1) return true;  // fail closed: preserve unknown objects
  }
  return false;
}

qint64 staleBytes(const QString& gitDir, int staleHours) {
  const auto cutoff = QDateTime::currentDateTimeUtc().addSecs(-std::max(1, staleHours) * 3600);
  qint64 total = 0;
  QDirIterator iterator(gitDir, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto info = iterator.fileInfo();
    if (info.fileName().startsWith(QStringLiteral("tmp_pack_")) && info.lastModified().toUTC() < cutoff)
      total += info.size();
  }
  return total;
}

QStringList activeGitLocks(const QString& gitDir) {
  QStringList result;
  QDirIterator iterator(gitDir, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto name = iterator.fileName();
    if (name.endsWith(QStringLiteral(".lock")) || name == QStringLiteral("gc.pid"))
      result.push_back(QDir(gitDir).relativeFilePath(iterator.filePath()));
  }
  return result;
}

QString purgePathError(const QString& snapshotRoot, const QString& gitDir) {
  const auto root = QDir::fromNativeSeparators(QFileInfo(snapshotRoot).canonicalFilePath());
  const auto target = QDir::fromNativeSeparators(QFileInfo(gitDir).canonicalFilePath());
  if (root.isEmpty() || target.isEmpty())
    return QStringLiteral("Snapshot root or repository path does not exist");
  const auto rootPrefix = root.endsWith(QChar('/')) ? root : root + QChar('/');
#ifdef Q_OS_WIN
  const auto sensitivity = Qt::CaseInsensitive;
#else
  const auto sensitivity = Qt::CaseSensitive;
#endif
  if (target.compare(root, sensitivity) == 0 || !target.startsWith(rootPrefix, sensitivity))
    return QStringLiteral("Repository is not a child of the configured snapshot root");
  return {};
}

QString processList(const QVector<qint64>& pids) {
  QStringList values;
  for (const auto pid : pids) values.push_back(QString::number(pid));
  return values.join(QStringLiteral(", "));
}

QString activityError(const ost::core::RepositoryCleanupPlan& repository,
                      const ost::core::RepositoryActivity& activity) {
  if (activity.state == ost::core::RepositoryActivityState::Inactive) return {};
  const auto pids = processList(activity.processIds);
  if (activity.state == ost::core::RepositoryActivityState::Active)
    return QStringLiteral("OpenCode PID(s) %1 currently use %2. Close only those matching instances and scan again")
        .arg(pids.isEmpty() ? QStringLiteral("unknown") : pids, repository.relativePath);
  return QStringLiteral("OpenCode PID(s) %1 may use %2, but their exact worktree could not be proven. Close or identify only those processes and scan again")
      .arg(pids.isEmpty() ? QStringLiteral("unknown") : pids, repository.relativePath);
}

QByteArray activityFingerprint(const QString& gitDir) {
  QVector<QByteArray> rows;
  QDirIterator iterator(gitDir, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto info = iterator.fileInfo();
    rows.push_back(QDir(gitDir).relativeFilePath(info.absoluteFilePath()).toUtf8() + '\0' +
                   QByteArray::number(info.size()) + '\0' +
                   QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
  }
  std::sort(rows.begin(), rows.end());
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (const auto& row : rows) hash.addData(row);
  return hash.result();
}

ost::core::RepositoryCleanupPlan cleanupItem(const ost::core::RepositoryInfo& repository) {
  ost::core::RepositoryCleanupPlan item;
  item.gitDir = repository.gitDir;
  item.relativePath = repository.relativePath;
  item.projectId = repository.projectId;
  item.worktree = repository.worktree;
  item.activity = repository.activity;
  item.currentBytes = repository.actualBytes;
  return item;
}
}  // namespace

namespace ost::core {
SnapshotCleaner::SnapshotCleaner(GitClient git, ActivityProbe activityProbe)
    : git_(std::move(git)), activityProbe_(std::move(activityProbe)) {}

CleanupPlan SnapshotCleaner::preview(const ScanResult& scan, const CleanupSettings& settings) const {
  CleanupPlan plan;
  plan.currentBytes = scan.totalBytes;
  plan.databasePath = scan.databasePath;
  for (const auto& repository : scan.repositories) {
    if (repository.snapshots.isEmpty()) continue;
    auto item = cleanupItem(repository);
    if (repository.activity.state != RepositoryActivityState::Inactive) {
      plan.blockedRepositories.push_back(std::move(item));
      continue;
    }
    QSet<QString> keep;
    QSet<QString> remove;
    for (const auto& snapshot : repository.snapshots) {
      (snapshot.keep ? keep : remove).insert(snapshot.hash);
    }
    remove.subtract(keep);
    item.keepHashes = keep.values();
    item.removeHashes = remove.values();
    std::sort(item.keepHashes.begin(), item.keepHashes.end());
    std::sort(item.removeHashes.begin(), item.removeHashes.end());
    if (settings.pruneLfs) {
      for (const auto& object : lfsObjects(repository.gitDir)) {
        if (!lfsReferenced(git_, repository.gitDir, object.fileName(), item.keepHashes))
          item.removableLfsBytes += object.size();
      }
    }
    item.staleTempBytes = staleBytes(repository.gitDir, settings.staleFileHours);
    plan.estimatedReclaimableBytes += item.removableLfsBytes + item.staleTempBytes;
    plan.keepTrees += item.keepHashes.size();
    plan.removeTrees += item.removeHashes.size();
    plan.repositories.push_back(item);
  }
  return plan;
}

CleanupPlan SnapshotCleaner::previewReset(const RepositoryInfo& repository,
                                          const CleanupSettings& settings,
                                          const QString& databasePath) const {
  CleanupPlan failed;
  failed.resetHistory = true;
  failed.databasePath = databasePath;
  if (repository.activity.state != RepositoryActivityState::Inactive) {
    const auto item = cleanupItem(repository);
    failed.blockedRepositories = {item};
    failed.error = activityError(item, repository.activity);
    return failed;
  }
  const auto current = git_.run(repository.gitDir, {QStringLiteral("write-tree")});
  const auto currentHash = QString::fromLatin1(current.output.trimmed());
  if (!current.ok() || currentHash.size() != 40) {
    failed.error = QStringLiteral("Could not protect the current index tree in %1: %2")
                       .arg(repository.relativePath, QString::fromUtf8(current.error).trimmed());
    return failed;
  }

  auto resetRepository = repository;
  bool currentPresent = false;
  for (auto& snapshot : resetRepository.snapshots) {
    snapshot.keep = snapshot.hash == currentHash;
    currentPresent = currentPresent || snapshot.hash == currentHash;
  }
  if (!currentPresent) {
    SnapshotInfo currentSnapshot;
    currentSnapshot.hash = currentHash;
    currentSnapshot.exists = true;
    currentSnapshot.keep = true;
    currentSnapshot.source = SnapshotSource::CurrentIndex;
    resetRepository.snapshots.push_back(currentSnapshot);
  }
  ScanResult scan;
  scan.databasePath = databasePath;
  scan.totalBytes = repository.actualBytes;
  scan.repositories = {resetRepository};
  auto plan = preview(scan, settings);
  plan.resetHistory = true;
  return plan;
}

CleanupPlan SnapshotCleaner::previewPurge(const RepositoryInfo& repository,
                                          const QString& snapshotRoot,
                                          const QString& databasePath) const {
  CleanupPlan plan;
  plan.purgeStore = true;
  plan.databasePath = databasePath;
  if (repository.activity.state != RepositoryActivityState::Inactive) {
    const auto item = cleanupItem(repository);
    plan.blockedRepositories = {item};
    plan.error = activityError(item, repository.activity);
    return plan;
  }
  if (const auto pathError = purgePathError(snapshotRoot, repository.gitDir);
      !pathError.isEmpty()) {
    plan.error = QStringLiteral("Refusing full-store purge for %1: %2")
                     .arg(repository.relativePath, pathError);
    return plan;
  }
  const auto current = git_.run(repository.gitDir, {QStringLiteral("write-tree")});
  if (!current.ok() || current.output.trimmed().size() != 40) {
    plan.error = QStringLiteral("Could not validate the live snapshot store in %1: %2")
                     .arg(repository.relativePath, QString::fromUtf8(current.error).trimmed());
    return plan;
  }
  auto item = cleanupItem(repository);
  item.allowedRoot = snapshotRoot;
  item.currentBytes = SnapshotScanner::directorySize(repository.gitDir);
  for (const auto& snapshot : repository.snapshots)
    if (!item.removeHashes.contains(snapshot.hash)) item.removeHashes.push_back(snapshot.hash);
  plan.currentBytes = item.currentBytes;
  plan.estimatedReclaimableBytes = item.currentBytes;
  plan.removeTrees = item.removeHashes.size();
  plan.repositories = {item};
  return plan;
}

CleanupResult SnapshotCleaner::execute(const CleanupPlan& plan, const CleanupSettings& settings) const {
  CleanupResult result;
  if (!plan.error.isEmpty()) {
    result.error = plan.error;
    return result;
  }
  if (plan.repositories.isEmpty()) {
    result.error = QStringLiteral("The cleanup plan contains no inactive snapshot stores");
    return result;
  }

  // Complete all activity/lock/write-stability checks before mutating the first
  // repository, so a batch never becomes partially cleaned by a late guard.
  QVector<RepositoryActivity> activities;
  activities.reserve(plan.repositories.size());
  if (activityProbe_) {
    for (const auto& repository : plan.repositories)
      activities.push_back(activityProbe_(repository, plan.databasePath));
  } else {
    QVector<RepositoryInfo> repositories;
    repositories.reserve(plan.repositories.size());
    for (const auto& item : plan.repositories) {
      RepositoryInfo repository;
      repository.gitDir = item.gitDir;
      repository.relativePath = item.relativePath;
      repository.projectId = item.projectId;
      repository.worktree = item.worktree;
      repositories.push_back(std::move(repository));
    }
    OpenCodeProcessDetector().detect(repositories, plan.databasePath);
    for (const auto& repository : repositories) activities.push_back(repository.activity);
  }

  for (int index = 0; index < plan.repositories.size(); ++index) {
    const auto& repository = plan.repositories[index];
    const auto& activity = activities[index];
    if (const auto error = activityError(repository, activity); !error.isEmpty()) {
      result.error = QStringLiteral("Refusing cleanup: %1").arg(error);
      return result;
    }
    const auto locks = activeGitLocks(repository.gitDir);
    if (!locks.isEmpty()) {
      result.error = QStringLiteral("Refusing cleanup because Git lock files exist in %1: %2")
                         .arg(repository.relativePath, locks.join(QStringLiteral(", ")));
      return result;
    }
    const auto before = activityFingerprint(repository.gitDir);
    QThread::msleep(150);
    if (before != activityFingerprint(repository.gitDir)) {
      result.error = QStringLiteral("Refusing cleanup because %1 changed during the activity check")
                         .arg(repository.relativePath);
      return result;
    }
  }
  qint64 plannedBytesAtPreview = 0;
  for (const auto& repository : plan.repositories) plannedBytesAtPreview += repository.currentBytes;
  const auto untouchedBytes = std::max<qint64>(0, plan.currentBytes - plannedBytesAtPreview);
  result.bytesBefore = untouchedBytes;
  for (const auto& repository : plan.repositories)
    result.bytesBefore += SnapshotScanner::directorySize(repository.gitDir);
  for (const auto& repository : plan.repositories) {
    if (plan.purgeStore) {
      if (const auto pathError = purgePathError(repository.allowedRoot, repository.gitDir);
          !pathError.isEmpty()) {
        result.error = QStringLiteral("Refusing full-store purge for %1: %2")
                           .arg(repository.relativePath, pathError);
        return result;
      }
      const auto currentTree = git_.run(repository.gitDir, {QStringLiteral("write-tree")});
      if (!currentTree.ok() || currentTree.output.trimmed().size() != 40) {
        result.error = QStringLiteral("Refusing full-store purge because the live index cannot be validated in %1: %2")
                           .arg(repository.relativePath,
                                QString::fromUtf8(currentTree.error).trimmed());
        return result;
      }
      if (!QDir(repository.gitDir).removeRecursively()) {
        result.error = QStringLiteral("Failed to remove the snapshot store for %1")
                           .arg(repository.relativePath);
        return result;
      }
      result.messages.push_back(QStringLiteral("Removed snapshot store for %1; OpenCode will recreate it on the next snapshot")
                                    .arg(repository.relativePath));
      continue;
    }
    auto keepHashes = repository.keepHashes;
    const auto currentTree = git_.run(repository.gitDir, {QStringLiteral("write-tree")});
    const auto currentHash = QString::fromLatin1(currentTree.output.trimmed());
    if (!currentTree.ok() || currentHash.size() != 40) {
      result.error = QStringLiteral("Refusing cleanup because the current index tree could not be protected in %1: %2")
                         .arg(repository.relativePath, QString::fromUtf8(currentTree.error));
      return result;
    }
    if (!keepHashes.contains(currentHash)) keepHashes.push_back(currentHash);
    std::sort(keepHashes.begin(), keepHashes.end());

    const auto existing = git_.run(repository.gitDir,
                                   {QStringLiteral("for-each-ref"),
                                    QStringLiteral("--format=%(refname) %(objectname)"),
                                    QString::fromLatin1(kRefPrefix)});
    QHash<QString, QString> old;
    for (const auto& row : existing.output.split('\n')) {
      const auto fields = row.trimmed().split(' ');
      if (fields.size() == 2) old.insert(QString::fromLatin1(fields[0]), QString::fromLatin1(fields[1]));
    }
    QHash<QString, QString> desired;
    for (const auto& hash : keepHashes) desired.insert(QString::fromLatin1(kRefPrefix) + hash, hash);
    QByteArray commands;
    for (auto it = old.cbegin(); it != old.cend(); ++it)
      if (!desired.contains(it.key())) commands += "delete " + it.key().toLatin1() + '\n';
    for (auto it = desired.cbegin(); it != desired.cend(); ++it)
      if (old.value(it.key()) != it.value())
        commands += "update " + it.key().toLatin1() + ' ' + it.value().toLatin1() + '\n';
    if (!commands.isEmpty()) {
      const auto updated = git_.run(repository.gitDir,
                                    {QStringLiteral("update-ref"), QStringLiteral("--stdin")}, commands);
      if (!updated.ok()) {
        result.error = QStringLiteral("Failed to protect retained trees in %1: %2")
                           .arg(repository.relativePath, QString::fromUtf8(updated.error));
        return result;
      }
    }

    git_.run(repository.gitDir, {QStringLiteral("config"), QStringLiteral("core.commitGraph"),
                                 QStringLiteral("false")});
    if (settings.pruneLfs) {
      for (const auto& object : lfsObjects(repository.gitDir)) {
        if (lfsReferenced(git_, repository.gitDir, object.fileName(), keepHashes)) continue;
        QFile::setPermissions(object.absoluteFilePath(), object.permissions() | QFileDevice::WriteOwner);
        QFile::remove(object.absoluteFilePath());
      }
    }

    const auto cutoff = QDateTime::currentDateTimeUtc().addSecs(-std::max(1, settings.staleFileHours) * 3600);
    QDirIterator files(repository.gitDir, QDir::Files | QDir::Hidden | QDir::System,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
      files.next();
      const auto info = files.fileInfo();
      const bool temporaryPack = info.fileName().startsWith(QStringLiteral("tmp_pack_"));
      const bool staleIndexLock = info.fileName() == QStringLiteral("index.lock") && info.size() == 0;
      if ((temporaryPack || staleIndexLock) && info.lastModified().toUTC() < cutoff) {
        QFile::setPermissions(info.absoluteFilePath(), info.permissions() | QFileDevice::WriteOwner);
        QFile::remove(info.absoluteFilePath());
      }
    }

    GitResult cleaned;
    if (settings.fullGc) {
      // recentDays is the preview retention policy. Trees outside that policy have
      // already been explicitly reviewed for release, so adding the same duration
      // as a Git pruning grace period would make "Clean now" retain them again.
      git_.run(repository.gitDir, {QStringLiteral("reflog"), QStringLiteral("expire"),
                                   QStringLiteral("--expire=now"),
                                   QStringLiteral("--expire-unreachable=now"), QStringLiteral("--all")});
      cleaned = git_.run(repository.gitDir,
                         {QStringLiteral("gc"), QStringLiteral("--prune=now")}, {}, 600000);
    } else {
      cleaned = git_.run(repository.gitDir,
                         {QStringLiteral("prune"), QStringLiteral("--expire=now")}, {}, 600000);
    }
    if (!cleaned.ok()) {
      result.error = QStringLiteral("Git cleanup failed in %1: %2")
                         .arg(repository.relativePath, QString::fromUtf8(cleaned.error));
      return result;
    }
    result.messages.push_back(QStringLiteral("Cleaned %1").arg(repository.relativePath));
  }
  result.bytesAfter = untouchedBytes;
  for (const auto& repository : plan.repositories)
    result.bytesAfter += SnapshotScanner::directorySize(repository.gitDir);
  result.success = true;
  return result;
}
}  // namespace ost::core
