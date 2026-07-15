#pragma once

#include "ost/core/snapshot_types.h"

namespace ost::core {

class RetentionPolicy {
 public:
  static void apply(QVector<RepositoryInfo>& repositories,
                    const RetentionSettings& settings,
                    const QDateTime& now = QDateTime::currentDateTimeUtc());
};

}  // namespace ost::core
