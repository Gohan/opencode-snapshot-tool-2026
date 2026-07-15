#include "ost/core/git_client.h"
#include "ost/core/snapshot_scanner.h"

#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <gtest/gtest.h>

#include <algorithm>

namespace {
using ost::core::GitClient;
using ost::core::RetentionSettings;
using ost::core::SnapshotScanner;

QString createTree(const GitClient& git, const QString& gitDir) {
  const auto blob = git.run(gitDir, {"hash-object", "-w", "--stdin"}, "database snapshot");
  EXPECT_TRUE(blob.ok()) << blob.error.constData();
  const auto tree = git.run(gitDir, {"mktree"},
                            "100644 blob " + blob.output.trimmed() + "\tfixture.txt\n");
  EXPECT_TRUE(tree.ok()) << tree.error.constData();
  return QString::fromLatin1(tree.output.trimmed());
}

TEST(SnapshotScanner, MapsDatabaseTreeAndCountsDistinctSessions) {
  int argc = 1;
  char executable[] = "ost-tests";
  char* argv[] = {executable, nullptr};
  QCoreApplication application(argc, argv);
  GitClient git;
  if (!git.available()) GTEST_SKIP() << "git is required";
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());

  const auto snapshotRoot = QDir(temp.path()).filePath("snapshot");
  const auto worktree = QDir(temp.path()).filePath("workspace");
  const auto gitDir = QDir(snapshotRoot).filePath("project-1/worktree-1");
  ASSERT_TRUE(QDir().mkpath(worktree));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(gitDir).path()));
  ASSERT_TRUE(git.run(gitDir, {"init", "--bare"}).ok());
  ASSERT_TRUE(git.run(gitDir, {"config", "core.worktree", worktree}).ok());
  const auto tree = createTree(git, gitDir);

  const auto databasePath = QDir(temp.path()).filePath("opencode.db");
  const auto connection = "scanner-fixture-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  {
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(databasePath);
    ASSERT_TRUE(db.open()) << db.lastError().text().toStdString();
    QSqlQuery query(db);
    ASSERT_TRUE(query.exec("CREATE TABLE session (id TEXT PRIMARY KEY, project_id TEXT, directory TEXT, title TEXT)"));
    ASSERT_TRUE(query.exec("CREATE TABLE part (time_created INTEGER, data TEXT, session_id TEXT)"));
    query.prepare("INSERT INTO session VALUES ('session-1','project-1',:directory,'Fixture session')");
    query.bindValue(":directory", worktree);
    ASSERT_TRUE(query.exec());
    query.prepare("INSERT INTO part VALUES (:time,:data,'session-1')");
    query.bindValue(":time", QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    query.bindValue(":data", QString::fromUtf8(QJsonDocument(QJsonObject{{"snapshot", tree}}).toJson(QJsonDocument::Compact)));
    ASSERT_TRUE(query.exec());
    query.bindValue(":time", QDateTime::currentDateTimeUtc().addSecs(1).toMSecsSinceEpoch());
    ASSERT_TRUE(query.exec());
    db.close();
  }
  QSqlDatabase::removeDatabase(connection);

  const auto result = SnapshotScanner(git).scan(snapshotRoot, databasePath, RetentionSettings{});
  ASSERT_EQ(result.repositories.size(), 1);
  const auto& snapshots = result.repositories[0].snapshots;
  const auto found = std::find_if(snapshots.cbegin(), snapshots.cend(),
                                  [&](const auto& item) { return item.hash == tree; });
  ASSERT_NE(found, snapshots.cend());
  const auto& snapshot = *found;
  EXPECT_EQ(snapshot.hash, tree);
  EXPECT_EQ(snapshot.references, 2);
  EXPECT_EQ(snapshot.sessions, 1);
  EXPECT_EQ(snapshot.titles, QStringList{"Fixture session"});
  EXPECT_TRUE(snapshot.exists);
  EXPECT_TRUE(snapshot.keep);
  EXPECT_GT(result.repositories[0].actualBytes, 0);
  EXPECT_EQ(result.unmappedDatabaseRecords, 0);
}
}  // namespace
