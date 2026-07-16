#pragma once

#include "ost/core/git_client.h"
#include "ost/core/snapshot_types.h"

namespace ost::core {

struct AnalyzedObject {
  QString oid;
  QString type;
  QString path;
  qint64 expandedBytes = 0;
  qint64 packedBytes = 0;
  bool current = false;
  bool retained = false;
};

struct PathUsage {
  QString path;
  qint64 expandedBytes = 0;
  qint64 packedBytes = 0;
  int objects = 0;
};

struct PackUsage {
  QString path;
  qint64 bytes = 0;
  int objects = 0;
};

struct RepositoryAnalysis {
  bool success = false;
  QString error;
  QStringList warnings;
  QString currentTree;
  qint64 gitObjectFilesBytes = 0;
  qint64 packedPayloadBytes = 0;
  qint64 currentReachableBytes = 0;
  qint64 retainedReachableBytes = 0;
  qint64 estimatedReclaimableBytes = 0;
  int localObjects = 0;
  int currentObjects = 0;
  int retainedObjects = 0;
  QVector<PathUsage> topPaths;
  QVector<AnalyzedObject> largestObjects;
  QVector<PackUsage> packs;
};

class SnapshotAnalyzer {
 public:
  explicit SnapshotAnalyzer(GitClient git = {});
  RepositoryAnalysis analyze(const RepositoryInfo& repository,
                             const QVector<QString>& retainedTrees) const;

 private:
  GitClient git_;
};

}  // namespace ost::core
