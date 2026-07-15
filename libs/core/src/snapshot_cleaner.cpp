#include "ost/core/snapshot_cleaner.h"

#include "ost/core/snapshot_scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

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
}  // namespace

namespace ost::core {
SnapshotCleaner::SnapshotCleaner(GitClient git) : git_(std::move(git)) {}

CleanupPlan SnapshotCleaner::preview(const ScanResult& scan, const CleanupSettings& settings) const {
  CleanupPlan plan;
  plan.currentBytes = scan.totalBytes;
  for (const auto& repository : scan.repositories) {
    if (repository.snapshots.isEmpty()) continue;
    RepositoryCleanupPlan item;
    item.gitDir = repository.gitDir;
    item.relativePath = repository.relativePath;
    item.currentBytes = repository.actualBytes;
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

CleanupResult SnapshotCleaner::execute(const CleanupPlan& plan, const CleanupSettings& settings) const {
  CleanupResult result;
  qint64 plannedBytesAtPreview = 0;
  for (const auto& repository : plan.repositories) plannedBytesAtPreview += repository.currentBytes;
  const auto untouchedBytes = std::max<qint64>(0, plan.currentBytes - plannedBytesAtPreview);
  result.bytesBefore = untouchedBytes;
  for (const auto& repository : plan.repositories)
    result.bytesBefore += SnapshotScanner::directorySize(repository.gitDir);
  for (const auto& repository : plan.repositories) {
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
