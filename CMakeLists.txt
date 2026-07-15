cmake_minimum_required(VERSION 3.24)
project(opencode-snapshot-tool-2026 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OST_BUILD_APP "Build the Qt Quick desktop application" ON)
option(OST_BUILD_TESTS "Build GoogleTest tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OstWarnings)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Sql Concurrent)
qt_standard_project_setup(REQUIRES 6.8)
add_subdirectory(libs/core)

if(OST_BUILD_APP)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 Widgets)
  add_subdirectory(apps/snapshot-tool)
endif()

if(OST_BUILD_TESTS)
  enable_testing()
  include(OstGTest)
  add_subdirectory(tests)
endif()
