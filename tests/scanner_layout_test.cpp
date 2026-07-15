#include "ost/core/snapshot_scanner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
void touch(const QString& path, const QByteArray& data = {}) {
  QDir().mkpath(QFileInfo(path).path());
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(data);
}

void makeGitDir(const QString& path) {
  touch(QDir(path).filePath("HEAD"), "ref: refs/heads/master\n");
  QDir().mkpath(QDir(path).filePath("objects"));
}

TEST(SnapshotScanner, DiscoversLegacyAndTwoLevelGitDirectoriesWithoutEnteringObjects) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  makeGitDir(QDir(temp.path()).filePath("legacy"));
  makeGitDir(QDir(temp.path()).filePath("project/worktree"));
  makeGitDir(QDir(temp.path()).filePath("project/worktree/objects/not-a-repo"));

  const auto found = ost::core::SnapshotScanner::discoverGitDirectories(temp.path());

  ASSERT_EQ(found.size(), 2);
  EXPECT_TRUE(found.contains(QDir(temp.path()).filePath("legacy")));
  EXPECT_TRUE(found.contains(QDir(temp.path()).filePath("project/worktree")));
}

TEST(SnapshotScanner, DirectorySizeIncludesLfsAndTemporaryPackFiles) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  touch(QDir(temp.path()).filePath("objects/pack/a.pack"), QByteArray(11, 'a'));
  touch(QDir(temp.path()).filePath("lfs/objects/aa/bb/oid"), QByteArray(17, 'b'));
  touch(QDir(temp.path()).filePath("objects/pack/tmp_pack_x"), QByteArray(23, 'c'));

  EXPECT_EQ(ost::core::SnapshotScanner::directorySize(temp.path()), 51);
}

TEST(SnapshotScanner, MissingSnapshotRootProducesActionableWarning) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto result = ost::core::SnapshotScanner().scan(
      QDir(temp.path()).filePath("missing-snapshot-root"),
      QDir(temp.path()).filePath("missing-opencode.db"), {});

  EXPECT_TRUE(result.repositories.isEmpty());
  ASSERT_FALSE(result.warnings.isEmpty());
  EXPECT_TRUE(result.warnings.join(' ').contains("does not exist", Qt::CaseInsensitive));
}
}  // namespace
