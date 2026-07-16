#include "ost/core/snapshot_analyzer.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
struct LocalObject {
  QString oid;
  QString type;
  qint64 expandedBytes = 0;
  qint64 packedBytes = 0;
};

struct Reachability {
  QSet<QString> objects;
  QHash<QString, QString> paths;
  QString error;
};

Reachability reachable(const ost::core::GitClient& git, const QString& gitDir,
                       QVector<QString> hashes) {
  hashes.erase(std::remove_if(hashes.begin(), hashes.end(),
                              [](const auto& hash) { return hash.size() != 40; }),
               hashes.end());
  std::sort(hashes.begin(), hashes.end());
  hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
  if (hashes.isEmpty()) return {};
  const auto input = hashes.join(QChar('\n')).toLatin1() + '\n';
  const auto listed = git.run(gitDir, {QStringLiteral("rev-list"), QStringLiteral("--objects"),
                                       QStringLiteral("--stdin"), QStringLiteral("--missing=allow-any")},
                              input, 120000);
  Reachability result;
  if (!listed.ok()) {
    result.error = QString::fromUtf8(listed.error).trimmed();
    return result;
  }
  for (const auto& raw : listed.output.split('\n')) {
    const auto row = raw.trimmed();
    if (row.size() < 40) continue;
    const auto oid = QString::fromLatin1(row.left(40));
    result.objects.insert(oid);
    if (row.size() > 41) result.paths.insert(oid, QString::fromUtf8(row.mid(41)));
  }
  return result;
}

QString topLevelPath(const QString& path) {
  if (path.isEmpty() || !path.contains('/')) return QStringLiteral("(root files)");
  return path.section('/', 0, 0);
}

QString suffixFor(const QString& path) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  return suffix.isEmpty() ? QStringLiteral("(no extension)")
                          : QStringLiteral(".") + suffix;
}
}  // namespace

