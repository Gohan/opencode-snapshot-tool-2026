#include "ost/core/app_settings.h"

#include "ost/core/snapshot_scanner.h"

#include <QDir>
#include <QSettings>

namespace ost::core {
AppSettings AppSettingsStore::load() const {
  QSettings store;
  AppSettings result;
  const auto data = SnapshotScanner::defaultDataDirectory();
  result.snapshotRoot = store.value(QStringLiteral("paths/snapshotRoot"),
                                    QDir(data).filePath(QStringLiteral("snapshot"))).toString();
  result.databasePath = store.value(QStringLiteral("paths/database"),
                                    QDir(data).filePath(QStringLiteral("opencode.db"))).toString();
  result.cleanup.recentDays = store.value(QStringLiteral("cleanup/recentDays"), 7).toInt();
  result.cleanup.fallbackCount = store.value(QStringLiteral("cleanup/fallbackCount"), 10).toInt();
  result.cleanup.fullGc = store.value(QStringLiteral("cleanup/fullGc"), true).toBool();
  result.cleanup.pruneLfs = store.value(QStringLiteral("cleanup/pruneLfs"), true).toBool();
  result.cleanup.staleFileHours = store.value(QStringLiteral("cleanup/staleFileHours"), 24).toInt();
  return result;
}

void AppSettingsStore::save(const AppSettings& settings) const {
  QSettings store;
  store.setValue(QStringLiteral("paths/snapshotRoot"), settings.snapshotRoot);
  store.setValue(QStringLiteral("paths/database"), settings.databasePath);
  store.setValue(QStringLiteral("cleanup/recentDays"), settings.cleanup.recentDays);
  store.setValue(QStringLiteral("cleanup/fallbackCount"), settings.cleanup.fallbackCount);
  store.setValue(QStringLiteral("cleanup/fullGc"), settings.cleanup.fullGc);
  store.setValue(QStringLiteral("cleanup/pruneLfs"), settings.cleanup.pruneLfs);
  store.setValue(QStringLiteral("cleanup/staleFileHours"), settings.cleanup.staleFileHours);
}
}  // namespace ost::core
