#include "snapshot_controller.h"

#include "ost/core/retention_policy.h"
#include "ost/core/opencode_process_detector.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QtConcurrent>

#include <algorithm>

using namespace ost::core;

namespace {
QString objectSuffix(const QString& path) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  return suffix.isEmpty() ? QStringLiteral("(no extension)")
                          : QStringLiteral(".") + suffix;
}

QString pidList(const QVector<qint64>& pids) {
  QStringList values;
  for (const auto pid : pids) values.push_back(QString::number(pid));
  return values.join(QStringLiteral(", "));
}
}  // namespace

SnapshotController::SnapshotController(QObject* parent) : QObject(parent), settings_(AppSettingsStore().load()) {
  connect(&scanWatcher_, &QFutureWatcher<ScanResult>::finished, this, [this] {
    scanResult_ = scanWatcher_.result();
    selectedRepository_ = -1;
    for (int index = 0; index < scanResult_.repositories.size(); ++index)
      if (selectedRepository_ < 0 || scanResult_.repositories[index].actualBytes >
                                         scanResult_.repositories[selectedRepository_].actualBytes)
        selectedRepository_ = index;
    repositoryAnalysis_ = {};
    analysisPath_.clear();
    analysisObjectPathFilter_.clear();
    analysisObjectSuffixFilter_.clear();
    projectPlan_ = {};
    projectPlanMode_.clear();
    cleanupPlan_ = {};
    auto message = tr("Found %1 repositories and %2 snapshot records")
                       .arg(repositoryCount()).arg(snapshotCount());
    if (!scanResult_.warnings.isEmpty())
      message += tr(" — Warning: %1").arg(scanResult_.warnings.join(QStringLiteral(" · ")));
    const auto active = std::count_if(scanResult_.repositories.cbegin(),
                                      scanResult_.repositories.cend(), [](const auto& repository) {
      return repository.activity.state == RepositoryActivityState::Active;
    });
    const auto possible = std::count_if(scanResult_.repositories.cbegin(),
                                        scanResult_.repositories.cend(), [](const auto& repository) {
      return repository.activity.state == RepositoryActivityState::PossiblyActive;
    });
    if (active || possible)
      message += tr(" — %1 active, %2 uncertain; only matching stores are protected")
                     .arg(active).arg(possible);
    setStatus(std::move(message));
    emit dataChanged();
    emit planChanged();
    emit projectPlanChanged();
    notifyBusy();
  });
  connect(&previewWatcher_, &QFutureWatcher<CleanupPlan>::finished, this, [this] {
    cleanupPlan_ = previewWatcher_.result();
    setStatus(tr("Preview ready: %1 inactive stores included, %2 active/uncertain stores skipped; %3 trees can be released")
                  .arg(cleanupPlan_.repositories.size())
                  .arg(cleanupPlan_.blockedRepositories.size())
                  .arg(cleanupPlan_.removeTrees));
    emit planChanged();
    notifyBusy();
  });
  connect(&cleanupWatcher_, &QFutureWatcher<CleanupResult>::finished, this, [this] {
    const auto result = cleanupWatcher_.result();
    if (!result.success) {
      setStatus(tr("Cleanup failed: %1").arg(result.error));
      notifyBusy();
      return;
    }
    setStatus(tr("Cleanup complete: reclaimed %1. Rescanning…")
                  .arg(formatBytes(std::max<qint64>(0, result.bytesBefore - result.bytesAfter))));
    notifyBusy();
    scan();
  });
  connect(&analysisWatcher_, &QFutureWatcher<RepositoryAnalysis>::finished, this, [this] {
    const auto analyzedIndex = analysisRepositoryIndex_;
    const auto analysis = analysisWatcher_.result();
    analysisRepositoryIndex_ = -1;
    if (analyzedIndex == selectedRepository_) {
      repositoryAnalysis_ = analysis;
      setStatus(analysis.success
                    ? tr("Deep analysis complete: %1 potentially reclaimable from unprotected Git objects")
                          .arg(formatBytes(analysis.estimatedReclaimableBytes))
                    : tr("Deep analysis failed: %1").arg(analysis.error));
    }
    emit analysisChanged();
    notifyBusy();
  });
  connect(&projectPreviewWatcher_, &QFutureWatcher<CleanupPlan>::finished, this, [this] {
    const auto previewIndex = projectPreviewRepositoryIndex_;
    projectPreviewRepositoryIndex_ = -1;
    if (previewIndex != selectedRepository_) {
      projectPlan_ = {};
      projectPlanMode_.clear();
      setStatus(tr("Discarded project preview because the selected repository changed"));
      emit projectPlanChanged();
      notifyBusy();
      return;
    }
    projectPlan_ = projectPreviewWatcher_.result();
    if (!projectPlan_.error.isEmpty()) {
      setStatus(tr("Project preview failed: %1").arg(projectPlan_.error));
      projectPlan_ = {};
      projectPlanMode_.clear();
    } else {
      setStatus(projectPlanMode_ == QStringLiteral("reset")
                    ? tr("History reset preview ready: keep current state and release %1 known trees")
                          .arg(projectPlan_.removeTrees)
                    : projectPlanMode_ == QStringLiteral("purge")
                    ? tr("Full-store purge preview ready: remove %1 and all Undo state for this repository")
                          .arg(formatBytes(projectPlan_.estimatedReclaimableBytes))
                    : tr("Project cleanup preview ready: release %1 trees outside retention")
                          .arg(projectPlan_.removeTrees));
    }
    emit projectPlanChanged();
    notifyBusy();
  });
  setStatus(tr("Ready. Scan is read-only; cleanup always requires a preview and confirmation."));
}

