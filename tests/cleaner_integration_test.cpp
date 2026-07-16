#include "ost/core/git_client.h"
#include "ost/core/snapshot_cleaner.h"
#include "ost/core/snapshot_scanner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
using ost::core::CleanupSettings;
using ost::core::GitClient;
using ost::core::RepositoryInfo;
using ost::core::ScanResult;
using ost::core::SnapshotCleaner;
using ost::core::SnapshotInfo;

QString writeTree(const GitClient& git, const QString& gitDir, const QByteArray& value) {
  const auto blob = git.run(gitDir, {"hash-object", "-w", "--stdin"}, value);
  EXPECT_TRUE(blob.ok()) << blob.error.constData();
  const QByteArray row = "100644 blob " + blob.output.trimmed() + "\tfile.txt\n";
  const auto tree = git.run(gitDir, {"mktree"}, row);
  EXPECT_TRUE(tree.ok()) << tree.error.constData();
  return QString::fromLatin1(tree.output.trimmed());
}

QByteArray deterministicNoise(qsizetype size) {
  QByteArray result(size, Qt::Uninitialized);
  quint32 state = 0x6d2b79f5U;
  for (qsizetype i = 0; i < size; ++i) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    result[i] = static_cast<char>(state & 0xffU);
  }
  return result;
}

TEST(SnapshotCleaner, PreviewDoesNotWriteAndExecuteKeepsOnlyPlannedTrees) {
  GitClient git;
  if (!git.available()) GTEST_SKIP() << "git is required";
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto gitDir = QDir(temp.path()).filePath("snapshot.git");
  ASSERT_TRUE(git.run(gitDir, {"init", "--bare"}).ok());
  const auto keep = writeTree(git, gitDir, "keep");
  const auto remove = writeTree(git, gitDir, deterministicNoise(4 * 1024 * 1024));

  RepositoryInfo repo;
  repo.gitDir = gitDir;
  repo.relativePath = "project/worktree";
  SnapshotInfo kept;
  kept.hash = keep;
  kept.keep = true;
  kept.exists = true;
  SnapshotInfo dropped;
  dropped.hash = remove;
  dropped.exists = true;
  repo.snapshots = {kept, dropped};
  repo.actualBytes = ost::core::SnapshotScanner::directorySize(gitDir);
  ScanResult scan;
  scan.repositories = {repo};
  scan.totalBytes = repo.actualBytes;
  SnapshotCleaner cleaner(git);

  const auto plan = cleaner.preview(scan, CleanupSettings{});
  EXPECT_FALSE(QDir(gitDir).exists("refs/opencode-snapshot-tool"));
  ASSERT_EQ(plan.keepTrees, 1);
  ASSERT_EQ(plan.removeTrees, 1);

  // Simulate OpenCode writing a newer index after the preview was produced.
  // Execution must protect this tree even though it is absent from the plan.
  const auto currentAtExecution = writeTree(git, gitDir, "current at execution");
  ASSERT_TRUE(git.run(gitDir, {"read-tree", currentAtExecution}).ok());
  const auto bytesBeforeExecution = ost::core::SnapshotScanner::directorySize(gitDir);

  auto settings = CleanupSettings{};
  // The retention window selects trees during preview. Once a tree has been
  // reviewed for release, cleanup must not add another seven-day Git grace period.
  settings.recentDays = 7;
  settings.fullGc = true;
  settings.pruneLfs = false;
  const auto result = cleaner.execute(plan, settings);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(git.run(gitDir, {"cat-file", "-e", keep + "^{tree}"}).ok());
  EXPECT_TRUE(git.run(gitDir, {"cat-file", "-e", currentAtExecution + "^{tree}"}).ok());
  EXPECT_TRUE(git.run(gitDir, {"show-ref", "--verify",
                               "refs/opencode-snapshot-tool/keep/" + currentAtExecution}).ok());
  EXPECT_FALSE(git.run(gitDir, {"cat-file", "-e", remove}).ok());
  EXPECT_LT(result.bytesAfter, bytesBeforeExecution - 1024 * 1024);
}

