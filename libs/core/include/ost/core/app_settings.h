#pragma once

#include "ost/core/snapshot_types.h"

namespace ost::core {

struct AppSettings {
  QString snapshotRoot;
  QString databasePath;
  CleanupSettings cleanup;
};

class AppSettingsStore {
 public:
  AppSettings load() const;
  void save(const AppSettings& settings) const;
};

}  // namespace ost::core
