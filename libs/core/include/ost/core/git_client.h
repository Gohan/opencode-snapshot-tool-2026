#pragma once

#include <QString>
#include <QStringList>

namespace ost::core {

struct GitResult {
  int exitCode = -1;
  QByteArray output;
  QByteArray error;
  bool ok() const { return exitCode == 0; }
};

class GitClient {
 public:
  GitResult run(const QString& gitDir, const QStringList& arguments,
                const QByteArray& input = {}, int timeoutMs = 120000) const;
  bool available() const;
};

}  // namespace ost::core
