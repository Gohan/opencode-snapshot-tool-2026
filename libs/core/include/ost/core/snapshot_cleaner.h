#pragma once

#include "ost/core/git_client.h"
#include "ost/core/snapshot_types.h"

namespace ost::core {

class SnapshotCleaner {
 public:
  explicit SnapshotCleaner(GitClient git = {});

  CleanupPlan preview(const ScanResult& scan, const CleanupSettings& settings) const;
  CleanupResult execute(const CleanupPlan& plan, const CleanupSettings& settings) const;

 private:
  GitClient git_;
};

}  // namespace ost::core