QString SnapshotController::snapshotRoot() const { return settings_.snapshotRoot; }
QString SnapshotController::databasePath() const { return settings_.databasePath; }
int SnapshotController::recentDays() const { return settings_.cleanup.recentDays; }
int SnapshotController::fallbackCount() const { return settings_.cleanup.fallbackCount; }
bool SnapshotController::fullGc() const { return settings_.cleanup.fullGc; }
bool SnapshotController::pruneLfs() const { return settings_.cleanup.pruneLfs; }
int SnapshotController::staleFileHours() const { return settings_.cleanup.staleFileHours; }
bool SnapshotController::busy() const { return scanWatcher_.isRunning() || previewWatcher_.isRunning() || cleanupWatcher_.isRunning() || analysisWatcher_.isRunning() || projectPreviewWatcher_.isRunning(); }
bool SnapshotController::analysisBusy() const { return analysisWatcher_.isRunning(); }
QString SnapshotController::analysisPath() const { return analysisPath_; }
QString SnapshotController::analysisObjectFilter() const {
  if (!analysisObjectPathFilter_.isEmpty()) return analysisObjectPathFilter_;
  return analysisObjectSuffixFilter_;
}
bool SnapshotController::hasProjectPlan() const { return !projectPlan_.repositories.isEmpty(); }
QString SnapshotController::projectPlanMode() const { return projectPlanMode_; }
int SnapshotController::projectPlanRemoveTrees() const { return projectPlan_.removeTrees; }
qint64 SnapshotController::projectPlanEstimatedBytes() const {
  return projectPlan_.estimatedReclaimableBytes;
}
QString SnapshotController::status() const { return status_; }
int SnapshotController::selectedRepository() const { return selectedRepository_; }
qint64 SnapshotController::totalBytes() const { return scanResult_.totalBytes; }
int SnapshotController::repositoryCount() const { return scanResult_.repositories.size(); }
qint64 SnapshotController::estimatedReclaimableBytes() const { return cleanupPlan_.estimatedReclaimableBytes; }
bool SnapshotController::hasPlan() const { return !cleanupPlan_.repositories.isEmpty(); }
int SnapshotController::planKeepTrees() const { return cleanupPlan_.keepTrees; }
int SnapshotController::planRemoveTrees() const { return cleanupPlan_.removeTrees; }
int SnapshotController::planRepositoryCount() const { return cleanupPlan_.repositories.size(); }
int SnapshotController::planBlockedCount() const { return cleanupPlan_.blockedRepositories.size(); }
QString SnapshotController::planBlockedSummary() const {
  QStringList rows;
  for (const auto& repository : cleanupPlan_.blockedRepositories) {
    const auto pids = pidList(repository.activity.processIds);
    rows.push_back(pids.isEmpty() ? repository.relativePath
                                  : QStringLiteral("%1 (PID %2)").arg(repository.relativePath, pids));
  }
  return rows.join(QStringLiteral(" · "));
}

