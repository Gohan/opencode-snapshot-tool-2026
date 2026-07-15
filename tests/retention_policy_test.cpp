#include "ost/core/retention_policy.h"

#include <gtest/gtest.h>

namespace {
using ost::core::RepositoryInfo;
using ost::core::RetentionPolicy;
using ost::core::RetentionSettings;
using ost::core::SnapshotInfo;
using ost::core::SnapshotSource;

SnapshotInfo makeSnapshot(const QString& hash, const QDateTime& now, int ageDays,
                          SnapshotSource source = SnapshotSource::Database) {
  SnapshotInfo result;
  result.hash = hash;
  result.lastSeen = now.addDays(-ageDays);
  result.firstSeen = result.lastSeen;
  result.exists = true;
  result.source = source;
  return result;
}

TEST(RetentionPolicy, KeepsEveryTreeWithinRecentWindow) {
  const auto now = QDateTime::fromString("2026-07-15T10:00:00Z", Qt::ISODate);
  RepositoryInfo repo;
  repo.snapshots = {makeSnapshot("a", now, 0), makeSnapshot("b", now, 3),
                    makeSnapshot("c", now, 7), makeSnapshot("d", now, 8)};
  QVector<RepositoryInfo> repos{repo};

  RetentionPolicy::apply(repos, RetentionSettings{7, 10}, now);

  EXPECT_TRUE(repos[0].snapshots[0].keep);
  EXPECT_TRUE(repos[0].snapshots[1].keep);
  EXPECT_TRUE(repos[0].snapshots[2].keep);
  EXPECT_FALSE(repos[0].snapshots[3].keep);
}

TEST(RetentionPolicy, FallsBackToNewestTenPerRepository) {
  const auto now = QDateTime::fromString("2026-07-15T10:00:00Z", Qt::ISODate);
  RepositoryInfo first;
  RepositoryInfo second;
  for (int i = 0; i < 14; ++i) first.snapshots.push_back(makeSnapshot(QString::number(i), now, 20 + i));
  for (int i = 0; i < 3; ++i) second.snapshots.push_back(makeSnapshot(QString("b%1").arg(i), now, 40 + i));
  QVector<RepositoryInfo> repos{first, second};

  RetentionPolicy::apply(repos, RetentionSettings{7, 10}, now);

  EXPECT_EQ(std::count_if(repos[0].snapshots.cbegin(), repos[0].snapshots.cend(),
                          [](const auto& item) { return item.keep; }), 10);
  EXPECT_EQ(std::count_if(repos[1].snapshots.cbegin(), repos[1].snapshots.cend(),
                          [](const auto& item) { return item.keep; }), 3);
}

TEST(RetentionPolicy, AlwaysKeepsCurrentIndexTreeWithoutConsumingFallbackQuota) {
  const auto now = QDateTime::fromString("2026-07-15T10:00:00Z", Qt::ISODate);
  RepositoryInfo repo;
  for (int i = 0; i < 12; ++i) repo.snapshots.push_back(makeSnapshot(QString::number(i), now, 20 + i));
  repo.snapshots.push_back(makeSnapshot("index", now, 0, SnapshotSource::CurrentIndex));
  QVector<RepositoryInfo> repos{repo};

  RetentionPolicy::apply(repos, RetentionSettings{7, 10}, now);

  EXPECT_EQ(std::count_if(repos[0].snapshots.cbegin(), repos[0].snapshots.cend(),
                          [](const auto& item) { return item.keep; }), 11);
  EXPECT_EQ(repos[0].snapshots.back().keepReason, QStringLiteral("Current Git index"));
}
}  // namespace
