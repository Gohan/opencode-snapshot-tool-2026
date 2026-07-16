#pragma once

#include "ost/core/git_client.h"
#include "ost/core/snapshot_types.h"

#include <functional>

namespace ost::core {

class SnapshotCleaner {
 public:
  using ActivityProbe = std::function<RepositoryActivity(
      const RepositoryCleanupPlan&, const QString& databasePath)>;

  explicit SnapshotCleaner(GitClient git = {}, ActivityProbe activityProbe = {});

  CleanupPlan preview(const ScanResult& scan, const CleanupSettings& settings) const;
  CleanupPlan previewReset(const RepositoryInfo& repository,
                           const CleanupSettings& settings,
                           const QString& databasePath = {}) const;
  CleanupPlan previewPurge(const RepositoryInfo& repository,
                           const QString& snapshotRoot,
                           const QString& databasePath = {}) const;
  CleanupResult execute(const CleanupPlan& plan, const CleanupSettings& settings) const;

 private:
  GitClient git_;
  ActivityProbe activityProbe_;
};

}  // namespace ost::core