int SnapshotController::snapshotCount() const {
  int total = 0;
  for (const auto& repo : scanResult_.repositories) total += repo.snapshots.size();
  return total;
}
int SnapshotController::keepCount() const {
  int total = 0;
  for (const auto& repo : scanResult_.repositories)
    total += static_cast<int>(std::count_if(repo.snapshots.cbegin(), repo.snapshots.cend(), [](const auto& s) { return s.keep; }));
  return total;
}
int SnapshotController::dropCount() const { return snapshotCount() - keepCount(); }

QVariantList SnapshotController::repositories() const {
  QVariantList rows;
  QVector<int> indices;
  indices.reserve(scanResult_.repositories.size());
  for (int index = 0; index < scanResult_.repositories.size(); ++index) indices.push_back(index);
  std::sort(indices.begin(), indices.end(), [this](int left, int right) {
    return scanResult_.repositories[left].actualBytes > scanResult_.repositories[right].actualBytes;
  });
  for (const int index : indices) {
    const auto& repo = scanResult_.repositories[index];
    int kept = 0;
    for (const auto& item : repo.snapshots) if (item.keep) ++kept;
    rows.push_back(QVariantMap{{"index", index}, {"name", repo.relativePath}, {"worktree", repo.worktree},
                               {"bytes", repo.actualBytes}, {"bytesText", formatBytes(repo.actualBytes)},
                               {"lfsText", formatBytes(repo.lfsBytes)}, {"snapshots", repo.snapshots.size()},
                               {"kept", kept}, {"dropped", repo.snapshots.size() - kept},
                               {"activity", repo.activity.state == RepositoryActivityState::Active
                                                ? QStringLiteral("active")
                                                : repo.activity.state == RepositoryActivityState::PossiblyActive
                                                ? QStringLiteral("possible") : QStringLiteral("inactive")},
                               {"activityLabel", repo.activity.state == RepositoryActivityState::Active
                                                ? tr("ACTIVE")
                                                : repo.activity.state == RepositoryActivityState::PossiblyActive
                                                ? tr("POSSIBLY ACTIVE") : tr("INACTIVE")},
                               {"processIds", pidList(repo.activity.processIds)}});
  }
  return rows;
}

