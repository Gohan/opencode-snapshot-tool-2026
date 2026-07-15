#pragma once

#include "ost/core/app_settings.h"
#include "ost/core/snapshot_cleaner.h"
#include "ost/core/snapshot_scanner.h"

#include <QFutureWatcher>
#include <QObject>
#include <QVariantList>

class SnapshotController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString snapshotRoot READ snapshotRoot WRITE setSnapshotRoot NOTIFY settingsChanged)
  Q_PROPERTY(QString databasePath READ databasePath WRITE setDatabasePath NOTIFY settingsChanged)
  Q_PROPERTY(int recentDays READ recentDays WRITE setRecentDays NOTIFY settingsChanged)
  Q_PROPERTY(int fallbackCount READ fallbackCount WRITE setFallbackCount NOTIFY settingsChanged)
  Q_PROPERTY(bool fullGc READ fullGc WRITE setFullGc NOTIFY settingsChanged)
  Q_PROPERTY(bool pruneLfs READ pruneLfs WRITE setPruneLfs NOTIFY settingsChanged)
  Q_PROPERTY(int staleFileHours READ staleFileHours WRITE setStaleFileHours NOTIFY settingsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QVariantList repositories READ repositories NOTIFY dataChanged)
  Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY dataChanged)
  Q_PROPERTY(int selectedRepository READ selectedRepository WRITE setSelectedRepository NOTIFY dataChanged)
  Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY dataChanged)
  Q_PROPERTY(int repositoryCount READ repositoryCount NOTIFY dataChanged)
  Q_PROPERTY(int snapshotCount READ snapshotCount NOTIFY dataChanged)
  Q_PROPERTY(int keepCount READ keepCount NOTIFY dataChanged)
  Q_PROPERTY(int dropCount READ dropCount NOTIFY dataChanged)
  Q_PROPERTY(qint64 estimatedReclaimableBytes READ estimatedReclaimableBytes NOTIFY planChanged)
  Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY planChanged)
  Q_PROPERTY(int planKeepTrees READ planKeepTrees NOTIFY planChanged)
  Q_PROPERTY(int planRemoveTrees READ planRemoveTrees NOTIFY planChanged)

 public:
  explicit SnapshotController(QObject* parent = nullptr);

  QString snapshotRoot() const;
  QString databasePath() const;
  int recentDays() const;
  int fallbackCount() const;
  bool fullGc() const;
  bool pruneLfs() const;
  int staleFileHours() const;
  bool busy() const;
  QString status() const;
  QVariantList repositories() const;
  QVariantList snapshots() const;
  int selectedRepository() const;
  qint64 totalBytes() const;
  int repositoryCount() const;
  int snapshotCount() const;
  int keepCount() const;
  int dropCount() const;
  qint64 estimatedReclaimableBytes() const;
  bool hasPlan() const;
  int planKeepTrees() const;
  int planRemoveTrees() const;

  void setSnapshotRoot(const QString& value);
  void setDatabasePath(const QString& value);
  void setRecentDays(int value);
  void setFallbackCount(int value);
  void setFullGc(bool value);
  void setPruneLfs(bool value);
  void setStaleFileHours(int value);
  void setSelectedRepository(int value);

  Q_INVOKABLE void scan();
  Q_INVOKABLE void previewCleanup();
  Q_INVOKABLE void executeCleanup();
  Q_INVOKABLE void chooseSnapshotRoot();
  Q_INVOKABLE void chooseDatabase();
  Q_INVOKABLE QString formatBytes(qint64 bytes) const;

 signals:
  void settingsChanged();
  void busyChanged();
  void statusChanged();
  void dataChanged();
  void planChanged();

 private:
  void persistSettings();
  void setStatus(QString value);
  void notifyBusy();
  ost::core::CleanupSettings cleanupSettings() const;

  ost::core::AppSettings settings_;
  ost::core::ScanResult scanResult_;
  ost::core::CleanupPlan cleanupPlan_;
  int selectedRepository_ = -1;
  QString status_;
  QFutureWatcher<ost::core::ScanResult> scanWatcher_;
  QFutureWatcher<ost::core::CleanupPlan> previewWatcher_;
  QFutureWatcher<ost::core::CleanupResult> cleanupWatcher_;
};
