#include "ost/core/git_client.h"

#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace ost::core {
GitResult GitClient::run(const QString& gitDir, const QStringList& arguments,
                         const QByteArray& input, int timeoutMs) const {
  QProcess process;
  const auto executable = QStandardPaths::findExecutable(QStringLiteral("git"));
  if (executable.isEmpty()) return {-1, {}, QByteArray("git executable was not found in PATH")};
  QStringList args{QStringLiteral("--git-dir=%1").arg(gitDir)};
  args.append(arguments);
  process.start(executable, args);
  if (!process.waitForStarted(std::min(timeoutMs, 10000)))
    return {-1, {}, process.errorString().toUtf8()};
  if (!input.isEmpty()) process.write(input);
  process.closeWriteChannel();
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    process.waitForFinished(5000);
    return {-1, process.readAllStandardOutput(), QByteArray("git timed out")};
  }
  return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

bool GitClient::available() const {
  return !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
}
}  // namespace ost::core