QVariantMap SnapshotController::selectedRepositoryDetails() const {
  if (selectedRepository_ < 0 || selectedRepository_ >= scanResult_.repositories.size())
    return {{"valid", false}};

  const auto& repo = scanResult_.repositories[selectedRepository_];
  const auto metadataBytes = std::max<qint64>(
      0, repo.actualBytes - repo.gitObjectBytes - repo.lfsBytes - repo.tempPackBytes);
  const auto percent = [total = repo.actualBytes](qint64 bytes) {
    return total > 0 ? static_cast<double>(bytes) * 100.0 / static_cast<double>(total) : 0.0;
  };
  const QVariantList categories{
      QVariantMap{{"label", tr("Git objects")}, {"bytes", repo.gitObjectBytes},
                  {"bytesText", formatBytes(repo.gitObjectBytes)}, {"percent", percent(repo.gitObjectBytes)},
                  {"color", QStringLiteral("#0055ff")}},
      QVariantMap{{"label", tr("Git LFS")}, {"bytes", repo.lfsBytes},
                  {"bytesText", formatBytes(repo.lfsBytes)}, {"percent", percent(repo.lfsBytes)},
                  {"color", QStringLiteral("#ffcc00")}},
      QVariantMap{{"label", tr("Temporary packs")}, {"bytes", repo.tempPackBytes},
                  {"bytesText", formatBytes(repo.tempPackBytes)}, {"percent", percent(repo.tempPackBytes)},
                  {"color", QStringLiteral("#e63b2e")}},
      QVariantMap{{"label", tr("Metadata")}, {"bytes", metadataBytes},
                  {"bytesText", formatBytes(metadataBytes)}, {"percent", percent(metadataBytes)},
                  {"color", QStringLiteral("#1a1a1a")}},
  };

  const qint64 dominantBytes = std::max({repo.gitObjectBytes, repo.lfsBytes,
                                         repo.tempPackBytes, metadataBytes});
  QString explanation;
  if (repo.actualBytes == 0)
    explanation = tr("This snapshot repository is empty.");
  else if (dominantBytes == repo.gitObjectBytes)
    explanation = tr("Git objects dominate this repository. Pack data is shared by retained trees; releasing a snapshot record only frees objects that no retained or current tree can reach.");
  else if (dominantBytes == repo.lfsBytes)
    explanation = tr("Git LFS objects dominate this repository. An LFS object is removable only when none of the retained trees references its SHA-256 object ID.");
  else if (dominantBytes == repo.tempPackBytes)
    explanation = tr("Temporary Git pack files dominate this repository. Stale temporary packs are directly removable after the configured age threshold.");
  else
    explanation = tr("Repository metadata dominates this repository. This includes the index, refs, configuration, logs, and other non-object Git files.");

  QVariantList largestFiles;
  for (const auto& file : repo.largestFiles)
    largestFiles.push_back(QVariantMap{{"path", QDir::fromNativeSeparators(file.relativePath)},
                                       {"bytes", file.bytes}, {"bytesText", formatBytes(file.bytes)}});
  if (!repo.largestFiles.isEmpty())
    explanation += tr(" Largest file: %1 (%2).")
                       .arg(QDir::fromNativeSeparators(repo.largestFiles.front().relativePath),
                            formatBytes(repo.largestFiles.front().bytes));

  int duplicateWorktrees = 0;
  qint64 combinedWorktreeBytes = repo.actualBytes;
  auto comparableWorktree = QDir::cleanPath(repo.worktree);
#ifdef Q_OS_WIN
  comparableWorktree = comparableWorktree.toLower();
#endif
  if (!repo.worktree.isEmpty()) {
    for (int index = 0; index < scanResult_.repositories.size(); ++index) {
      if (index == selectedRepository_) continue;
      auto otherWorktree = QDir::cleanPath(scanResult_.repositories[index].worktree);
#ifdef Q_OS_WIN
      otherWorktree = otherWorktree.toLower();
#endif
      if (otherWorktree != comparableWorktree) continue;
      ++duplicateWorktrees;
      combinedWorktreeBytes += scanResult_.repositories[index].actualBytes;
    }
  }
  if (duplicateWorktrees > 0)
    explanation += tr(" %1 other snapshot repository points to the same worktree; together they use %2.")
                       .arg(duplicateWorktrees).arg(formatBytes(combinedWorktreeBytes));

  const auto activityCode = repo.activity.state == RepositoryActivityState::Active
                                ? QStringLiteral("active")
                                : repo.activity.state == RepositoryActivityState::PossiblyActive
                                ? QStringLiteral("possible") : QStringLiteral("inactive");
  const auto pids = pidList(repo.activity.processIds);
  const auto activityLabel = repo.activity.state == RepositoryActivityState::Active
                                 ? tr("ACTIVE — PID %1").arg(pids)
                                 : repo.activity.state == RepositoryActivityState::PossiblyActive
                                 ? tr("POSSIBLY ACTIVE — PID %1").arg(pids)
                                 : tr("INACTIVE");
  const auto activityMessage = repo.activity.state == RepositoryActivityState::Active
      ? tr("This exact snapshot store is used by OpenCode PID %1. Close only that matching instance before cleanup; other OpenCode projects can remain open.").arg(pids)
      : repo.activity.state == RepositoryActivityState::PossiblyActive
      ? tr("OpenCode PID %1 may access multiple projects or its worktree could not be read. Cleanup is blocked until this process is identified or closed.").arg(pids)
      : tr("No running OpenCode process maps to this snapshot store. Execution checks the process list, Git locks, and store writes again before changing anything.");

  return {{"valid", true}, {"name", repo.relativePath}, {"worktree", repo.worktree},
          {"gitDir", repo.gitDir}, {"totalBytes", repo.actualBytes},
          {"totalBytesText", formatBytes(repo.actualBytes)}, {"categories", categories},
          {"looseObjects", repo.looseObjects}, {"packedObjects", repo.packedObjects},
          {"largestFiles", largestFiles}, {"duplicateWorktrees", duplicateWorktrees},
          {"combinedWorktreeBytesText", formatBytes(combinedWorktreeBytes)},
          {"explanation", explanation}, {"activity", activityCode},
          {"activityLabel", activityLabel}, {"activityMessage", activityMessage},
          {"processIds", pids}, {"canClean", repo.activity.state == RepositoryActivityState::Inactive}};
}

