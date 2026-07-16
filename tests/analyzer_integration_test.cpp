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

using FileFixture = QPair<QString, QByteArray>;

QString writeTree(const GitClient& git, const QString& gitDir,
                  const QVector<FileFixture>& files) {
  EXPECT_TRUE(git.run(gitDir, {"read-tree", "--empty"}).ok());
  for (const auto& [path, value] : files) {
    const auto blob = git.run(gitDir, {"hash-object", "-w", "--stdin"}, value);
    EXPECT_TRUE(blob.ok()) << blob.error.constData();
    const auto cached = QStringLiteral("100644,%1,%2")
                            .arg(QString::fromLatin1(blob.output.trimmed()), path);
    const auto updated = git.run(gitDir, {"update-index", "--add", "--cacheinfo", cached});
    EXPECT_TRUE(updated.ok()) << updated.error.constData();
  }
  const auto tree = git.run(gitDir, {"write-tree"});
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

  const QByteArray shared(256 * 1024, 's');
  const auto current = writeTree(git, gitDir,
                                 {{"assets/shared.PNG", shared},
                                  {"src/current.cpp", QByteArray(512 * 1024, 'c')},
                                  {"README", QByteArray(64 * 1024, 'r')}});
  const auto history = writeTree(git, gitDir,
                                 {{"assets/shared.PNG", shared},
                                  {"docs/history.md", QByteArray(768 * 1024, 'h')}});
  const auto unprotected = writeTree(git, gitDir,
                                     {{"archive/old.zip", QByteArray(1024 * 1024, 'z')}});
  ASSERT_TRUE(git.run(gitDir, {"update-ref", "refs/test/current", current}).ok());
  ASSERT_TRUE(git.run(gitDir, {"update-ref", "refs/test/history", history}).ok());
  ASSERT_TRUE(git.run(gitDir, {"update-ref", "refs/test/unprotected", unprotected}).ok());
  ASSERT_TRUE(git.run(gitDir, {"repack", "-ad"}).ok());
  ASSERT_TRUE(git.run(gitDir, {"update-ref", "-d", "refs/test/unprotected"}).ok());
  ASSERT_TRUE(git.run(gitDir, {"read-tree", current}).ok());

  RepositoryInfo repository;
  repository.gitDir = gitDir;
  repository.gitObjectBytes = SnapshotScanner::directorySize(QDir(gitDir).filePath("objects"));
  SnapshotInfo currentSnapshot;
  currentSnapshot.hash = current;
  currentSnapshot.keep = true;
  SnapshotInfo historySnapshot;
  historySnapshot.hash = history;
  historySnapshot.keep = true;
  SnapshotInfo unprotectedSnapshot;
  unprotectedSnapshot.hash = unprotected;
  repository.snapshots = {currentSnapshot, historySnapshot, unprotectedSnapshot};
  const auto result = SnapshotAnalyzer(git).analyze(repository, {current, history});

  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_EQ(result.currentTree, current);
  EXPECT_GT(result.localObjects, result.currentObjects);
  EXPECT_GT(result.retainedObjects, result.currentObjects);
  EXPECT_GT(result.currentReachableBytes, 0);
  EXPECT_GT(result.currentExclusiveBytes, 0);
  EXPECT_GT(result.currentSharedBytes, 0);
  EXPECT_GT(result.historyOnlyBytes, 0);
  EXPECT_GT(result.estimatedReclaimableBytes, 0);
  ASSERT_FALSE(result.topPaths.isEmpty());
  EXPECT_EQ(result.topPaths.front().path, QStringLiteral("src"));
  EXPECT_TRUE(std::any_of(result.pathEntries.cbegin(), result.pathEntries.cend(),
                          [](const auto& entry) {
                            return entry.path == QStringLiteral("assets") && entry.directory &&
                                   entry.objects == 1;
                          }));
  EXPECT_TRUE(std::any_of(result.pathEntries.cbegin(), result.pathEntries.cend(),
                          [](const auto& entry) {
                            return entry.path == QStringLiteral("assets/shared.PNG") &&
                                   !entry.directory;
                          }));
  EXPECT_TRUE(std::any_of(result.fileTypes.cbegin(), result.fileTypes.cend(),
                          [](const auto& type) {
                            return type.suffix == QStringLiteral(".png") && type.files == 1;
                          }));
  EXPECT_TRUE(std::any_of(result.fileTypes.cbegin(), result.fileTypes.cend(),
                          [](const auto& type) {
                            return type.suffix == QStringLiteral("(no extension)") && type.files == 1;
                          }));
  ASSERT_GE(result.largestObjects.size(), 2);
  EXPECT_TRUE(std::any_of(result.largestObjects.cbegin(), result.largestObjects.cend(),
                          [](const auto& object) {
                            return object.path == QStringLiteral("archive/old.zip") &&
                                   !object.current && !object.retained;
                          }));
  ASSERT_FALSE(result.packs.isEmpty());
  EXPECT_GT(result.packs.front().bytes, 0);
}
}  // namespace
