#include "snapshot_controller.h"

#include "ost/core/retention_policy.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>

using namespace ost::core;

SnapshotController::SnapshotController(QObject* parent) : QObject(parent), settings_(AppSettingsStore().load()) {
  connect(&scanWatcher_, &QFutureWatcher<ScanResult>::finished, this, [this] {
    scanResult_ = scanWatcher_.result();
    selectedRepository_ = -1;
    for (int index = 0; index < scanResult_.repositories.size(); ++index)
      if (selectedRepository_ < 0 || scanResult_.repositories[index].actualBytes >
                                         scanResult_.repositories[selectedRepository_].actualBytes)
        selectedRepository_ = index;
    cleanupPlan_ = {};
    auto message = tr("Found %1 repositories and %2 snapshot records")
                       .arg(repositoryCount()).arg(snapshotCount());
    if (!scanResult_.warnings.isEmpty())
      message += tr(" — Warning: %1").arg(scanResult_.warnings.join(QStringLiteral(" · ")));
    setStatus(std::move(message));
    emit dataChanged();
    emit planChanged();
    notifyBusy();
  });
  connect(&previewWatcher_, &QFutureWatcher<CleanupPlan>::finished, this, [this] {
    cleanupPlan_ = previewWatcher_.result();
    setStatus(tr("Preview ready: %1 trees can be released; %2 in directly removable files. Git pack savings are measured after cleanup")
                  .arg(cleanupPlan_.removeTrees).arg(formatBytes(cleanupPlan_.estimatedReclaimableBytes)));
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
  setStatus(tr("Ready. Scan is read-only; cleanup always requires a preview and confirmation."));
}

QString SnapshotController::snapshotRoot() const { return settings_.snapshotRoot; }
QString SnapshotController::databasePath() const { return settings_.databasePath; }
int SnapshotController::recentDays() const { return settings_.cleanup.recentDays; }
int SnapshotController::fallbackCount() const { return settings_.cleanup.fallbackCount; }
bool SnapshotController::fullGc() const { return settings_.cleanup.fullGc; }
bool SnapshotController::pruneLfs() const { return settings_.cleanup.pruneLfs; }
int SnapshotController::staleFileHours() const { return settings_.cleanup.staleFileHours; }
bool SnapshotController::busy() const { return scanWatcher_.isRunning() || previewWatcher_.isRunning() || cleanupWatcher_.isRunning(); }
QString SnapshotController::status() const { return status_; }
int SnapshotController::selectedRepository() const { return selectedRepository_; }
qint64 SnapshotController::totalBytes() const { return scanResult_.totalBytes; }
int SnapshotController::repositoryCount() const { return scanResult_.repositories.size(); }
qint64 SnapshotController::estimatedReclaimableBytes() const { return cleanupPlan_.estimatedReclaimableBytes; }
bool SnapshotController::hasPlan() const { return !cleanupPlan_.repositories.isEmpty(); }
int SnapshotController::planKeepTrees() const { return cleanupPlan_.keepTrees; }
int SnapshotController::planRemoveTrees() const { return cleanupPlan_.removeTrees; }

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
                               {"kept", kept}, {"dropped", repo.snapshots.size() - kept}});
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

  return {{"valid", true}, {"name", repo.relativePath}, {"worktree", repo.worktree},
          {"gitDir", repo.gitDir}, {"totalBytes", repo.actualBytes},
          {"totalBytesText", formatBytes(repo.actualBytes)}, {"categories", categories},
          {"looseObjects", repo.looseObjects}, {"packedObjects", repo.packedObjects},
          {"largestFiles", largestFiles}, {"duplicateWorktrees", duplicateWorktrees},
          {"combinedWorktreeBytesText", formatBytes(combinedWorktreeBytes)},
          {"explanation", explanation}};
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
void SnapshotController::setSelectedRepository(int value) { if (value != selectedRepository_) { selectedRepository_ = value; emit dataChanged(); } }

void SnapshotController::scan() {
  if (busy()) return;
  persistSettings();
  setStatus(tr("Scanning snapshot storage…"));
  notifyBusy();
  const auto root = settings_.snapshotRoot;
  const auto database = settings_.databasePath;
  const RetentionSettings retention{settings_.cleanup.recentDays, settings_.cleanup.fallbackCount};
  const QPointer<SnapshotController> self(this);
  scanWatcher_.setFuture(QtConcurrent::run([root, database, retention, self] {
    return SnapshotScanner().scan(root, database, retention, [self](const QString& message) {
      if (!self) return;
      QMetaObject::invokeMethod(self, [self, message] {
        if (self) self->setStatus(message + QStringLiteral("…"));
      }, Qt::QueuedConnection);
    });
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

void SnapshotController::persistSettings() { AppSettingsStore().save(settings_); emit settingsChanged(); cleanupPlan_ = {}; emit planChanged(); }
void SnapshotController::setStatus(QString value) { if (status_ != value) { status_ = std::move(value); emit statusChanged(); } }
void SnapshotController::notifyBusy() { emit busyChanged(); }
CleanupSettings SnapshotController::cleanupSettings() const { return settings_.cleanup; }