QVariantMap SnapshotController::repositoryAnalysis() const {
  if (!repositoryAnalysis_.success)
    return {{"ready", false}, {"error", repositoryAnalysis_.error}};
  const auto& analysis = repositoryAnalysis_;
  const auto percent = [](qint64 part, qint64 total) {
    return total > 0 ? static_cast<double>(part) * 100.0 / static_cast<double>(total) : 0.0;
  };
  QVariantList paths;
  for (const auto& path : analysis.pathEntries) {
    if (path.parent != analysisPath_) continue;
    paths.push_back(QVariantMap{{"path", path.path}, {"name", path.name},
                                {"directory", path.directory},
                                {"bytes", path.packedBytes},
                                {"bytesText", formatBytes(path.packedBytes)},
                                {"expandedText", formatBytes(path.expandedBytes)},
                                {"objects", path.objects},
                                {"percent", percent(path.packedBytes, analysis.currentReachableBytes)}});
  }
  QVariantList fileTypes;
  for (const auto& type : analysis.fileTypes)
    fileTypes.push_back(QVariantMap{{"suffix", type.suffix}, {"bytes", type.packedBytes},
                                    {"bytesText", formatBytes(type.packedBytes)},
                                    {"expandedText", formatBytes(type.expandedBytes)},
                                    {"files", type.files},
                                    {"percent", percent(type.packedBytes, analysis.currentReachableBytes)}});
  QVariantList objects;
  int objectMatches = 0;
  for (const auto& object : analysis.largestObjects) {
    if (!analysisObjectPathFilter_.isEmpty() && object.path != analysisObjectPathFilter_) continue;
    if (!analysisObjectSuffixFilter_.isEmpty() &&
        objectSuffix(object.path) != analysisObjectSuffixFilter_) continue;
    ++objectMatches;
    if (objects.size() >= 100) continue;
    objects.push_back(QVariantMap{{"oid", object.oid}, {"shortOid", object.oid.left(10)},
                                  {"path", object.path}, {"type", object.type},
                                  {"expandedText", formatBytes(object.expandedBytes)},
                                  {"packedText", formatBytes(object.packedBytes)},
                                  {"current", object.current}, {"retained", object.retained},
                                  {"history", object.history}});
  }
  QVariantList packs;
  for (const auto& pack : analysis.packs)
    packs.push_back(QVariantMap{{"path", QDir::fromNativeSeparators(pack.path)},
                                {"bytes", pack.bytes}, {"bytesText", formatBytes(pack.bytes)},
                                {"objects", pack.objects}});
  const auto retainedHistory = std::max<qint64>(0, analysis.retainedReachableBytes -
                                                       analysis.currentReachableBytes);
  const auto resetReclaimable = std::max<qint64>(0, analysis.packedPayloadBytes -
                                                        analysis.currentReachableBytes);
  return {{"ready", true}, {"currentTree", analysis.currentTree},
          {"gitObjectFilesBytes", analysis.gitObjectFilesBytes},
          {"gitObjectFilesText", formatBytes(analysis.gitObjectFilesBytes)},
          {"packedPayloadText", formatBytes(analysis.packedPayloadBytes)},
          {"currentReachableBytes", analysis.currentReachableBytes},
          {"currentReachableText", formatBytes(analysis.currentReachableBytes)},
          {"retainedHistoryBytes", retainedHistory},
          {"retainedHistoryText", formatBytes(retainedHistory)},
          {"currentExclusiveBytes", analysis.currentExclusiveBytes},
          {"currentExclusiveText", formatBytes(analysis.currentExclusiveBytes)},
          {"currentSharedBytes", analysis.currentSharedBytes},
          {"currentSharedText", formatBytes(analysis.currentSharedBytes)},
          {"historyOnlyBytes", analysis.historyOnlyBytes},
          {"historyOnlyText", formatBytes(analysis.historyOnlyBytes)},
          {"estimatedReclaimableBytes", analysis.estimatedReclaimableBytes},
          {"estimatedReclaimableText", formatBytes(analysis.estimatedReclaimableBytes)},
          {"resetReclaimableBytes", resetReclaimable},
          {"resetReclaimableText", formatBytes(resetReclaimable)},
          {"currentPercent", percent(analysis.currentReachableBytes, analysis.packedPayloadBytes)},
          {"retainedPercent", percent(retainedHistory, analysis.packedPayloadBytes)},
          {"reclaimablePercent", percent(analysis.estimatedReclaimableBytes, analysis.packedPayloadBytes)},
          {"localObjects", analysis.localObjects}, {"currentObjects", analysis.currentObjects},
          {"retainedObjects", analysis.retainedObjects}, {"historyObjects", analysis.historyObjects},
          {"analysisPath", analysisPath_}, {"canGoUp", !analysisPath_.isEmpty()},
          {"objectFilter", analysisObjectFilter()}, {"objectMatches", objectMatches},
          {"paths", paths}, {"fileTypes", fileTypes}, {"objects", objects},
          {"packs", packs}, {"warnings", analysis.warnings}};
}

