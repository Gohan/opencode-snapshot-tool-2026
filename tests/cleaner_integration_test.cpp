#include "ost/core/git_client.h"
#include "ost/core/snapshot_cleaner.h"

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

TEST(SnapshotCleaner, PreviewDoesNotWriteAndExecuteKeepsOnlyPlannedTrees) {
  GitClient git;
  if (!git.available()) GTEST_SKIP() << "git is required";
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto gitDir = QDir(temp.path()).filePath("snapshot.git");
  ASSERT_TRUE(git.run(gitDir, {"init", "--bare"}).ok());
  const auto keep = writeTree(git, gitDir, "keep");
  const auto remove = writeTree(git, gitDir, "remove");

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
  ScanResult scan;
  scan.repositories = {repo};
  SnapshotCleaner cleaner(git);

  const auto plan = cleaner.preview(scan, CleanupSettings{});
  EXPECT_FALSE(QDir(gitDir).exists("refs/opencode-snapshot-tool"));
  ASSERT_EQ(plan.keepTrees, 1);
  ASSERT_EQ(plan.removeTrees, 1);

  // Simulate OpenCode writing a newer index after the preview was produced.
  // Execution must protect this tree even though it is absent from the plan.
  const auto currentAtExecution = writeTree(git, gitDir, "current at execution");
  ASSERT_TRUE(git.run(gitDir, {"read-tree", currentAtExecution}).ok());

  auto settings = CleanupSettings{};
  settings.recentDays = 0;
  settings.fullGc = true;
  settings.pruneLfs = false;
  const auto result = cleaner.execute(plan, settings);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(git.run(gitDir, {"cat-file", "-e", keep + "^{tree}"}).ok());
  EXPECT_TRUE(git.run(gitDir, {"cat-file", "-e", currentAtExecution + "^{tree}"}).ok());
  EXPECT_TRUE(git.run(gitDir, {"show-ref", "--verify",
                               "refs/opencode-snapshot-tool/keep/" + currentAtExecution}).ok());
  EXPECT_FALSE(git.run(gitDir, {"cat-file", "-e", remove}).ok());
}
}  // namespace
