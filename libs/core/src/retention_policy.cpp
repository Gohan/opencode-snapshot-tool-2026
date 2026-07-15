#include "ost/core/retention_policy.h"

#include <QHash>

#include <algorithm>

namespace ost::core {
void RetentionPolicy::apply(QVector<RepositoryInfo>& repositories,
                            const RetentionSettings& settings,
                            const QDateTime& now) {
  const auto cutoff = now.toUTC().addDays(-std::max(0, settings.recentDays));
  for (auto& repository : repositories) {
    QHash<QString, QDateTime> latest;
    for (const auto& snapshot : repository.snapshots) {
      if (snapshot.source == SnapshotSource::CurrentIndex) continue;
      const auto hit = latest.constFind(snapshot.hash);
      if (hit == latest.cend() || hit.value() < snapshot.lastSeen) latest[snapshot.hash] = snapshot.lastSeen;
    }

    QVector<QPair<QString, QDateTime>> ordered;
    ordered.reserve(latest.size());
    for (auto it = latest.cbegin(); it != latest.cend(); ++it) ordered.push_back({it.key(), it.value()});
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
      if (left.second == right.second) return left.first > right.first;
      return left.second > right.second;
    });

    QSet<QString> selected;
    for (const auto& item : ordered) {
      if (item.second.toUTC() >= cutoff) selected.insert(item.first);
    }
    const bool hasRecent = !selected.isEmpty();
    if (!hasRecent) {
      const int count = std::min(std::max(1, settings.fallbackCount),
                                 static_cast<int>(ordered.size()));
      for (int index = 0; index < count; ++index) selected.insert(ordered[index].first);
    }

    for (auto& snapshot : repository.snapshots) {
      if (snapshot.source == SnapshotSource::CurrentIndex) {
        snapshot.keep = true;
        snapshot.keepReason = QStringLiteral("Current Git index");
      } else if (selected.contains(snapshot.hash)) {
        snapshot.keep = true;
        snapshot.keepReason = hasRecent
                                  ? QStringLiteral("Last %1 days").arg(settings.recentDays)
                                  : QStringLiteral("Fallback newest %1").arg(settings.fallbackCount);
      } else {
        snapshot.keep = false;
        snapshot.keepReason.clear();
      }
    }
  }
}
}  // namespace ost::core
