#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ost::core {

enum class SnapshotSource { Database, GitInferred, CurrentIndex };
enum class RepositoryActivityState { Inactive, Active, PossiblyActive };

struct RepositoryActivity {
  RepositoryActivityState state = RepositoryActivityState::Inactive;
  QVector<qint64> processIds;
};

struct SnapshotInfo {
  QString hash;
  QString projectId;
  QString directory;
  QStringList titles;
  QDateTime firstSeen;
  QDateTime lastSeen;
  int references = 0;
  int sessions = 0;
  SnapshotSource source = SnapshotSource::Database;
  bool exists = false;
  bool keep = false;
  QString keepReason;
};

struct RepositoryFileInfo {
  QString relativePath;
  qint64 bytes = 0;
};

struct RepositoryInfo {
  QString gitDir;
  QString relativePath;
  QString projectId;
  QString worktree;
  qint64 actualBytes = 0;
  qint64 gitObjectBytes = 0;
  qint64 lfsBytes = 0;
  qint64 tempPackBytes = 0;
  int looseObjects = 0;
  int packedObjects = 0;
  RepositoryActivity activity;
  QVector<RepositoryFileInfo> largestFiles;
  QVector<SnapshotInfo> snapshots;
};

struct ScanResult {
  QString snapshotRoot;
  QString databasePath;
  QVector<RepositoryInfo> repositories;
  QStringList warnings;
  int unmappedDatabaseRecords = 0;
  qint64 totalBytes = 0;
};

struct RetentionSettings {
  int recentDays = 7;
  int fallbackCount = 10;
};

struct CleanupSettings : RetentionSettings {
  bool fullGc = true;
  bool pruneLfs = true;
  int staleFileHours = 24;
};

struct RepositoryCleanupPlan {
  QString gitDir;
  QString relativePath;
  QString projectId;
  QString worktree;
  RepositoryActivity activity;
  QString allowedRoot;
  QVector<QString> keepHashes;
  QVector<QString> removeHashes;
  qint64 currentBytes = 0;
  qint64 removableLfsBytes = 0;
  qint64 staleTempBytes = 0;
};

struct CleanupPlan {
  QVector<RepositoryCleanupPlan> repositories;
  QVector<RepositoryCleanupPlan> blockedRepositories;
  QString databasePath;
  qint64 currentBytes = 0;
  qint64 estimatedReclaimableBytes = 0;
  int keepTrees = 0;
  int removeTrees = 0;
  bool resetHistory = false;
  bool purgeStore = false;
  QString error;
};

struct CleanupResult {
  bool success = false;
  qint64 bytesBefore = 0;
  qint64 bytesAfter = 0;
  QStringList messages;
  QString error;
};

}  // namespace ost::core
