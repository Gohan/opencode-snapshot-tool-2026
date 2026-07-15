#include "snapshot_controller.h"

#include <QApplication>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char* argv[]) {
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("OpenCodeTools"));
  QCoreApplication::setApplicationName(QStringLiteral("OpenCode Snapshot Tool"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("OpenCode Snapshot Tool"));

  QFontDatabase::addApplicationFont(
      QStringLiteral(":/qt/qml/OpenCodeSnapshotTool/assets/fonts/Inter-Variable.ttf"));
  QFontDatabase::addApplicationFont(
      QStringLiteral(":/qt/qml/OpenCodeSnapshotTool/assets/fonts/SpaceGrotesk-Variable.ttf"));
  app.setFont(QFont(QStringLiteral("Inter"), 10));

  QQmlApplicationEngine engine;
  SnapshotController controller;
  engine.rootContext()->setContextProperty(QStringLiteral("snapshotController"), &controller);
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("OpenCodeSnapshotTool"), QStringLiteral("Main"));
  return app.exec();
}
