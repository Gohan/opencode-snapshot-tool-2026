#include "ost/core/git_client.h"
#include "ost/core/snapshot_analyzer.h"
#include "ost/core/snapshot_scanner.h"

#include <QDir>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>

namespace {
using ost::core::GitClient;
using ost::core::RepositoryInfo;
using ost::core::SnapshotAnalyzer;
using ost::core::SnapshotScanner;
using ost::core::SnapshotInfo;

QString writeTree(const GitClient& git, const QString& gitDir, const QString& path,
                  const QByteArray& value) {
  const auto blob = git.run(gitDir, {"hash-object", "-w", "--stdin"}, value);
  EXPECT_TRUE(blob.ok()) << blob.error.constData();
  const auto tree = git.run(gitDir, {"mktree"},
                            "100644 blob " + blob.output.trimmed() + "\t" + path.toUtf8() + "\n");
  EXPECT_TRUE(tree.ok()) << tree.error.constData();
  return QString::fromLatin1(tree.output.trimmed());
}

TEST(SnapshotAnalyzer, SeparatesCurrentReachableAndHistoryOnlyPackedObjects) {
  GitClient git;
  if (!git.available()) GTEST_SKIP() << "git is required";
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto gitDir = QDir(temp.path()).filePath("snapshot.git");
  ASSERT_TRUE(git.run(gitDir, {"init", "--bare"}).ok());

  const auto current = writeTree(git, gitDir, "current.bin", QByteArray(512 * 1024, 'c'));
  const auto history = writeTree(git, gitDir, "history.bin", QByteArray(1024 * 1024, 'h'));
  ASSERT_TRUE(git.run(gitDir, {"update-ref", "refs/test/current", current}).ok());
  ASSERT_TRUE(git.run(gitDir, {"update-ref", "refs/test/history", history}).ok());
  ASSERT_TRUE(git.run(gitDir, {"repack", "-ad"}).ok());
  ASSERT_TRUE(git.run(gitDir, {"update-ref", "-d", "refs/test/history"}).ok());
  ASSERT_TRUE(git.run(gitDir, {"read-tree", current}).ok());

  RepositoryInfo repository;
  repository.gitDir = gitDir;
  repository.gitObjectBytes = SnapshotScanner::directorySize(QDir(gitDir).filePath("objects"));
  SnapshotInfo currentSnapshot;
  currentSnapshot.hash = current;
  currentSnapshot.keep = true;
  SnapshotInfo historySnapshot;
  historySnapshot.hash = history;
  repository.snapshots = {currentSnapshot, historySnapshot};
  const auto result = SnapshotAnalyzer(git).analyze(repository, {current});

  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_EQ(result.currentTree, current);
  EXPECT_GT(result.localObjects, result.currentObjects);
  EXPECT_EQ(result.currentObjects, result.retainedObjects);
  EXPECT_GT(result.currentReachableBytes, 0);
  EXPECT_GT(result.estimatedReclaimableBytes, 0);
  ASSERT_FALSE(result.topPaths.isEmpty());
  EXPECT_EQ(result.topPaths.front().path, QStringLiteral("(root files)"));
  ASSERT_GE(result.largestObjects.size(), 2);
  EXPECT_TRUE(std::any_of(result.largestObjects.cbegin(), result.largestObjects.cend(),
                          [](const auto& object) {
                            return object.path == QStringLiteral("history.bin") && !object.current;
                          }));
  ASSERT_FALSE(result.packs.isEmpty());
  EXPECT_GT(result.packs.front().bytes, 0);
}
}  // namespace