TEST(SnapshotCleaner, ResetPreviewKeepsCurrentStateAndAllowsFutureSnapshots) {
  GitClient git;
  if (!git.available()) GTEST_SKIP() << "git is required";
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto gitDir = QDir(temp.path()).filePath("snapshot.git");
  ASSERT_TRUE(git.run(gitDir, {"init", "--bare"}).ok());
  ASSERT_TRUE(git.run(gitDir, {"config", "snapshot.fixture", "preserved"}).ok());
  const auto current = writeTree(git, gitDir, "current");
  const auto history = writeTree(git, gitDir, deterministicNoise(2 * 1024 * 1024));
  ASSERT_TRUE(git.run(gitDir, {"read-tree", current}).ok());

  RepositoryInfo repository;
  repository.gitDir = gitDir;
  repository.relativePath = "project/worktree";
  repository.actualBytes = ost::core::SnapshotScanner::directorySize(gitDir);
  SnapshotInfo old;
  old.hash = history;
  old.keep = true;
  repository.snapshots = {old};

  SnapshotCleaner cleaner(git);
  auto settings = CleanupSettings{};
  settings.fullGc = true;
  settings.pruneLfs = false;
  const auto plan = cleaner.previewReset(repository, settings);

  ASSERT_TRUE(plan.resetHistory);
  ASSERT_EQ(plan.repositories.size(), 1);
  EXPECT_EQ(plan.repositories.front().keepHashes, QVector<QString>{current});
  EXPECT_TRUE(plan.repositories.front().removeHashes.contains(history));

  const auto lockPath = QDir(gitDir).filePath(QStringLiteral("index.lock"));
  QFile lock(lockPath);
  ASSERT_TRUE(lock.open(QIODevice::WriteOnly));
  ASSERT_GT(lock.write("active"), 0);
  lock.close();
  const auto blocked = cleaner.execute(plan, settings);
  EXPECT_FALSE(blocked.success);
  EXPECT_TRUE(blocked.error.contains(QStringLiteral("lock"), Qt::CaseInsensitive));
  ASSERT_TRUE(QFile::remove(lockPath));

  const auto result = cleaner.execute(plan, settings);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(git.run(gitDir, {"cat-file", "-e", current + "^{tree}"}).ok());
  EXPECT_FALSE(git.run(gitDir, {"cat-file", "-e", history}).ok());
  EXPECT_EQ(QString::fromUtf8(git.run(gitDir, {"config", "--get", "snapshot.fixture"})
                                  .output.trimmed()),
            QStringLiteral("preserved"));

  const auto future = writeTree(git, gitDir, "future");
  ASSERT_TRUE(git.run(gitDir, {"read-tree", future}).ok());
  EXPECT_EQ(QString::fromLatin1(git.run(gitDir, {"write-tree"}).output.trimmed()), future);
}

TEST(SnapshotCleaner, FullStorePurgeIsRootBoundedAndOpenCodeCanReinitialize) {
  GitClient git;
  if (!git.available()) GTEST_SKIP() << "git is required";
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto snapshotRoot = QDir(temp.path()).filePath("snapshot");
  ASSERT_TRUE(QDir().mkpath(snapshotRoot));
  const auto gitDir = QDir(snapshotRoot).filePath("project/worktree");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(gitDir).absolutePath()));
  ASSERT_TRUE(git.run(gitDir, {"init", "--bare"}).ok());
  const auto current = writeTree(git, gitDir, deterministicNoise(2 * 1024 * 1024));
  ASSERT_TRUE(git.run(gitDir, {"read-tree", current}).ok());

  RepositoryInfo repository;
  repository.gitDir = gitDir;
  repository.relativePath = "project/worktree";
  repository.actualBytes = ost::core::SnapshotScanner::directorySize(gitDir);
  SnapshotInfo snapshot;
  snapshot.hash = current;
  snapshot.keep = true;
  repository.snapshots = {snapshot};

  SnapshotCleaner cleaner(git);
  const auto otherRoot = QDir(temp.path()).filePath("other-root");
  ASSERT_TRUE(QDir().mkpath(otherRoot));
  const auto outside = cleaner.previewPurge(repository, otherRoot);
  EXPECT_FALSE(outside.error.isEmpty());
  EXPECT_TRUE(outside.repositories.isEmpty());

  const auto plan = cleaner.previewPurge(repository, snapshotRoot);
  ASSERT_TRUE(plan.error.isEmpty()) << plan.error.toStdString();
  ASSERT_TRUE(plan.purgeStore);
  ASSERT_EQ(plan.repositories.size(), 1);
  EXPECT_EQ(plan.estimatedReclaimableBytes, repository.actualBytes);

  const auto lockPath = QDir(gitDir).filePath(QStringLiteral("index.lock"));
  QFile lock(lockPath);
  ASSERT_TRUE(lock.open(QIODevice::WriteOnly));
  ASSERT_GT(lock.write("active"), 0);
  lock.close();
  const auto blocked = cleaner.execute(plan, CleanupSettings{});
  EXPECT_FALSE(blocked.success);
  EXPECT_TRUE(blocked.error.contains(QStringLiteral("lock"), Qt::CaseInsensitive));
  ASSERT_TRUE(QFile::remove(lockPath));

  const auto result = cleaner.execute(plan, CleanupSettings{});
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_FALSE(QFileInfo::exists(gitDir));
  EXPECT_GE(result.bytesBefore - result.bytesAfter, 1024 * 1024);

  // OpenCode follows the same recovery shape: a missing snapshot gitdir is
  // initialized again before the next index/tree is written.
  ASSERT_TRUE(QDir().mkpath(QFileInfo(gitDir).absolutePath()));
  ASSERT_TRUE(git.run(gitDir, {"init", "--bare"}).ok());
  const auto future = writeTree(git, gitDir, "future after purge");
  ASSERT_TRUE(git.run(gitDir, {"read-tree", future}).ok());
  EXPECT_EQ(QString::fromLatin1(git.run(gitDir, {"write-tree"}).output.trimmed()), future);
}
}  // namespace
