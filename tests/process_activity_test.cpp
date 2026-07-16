#include "ost/core/opencode_process_detector.h"

#include <QDir>
#include <gtest/gtest.h>

namespace {
using ost::core::OpenCodeProcessInfo;
using ost::core::ProjectBinding;
using ost::core::RepositoryActivityState;
using ost::core::RepositoryInfo;
using ost::core::OpenCodeProcessDetector;

RepositoryInfo repository(QString project, QString worktree, QString store) {
  RepositoryInfo result;
  result.projectId = std::move(project);
  result.worktree = QDir::cleanPath(std::move(worktree));
  result.gitDir = QDir::cleanPath(std::move(store));
  return result;
}

OpenCodeProcessInfo process(qint64 pid, QString worktree) {
  OpenCodeProcessInfo result;
  result.pid = pid;
  result.executable = QStringLiteral("opencode.exe");
  result.workingDirectory = QDir::cleanPath(worktree);
  result.worktree = QDir::cleanPath(std::move(worktree));
  result.accessible = true;
  return result;
}

TEST(OpenCodeProcessDetector, MapsOnlyCurrentProjectStoreAndLeavesLegacyStoreInactive) {
  const auto worktree = QDir::cleanPath(QStringLiteral("C:/code/takingNotes"));
  QVector<RepositoryInfo> repositories{
      repository(QStringLiteral("legacy-project"), worktree, QStringLiteral("C:/snap/legacy/store")),
      repository(QStringLiteral("current-project"), worktree, QStringLiteral("C:/snap/current/store")),
  };
  const QVector<OpenCodeProcessInfo> processes{process(145100, worktree)};
  const QVector<ProjectBinding> projects{
      {QStringLiteral("current-project"), {worktree}},
  };

  OpenCodeProcessDetector::classify(repositories, processes, projects);

  EXPECT_EQ(repositories[0].activity.state, RepositoryActivityState::Inactive);
  EXPECT_TRUE(repositories[0].activity.processIds.isEmpty());
  EXPECT_EQ(repositories[1].activity.state, RepositoryActivityState::Active);
  EXPECT_EQ(repositories[1].activity.processIds, QVector<qint64>{145100});
}

TEST(OpenCodeProcessDetector, CurrentProjectBindingKeepsLegacyStoreInactiveWhenCheckedAlone) {
  const auto worktree = QDir::cleanPath(QStringLiteral("C:/code/takingNotes"));
  QVector<RepositoryInfo> repositories{
      repository(QStringLiteral("legacy-project"), worktree, QStringLiteral("C:/snap/legacy/store")),
  };

  OpenCodeProcessDetector::classify(
      repositories, {process(145100, worktree)},
      {{QStringLiteral("current-project"), {worktree}}});

  EXPECT_EQ(repositories.front().activity.state, RepositoryActivityState::Inactive);
  EXPECT_TRUE(repositories.front().activity.processIds.isEmpty());
}

TEST(OpenCodeProcessDetector, DistinguishesWorktreesInsideOneProject) {
  QVector<RepositoryInfo> repositories{
      repository(QStringLiteral("mux-project"), QStringLiteral("C:/code/mux"),
                 QStringLiteral("C:/snap/mux/main")),
      repository(QStringLiteral("mux-project"), QStringLiteral("C:/worktrees/mux/docs"),
                 QStringLiteral("C:/snap/mux/docs")),
      repository(QStringLiteral("mux-project"), QStringLiteral("C:/worktrees/mux/unused"),
                 QStringLiteral("C:/snap/mux/unused")),
  };
  const QVector<OpenCodeProcessInfo> processes{
      process(10, QStringLiteral("C:/code/mux")),
      process(20, QStringLiteral("C:/worktrees/mux/docs")),
  };
  const QVector<ProjectBinding> projects{{QStringLiteral("mux-project"),
                                           {QStringLiteral("C:/code/mux"),
                                            QStringLiteral("C:/worktrees/mux/docs"),
                                            QStringLiteral("C:/worktrees/mux/unused")}}};

  OpenCodeProcessDetector::classify(repositories, processes, projects);

  EXPECT_EQ(repositories[0].activity.state, RepositoryActivityState::Active);
  EXPECT_EQ(repositories[0].activity.processIds, QVector<qint64>{10});
  EXPECT_EQ(repositories[1].activity.state, RepositoryActivityState::Active);
  EXPECT_EQ(repositories[1].activity.processIds, QVector<qint64>{20});
  EXPECT_EQ(repositories[2].activity.state, RepositoryActivityState::Inactive);
}

TEST(OpenCodeProcessDetector, MarksDuplicateStoresPossiblyActiveWhenProjectIdentityIsUnknown) {
  const auto worktree = QStringLiteral("C:/code/project");
  QVector<RepositoryInfo> repositories{
      repository(QStringLiteral("one"), worktree, QStringLiteral("C:/snap/one/store")),
      repository(QStringLiteral("two"), worktree, QStringLiteral("C:/snap/two/store")),
  };

  OpenCodeProcessDetector::classify(repositories, {process(42, worktree)}, {});

  EXPECT_EQ(repositories[0].activity.state, RepositoryActivityState::PossiblyActive);
  EXPECT_EQ(repositories[1].activity.state, RepositoryActivityState::PossiblyActive);
  EXPECT_EQ(repositories[0].activity.processIds, QVector<qint64>{42});
}

TEST(OpenCodeProcessDetector, MultiProjectProcessMakesEveryStorePossiblyActive) {
  QVector<RepositoryInfo> repositories{
      repository(QStringLiteral("one"), QStringLiteral("C:/code/one"),
                 QStringLiteral("C:/snap/one/store")),
      repository(QStringLiteral("two"), QStringLiteral("C:/code/two"),
                 QStringLiteral("C:/snap/two/store")),
  };
  auto server = process(99, QStringLiteral("C:/code/one"));
  server.multiProject = true;
  server.commandLine = QStringLiteral("opencode serve --port 4096");

  OpenCodeProcessDetector::classify(repositories, {server}, {});

  for (const auto& item : repositories) {
    EXPECT_EQ(item.activity.state, RepositoryActivityState::PossiblyActive);
    EXPECT_EQ(item.activity.processIds, QVector<qint64>{99});
  }
}
}  // namespace