QVariantList SnapshotController::snapshots() const {
  QVariantList rows;
  if (selectedRepository_ < 0 || selectedRepository_ >= scanResult_.repositories.size()) return rows;
  for (const auto& item : scanResult_.repositories[selectedRepository_].snapshots) {
    const auto source = item.source == SnapshotSource::Database ? tr("Database")
                      : item.source == SnapshotSource::CurrentIndex ? tr("Current index") : tr("Git inferred");
    rows.push_back(QVariantMap{{"hash", item.hash}, {"shortHash", item.hash.left(10)},
                               {"title", item.titles.isEmpty() ? tr("Untitled snapshot") : item.titles.join(QStringLiteral(" · "))},
                               {"time", item.lastSeen.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
                               {"source", source}, {"keep", item.keep}, {"reason", item.keepReason},
                               {"references", item.references}, {"sessions", item.sessions}});
  }
  return rows;
}

void SnapshotController::setSnapshotRoot(const QString& value) {
  if (value == settings_.snapshotRoot) return;
  settings_.snapshotRoot = QDir::cleanPath(value);
  scanResult_ = {};
  selectedRepository_ = -1;
  persistSettings();
  emit dataChanged();
}
void SnapshotController::setDatabasePath(const QString& value) {
  if (value == settings_.databasePath) return;
  settings_.databasePath = QDir::cleanPath(value);
  scanResult_ = {};
  selectedRepository_ = -1;
  persistSettings();
  emit dataChanged();
}
void SnapshotController::setRecentDays(int value) {
  value = std::clamp(value, 0, 3650);
  if (value == settings_.cleanup.recentDays) return;
  settings_.cleanup.recentDays = value;
  RetentionPolicy::apply(scanResult_.repositories,
                         RetentionSettings{settings_.cleanup.recentDays, settings_.cleanup.fallbackCount});
  persistSettings();
  emit dataChanged();
}
void SnapshotController::setFallbackCount(int value) {
  value = std::clamp(value, 1, 1000);
  if (value == settings_.cleanup.fallbackCount) return;
  settings_.cleanup.fallbackCount = value;
  RetentionPolicy::apply(scanResult_.repositories,
                         RetentionSettings{settings_.cleanup.recentDays, settings_.cleanup.fallbackCount});
  persistSettings();
  emit dataChanged();
}
void SnapshotController::setFullGc(bool value) { if (value != settings_.cleanup.fullGc) { settings_.cleanup.fullGc = value; persistSettings(); } }
void SnapshotController::setPruneLfs(bool value) { if (value != settings_.cleanup.pruneLfs) { settings_.cleanup.pruneLfs = value; persistSettings(); } }
void SnapshotController::setStaleFileHours(int value) {
  value = std::clamp(value, 1, 24 * 365);
  if (value != settings_.cleanup.staleFileHours) {
    settings_.cleanup.staleFileHours = value;
    persistSettings();
  }
}
void SnapshotController::setSelectedRepository(int value) {
  if (value == selectedRepository_) return;
  selectedRepository_ = value;
  repositoryAnalysis_ = {};
  analysisPath_.clear();
  analysisObjectPathFilter_.clear();
  analysisObjectSuffixFilter_.clear();
  projectPlan_ = {};
  projectPlanMode_.clear();
  emit dataChanged();
  emit analysisChanged();
  emit projectPlanChanged();
}

void SnapshotController::scan() {
  if (busy()) return;
  repositoryAnalysis_ = {};
  analysisPath_.clear();
  analysisObjectPathFilter_.clear();
  analysisObjectSuffixFilter_.clear();
  projectPlan_ = {};
  projectPlanMode_.clear();
  emit analysisChanged();
  emit projectPlanChanged();
  persistSettings();
  setStatus(tr("Scanning snapshot storage…"));
  notifyBusy();
  const auto root = settings_.snapshotRoot;
  const auto database = settings_.databasePath;
  const RetentionSettings retention{settings_.cleanup.recentDays, settings_.cleanup.fallbackCount};
  const QPointer<SnapshotController> self(this);
  scanWatcher_.setFuture(QtConcurrent::run([root, database, retention, self] {
    auto result = SnapshotScanner().scan(root, database, retention, [self](const QString& message) {
      if (!self) return;
      QMetaObject::invokeMethod(self, [self, message] {
        if (self) self->setStatus(message + QStringLiteral("…"));
      }, Qt::QueuedConnection);
    });
    if (self) QMetaObject::invokeMethod(self, [self] {
      if (self) self->setStatus(QObject::tr("Mapping running OpenCode instances to snapshot stores…"));
    }, Qt::QueuedConnection);
    OpenCodeProcessDetector().detect(result.repositories, database);
    return result;
  }));
  notifyBusy();
}

void SnapshotController::previewCleanup() {
  if (busy() || scanResult_.repositories.isEmpty()) return;
  setStatus(tr("Calculating cleanup preview…"));
  const auto scanCopy = scanResult_;
  const auto settings = cleanupSettings();
  previewWatcher_.setFuture(QtConcurrent::run([scanCopy, settings] { return SnapshotCleaner().preview(scanCopy, settings); }));
  notifyBusy();
}

void SnapshotController::executeCleanup() {
  if (busy() || !hasPlan()) return;
  setStatus(tr("Cleaning snapshot storage. Do not close OpenCode Snapshot Tool…"));
  const auto plan = cleanupPlan_;
  const auto settings = cleanupSettings();
  cleanupWatcher_.setFuture(QtConcurrent::run([plan, settings] { return SnapshotCleaner().execute(plan, settings); }));
  notifyBusy();
}

void SnapshotController::analyzeSelectedRepository() {
  if (busy() || selectedRepository_ < 0 || selectedRepository_ >= scanResult_.repositories.size()) return;
  repositoryAnalysis_ = {};
  analysisPath_.clear();
  analysisObjectPathFilter_.clear();
  analysisObjectSuffixFilter_.clear();
  analysisRepositoryIndex_ = selectedRepository_;
  const auto repository = scanResult_.repositories[selectedRepository_];
  QVector<QString> retained;
  for (const auto& snapshot : repository.snapshots)
    if (snapshot.keep && !retained.contains(snapshot.hash)) retained.push_back(snapshot.hash);
  setStatus(tr("Deep-analyzing Git objects for %1…").arg(repository.relativePath));
  emit analysisChanged();
  notifyBusy();
  analysisWatcher_.setFuture(QtConcurrent::run([repository, retained] {
    return SnapshotAnalyzer().analyze(repository, retained);
  }));
}

void SnapshotController::navigateAnalysisPath(const QString& path) {
  if (!repositoryAnalysis_.success) return;
  const auto normalized = QDir::fromNativeSeparators(path).remove(QRegularExpression(QStringLiteral("^/+|/+$")));
  const auto found = std::find_if(repositoryAnalysis_.pathEntries.cbegin(),
                                  repositoryAnalysis_.pathEntries.cend(),
                                  [&normalized](const auto& entry) {
                                    return entry.directory && entry.path == normalized;
                                  });
  if (found == repositoryAnalysis_.pathEntries.cend() || normalized == analysisPath_) return;
  analysisPath_ = normalized;
  emit analysisChanged();
}

void SnapshotController::navigateAnalysisPathUp() {
  if (analysisPath_.isEmpty()) return;
  const auto slash = analysisPath_.lastIndexOf(QChar('/'));
  analysisPath_ = slash < 0 ? QString() : analysisPath_.left(slash);
  emit analysisChanged();
}

void SnapshotController::filterAnalysisObjects(const QString& path, const QString& suffix) {
  if (!repositoryAnalysis_.success) return;
  analysisObjectPathFilter_ = QDir::fromNativeSeparators(path);
  analysisObjectSuffixFilter_ = suffix.toLower();
  emit analysisChanged();
}

void SnapshotController::clearAnalysisObjectFilter() {
  if (analysisObjectPathFilter_.isEmpty() && analysisObjectSuffixFilter_.isEmpty()) return;
  analysisObjectPathFilter_.clear();
  analysisObjectSuffixFilter_.clear();
  emit analysisChanged();
}

void SnapshotController::previewProjectCleanup() {
  if (busy() || selectedRepository_ < 0 || selectedRepository_ >= scanResult_.repositories.size()) return;
  projectPlan_ = {};
  projectPlanMode_ = QStringLiteral("safe");
  const auto repository = scanResult_.repositories[selectedRepository_];
  projectPreviewRepositoryIndex_ = selectedRepository_;
  const auto settings = cleanupSettings();
  const auto database = settings_.databasePath;
  setStatus(tr("Previewing safe cleanup for %1…").arg(repository.relativePath));
  emit projectPlanChanged();
  notifyBusy();
  projectPreviewWatcher_.setFuture(QtConcurrent::run([repository, settings, database] {
    ScanResult scan;
    scan.databasePath = database;
    scan.totalBytes = repository.actualBytes;
    scan.repositories = {repository};
    return SnapshotCleaner().preview(scan, settings);
  }));
}

void SnapshotController::previewProjectReset() {
  if (busy() || selectedRepository_ < 0 || selectedRepository_ >= scanResult_.repositories.size()) return;
  projectPlan_ = {};
  projectPlanMode_ = QStringLiteral("reset");
  const auto repository = scanResult_.repositories[selectedRepository_];
  projectPreviewRepositoryIndex_ = selectedRepository_;
  const auto settings = cleanupSettings();
  const auto database = settings_.databasePath;
  setStatus(tr("Previewing snapshot-history reset for %1…").arg(repository.relativePath));
  emit projectPlanChanged();
  notifyBusy();
  projectPreviewWatcher_.setFuture(QtConcurrent::run([repository, settings, database] {
    return SnapshotCleaner().previewReset(repository, settings, database);
  }));
}

void SnapshotController::previewProjectPurge() {
  if (busy() || selectedRepository_ < 0 || selectedRepository_ >= scanResult_.repositories.size()) return;
  projectPlan_ = {};
  projectPlanMode_ = QStringLiteral("purge");
  const auto repository = scanResult_.repositories[selectedRepository_];
  projectPreviewRepositoryIndex_ = selectedRepository_;
  const auto root = settings_.snapshotRoot;
  const auto database = settings_.databasePath;
  setStatus(tr("Validating full snapshot-store purge for %1…").arg(repository.relativePath));
  emit projectPlanChanged();
  notifyBusy();
  projectPreviewWatcher_.setFuture(QtConcurrent::run([repository, root, database] {
    return SnapshotCleaner().previewPurge(repository, root, database);
  }));
}

void SnapshotController::executeProjectAction() {
  if (busy() || !hasProjectPlan()) return;
  const auto plan = projectPlan_;
  const auto settings = cleanupSettings();
  setStatus(projectPlanMode_ == QStringLiteral("reset")
                ? tr("Resetting selected project snapshot history. Do not start OpenCode…")
                : projectPlanMode_ == QStringLiteral("purge")
                ? tr("Removing the selected snapshot store. Do not start OpenCode…")
                : tr("Cleaning selected project snapshot storage. Do not start OpenCode…"));
  projectPlan_ = {};
  projectPlanMode_.clear();
  emit projectPlanChanged();
  notifyBusy();
  cleanupWatcher_.setFuture(QtConcurrent::run([plan, settings] {
    return SnapshotCleaner().execute(plan, settings);
  }));
}

void SnapshotController::chooseSnapshotRoot() {
  const auto path = QFileDialog::getExistingDirectory(nullptr, tr("Select OpenCode snapshot directory"), settings_.snapshotRoot);
  if (!path.isEmpty()) setSnapshotRoot(path);
}
void SnapshotController::chooseDatabase() {
  const auto path = QFileDialog::getOpenFileName(nullptr, tr("Select OpenCode database"), QFileInfo(settings_.databasePath).path(), tr("SQLite databases (*.db *.sqlite);;All files (*)"));
  if (!path.isEmpty()) setDatabasePath(path);
}

QString SnapshotController::formatBytes(qint64 bytes) const {
  static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(std::max<qint64>(0, bytes));
  int unit = 0;
  while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
  return QStringLiteral("%1 %2").arg(value, 0, unit == 0 ? 'f' : 'f', unit == 0 ? 0 : 2).arg(QString::fromLatin1(units[unit]));
}

void SnapshotController::persistSettings() {
  AppSettingsStore().save(settings_);
  emit settingsChanged();
  cleanupPlan_ = {};
  projectPlan_ = {};
  projectPlanMode_.clear();
  emit planChanged();
  emit projectPlanChanged();
}
void SnapshotController::setStatus(QString value) { if (status_ != value) { status_ = std::move(value); emit statusChanged(); } }
void SnapshotController::notifyBusy() { emit busyChanged(); }
CleanupSettings SnapshotController::cleanupSettings() const { return settings_.cleanup; }