namespace ost::core {
SnapshotAnalyzer::SnapshotAnalyzer(GitClient git) : git_(std::move(git)) {}

RepositoryAnalysis SnapshotAnalyzer::analyze(const RepositoryInfo& repository,
                                             const QVector<QString>& retainedTrees) const {
  RepositoryAnalysis result;
  result.gitObjectFilesBytes = repository.gitObjectBytes;

  const auto current = git_.run(repository.gitDir, {QStringLiteral("write-tree")});
  result.currentTree = QString::fromLatin1(current.output.trimmed());
  if (!current.ok() || result.currentTree.size() != 40) {
    result.error = QStringLiteral("Could not read the current snapshot index tree: %1")
                       .arg(QString::fromUtf8(current.error).trimmed());
    return result;
  }

  QHash<QString, LocalObject> local;
  const QDir packDirectory(QDir(repository.gitDir).filePath(QStringLiteral("objects/pack")));
  for (const auto& index : packDirectory.entryInfoList({QStringLiteral("*.idx")}, QDir::Files)) {
    PackUsage pack;
    pack.path = QDir(repository.gitDir).relativeFilePath(
        QDir(index.absolutePath()).filePath(index.completeBaseName() + QStringLiteral(".pack")));
    pack.bytes = QFileInfo(QDir(repository.gitDir).filePath(pack.path)).size();
    const auto verified = git_.run(repository.gitDir,
                                   {QStringLiteral("verify-pack"), QStringLiteral("-v"),
                                    index.absoluteFilePath()}, {}, 120000);
    if (!verified.ok()) {
      result.warnings.push_back(QStringLiteral("Could not inspect %1: %2")
                                    .arg(index.fileName(), QString::fromUtf8(verified.error).trimmed()));
      continue;
    }
    for (const auto& raw : verified.output.split('\n')) {
      const auto fields = QString::fromLatin1(raw).split(QRegularExpression(QStringLiteral("\\s+")),
                                                       Qt::SkipEmptyParts);
      if (fields.size() < 5 || fields[0].size() != 40) continue;
      if (fields[1] != QStringLiteral("blob") && fields[1] != QStringLiteral("tree") &&
          fields[1] != QStringLiteral("commit") && fields[1] != QStringLiteral("tag"))
        continue;
      bool expandedOk = false;
      bool packedOk = false;
      const auto expanded = fields[2].toLongLong(&expandedOk);
      const auto packed = fields[3].toLongLong(&packedOk);
      if (!expandedOk || !packedOk) continue;
      local.insert(fields[0], {fields[0], fields[1], expanded, packed});
      result.packedPayloadBytes += packed;
      ++pack.objects;
    }
    result.packs.push_back(pack);
  }

  const QDir objectsDirectory(QDir(repository.gitDir).filePath(QStringLiteral("objects")));
  for (const auto& fanout : objectsDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (fanout.fileName().size() != 2) continue;
    for (const auto& object : QDir(fanout.absoluteFilePath()).entryInfoList(QDir::Files)) {
      if (object.fileName().size() != 38) continue;
      const auto oid = fanout.fileName() + object.fileName();
      if (!local.contains(oid)) local.insert(oid, {oid, QStringLiteral("object"), 0, object.size()});
    }
  }
  result.localObjects = local.size();

  auto protectedTrees = retainedTrees;
  if (!protectedTrees.contains(result.currentTree)) protectedTrees.push_back(result.currentTree);
  auto historyTrees = retainedTrees;
  historyTrees.erase(std::remove(historyTrees.begin(), historyTrees.end(), result.currentTree),
                     historyTrees.end());
  auto knownTrees = protectedTrees;
  for (const auto& snapshot : repository.snapshots)
    if (!knownTrees.contains(snapshot.hash)) knownTrees.push_back(snapshot.hash);

  const auto currentReachability = reachable(git_, repository.gitDir, {result.currentTree});
  const auto historyReachability = reachable(git_, repository.gitDir, historyTrees);
  const auto retainedReachability = reachable(git_, repository.gitDir, protectedTrees);
  const auto knownReachability = reachable(git_, repository.gitDir, knownTrees);
  if (!currentReachability.error.isEmpty() || !historyReachability.error.isEmpty() ||
      !retainedReachability.error.isEmpty()) {
    result.error = QStringLiteral("Could not calculate Git object reachability: %1 %2 %3")
                       .arg(currentReachability.error, historyReachability.error,
                            retainedReachability.error).trimmed();
    return result;
  }
  if (!knownReachability.error.isEmpty())
    result.warnings.push_back(QStringLiteral("Some historical object paths could not be resolved: %1")
                                  .arg(knownReachability.error));

  QHash<QString, PathUsage> paths;
  QHash<QString, PathUsage> pathEntries;
  QHash<QString, FileTypeUsage> fileTypes;
  QVector<AnalyzedObject> objects;
  objects.reserve(local.size());
  for (auto it = local.cbegin(); it != local.cend(); ++it) {
    const bool isCurrent = currentReachability.objects.contains(it.key());
    const bool isHistory = historyReachability.objects.contains(it.key());
    const bool isRetained = retainedReachability.objects.contains(it.key());
    if (isCurrent) {
      ++result.currentObjects;
      result.currentReachableBytes += it->packedBytes;
    }
    if (isRetained) {
      ++result.retainedObjects;
      result.retainedReachableBytes += it->packedBytes;
    }
    if (isHistory) ++result.historyObjects;
    if (isCurrent && isHistory)
      result.currentSharedBytes += it->packedBytes;
    else if (isCurrent)
      result.currentExclusiveBytes += it->packedBytes;
    else if (isHistory)
      result.historyOnlyBytes += it->packedBytes;
    const auto path = currentReachability.paths.value(
        it.key(), knownReachability.paths.value(it.key(), QStringLiteral("(history-only / internal)")));
    if (it->type == QStringLiteral("blob") && isCurrent) {
      const auto group = topLevelPath(path);
      auto& usage = paths[group];
      usage.path = group;
      usage.expandedBytes += it->expandedBytes;
      usage.packedBytes += it->packedBytes;
      ++usage.objects;

      const auto parts = path.split(QChar('/'), Qt::SkipEmptyParts);
      QString parent;
      for (int index = 0; index < parts.size(); ++index) {
        const auto fullPath = parent.isEmpty() ? parts[index]
                                                : parent + QChar('/') + parts[index];
        auto& entry = pathEntries[fullPath];
        entry.path = fullPath;
        entry.name = parts[index];
        entry.parent = parent;
        entry.directory = index + 1 < parts.size();
        entry.expandedBytes += it->expandedBytes;
        entry.packedBytes += it->packedBytes;
        ++entry.objects;
        parent = fullPath;
      }

      const auto suffix = suffixFor(path);
      auto& type = fileTypes[suffix];
      type.suffix = suffix;
      type.expandedBytes += it->expandedBytes;
      type.packedBytes += it->packedBytes;
      ++type.files;
    }
    if (it->type == QStringLiteral("blob"))
      objects.push_back({it->oid, it->type, path, it->expandedBytes, it->packedBytes,
                         isCurrent, isRetained, isHistory});
  }

  result.estimatedReclaimableBytes =
      std::max<qint64>(0, result.packedPayloadBytes - result.retainedReachableBytes);
  result.topPaths = paths.values();
  std::sort(result.topPaths.begin(), result.topPaths.end(),
            [](const auto& left, const auto& right) { return left.packedBytes > right.packedBytes; });
  if (result.topPaths.size() > 20) result.topPaths.resize(20);
  result.pathEntries = pathEntries.values();
  std::sort(result.pathEntries.begin(), result.pathEntries.end(), [](const auto& left,
                                                                     const auto& right) {
    if (left.parent != right.parent) return left.parent < right.parent;
    if (left.directory != right.directory) return left.directory;
    if (left.packedBytes != right.packedBytes) return left.packedBytes > right.packedBytes;
    return left.name < right.name;
  });
  result.fileTypes = fileTypes.values();
  std::sort(result.fileTypes.begin(), result.fileTypes.end(), [](const auto& left,
                                                                 const auto& right) {
    return left.packedBytes > right.packedBytes;
  });
  std::sort(objects.begin(), objects.end(), [](const auto& left, const auto& right) {
    return left.packedBytes > right.packedBytes;
  });
  result.largestObjects = std::move(objects);
  std::sort(result.packs.begin(), result.packs.end(),
            [](const auto& left, const auto& right) { return left.bytes > right.bytes; });
  result.success = true;
  return result;
}
}  // namespace ost::core
