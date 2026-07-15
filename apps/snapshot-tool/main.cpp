#include "snapshot_controller.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("OpenCodeTools"));
  QCoreApplication::setApplicationName(QStringLiteral("OpenCode Snapshot Tool"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("OpenCode Snapshot Tool"));

  QQmlApplicationEngine engine;
  SnapshotController controller;
  engine.rootContext()->setContextProperty(QStringLiteral("snapshotController"), &controller);
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("OpenCodeSnapshotTool"), QStringLiteral("Main"));
  return app.exec();
}
