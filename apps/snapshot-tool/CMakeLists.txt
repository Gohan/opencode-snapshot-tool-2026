qt_add_executable(opencode-snapshot-tool
  main.cpp
  snapshot_controller.cpp
  snapshot_controller.h
)

qt_add_qml_module(opencode-snapshot-tool
  URI OpenCodeSnapshotTool
  VERSION 1.0
  QML_FILES qml/Main.qml
)

target_link_libraries(opencode-snapshot-tool PRIVATE
  ost::core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Widgets
  Qt6::Concurrent
)
ost_set_warnings(opencode-snapshot-tool)

set_target_properties(opencode-snapshot-tool PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "OpenCode Snapshot Tool"
)
