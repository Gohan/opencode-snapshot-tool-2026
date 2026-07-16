#pragma once

#include "ost/core/snapshot_types.h"

namespace ost::core {

struct OpenCodeProcessInfo {
  qint64 pid = 0;
  QString executable;
  QString commandLine;
  QString workingDirectory;
  QString worktree;
  bool accessible = false;
  bool multiProject = false;
};

struct ProjectBinding {
  QString projectId;
  QStringList worktrees;
};

class OpenCodeProcessDetector {
 public:
  QVector<OpenCodeProcessInfo> processes() const;
  QVector<ProjectBinding> projectBindings(const QString& databasePath) const;
  void detect(QVector<RepositoryInfo>& repositories, const QString& databasePath) const;

  static void classify(QVector<RepositoryInfo>& repositories,
                       const QVector<OpenCodeProcessInfo>& processes,
                       const QVector<ProjectBinding>& projects);
};

}  // namespace ost::core
