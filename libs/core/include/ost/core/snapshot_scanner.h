#pragma once

#include "ost/core/git_client.h"
#include "ost/core/snapshot_types.h"

#include <functional>

namespace ost::core {

class SnapshotScanner {
 public:
  explicit SnapshotScanner(GitClient git = {});

  ScanResult scan(const QString& snapshotRoot, const QString& databasePath,
                  const RetentionSettings& settings,
                  const std::function<void(const QString&)>& progress = {}) const;

  static QStringList discoverGitDirectories(const QString& snapshotRoot);
  static qint64 directorySize(const QString& path);
  static QString defaultDataDirectory();

 private:
  GitClient git_;
};

}  // namespace ost::core
