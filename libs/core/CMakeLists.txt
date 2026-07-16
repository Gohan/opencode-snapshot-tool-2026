add_library(ost_core STATIC
  src/retention_policy.cpp
  src/git_client.cpp
  src/snapshot_scanner.cpp
  src/snapshot_analyzer.cpp
  src/snapshot_cleaner.cpp
  src/app_settings.cpp
)
add_library(ost::core ALIAS ost_core)

target_include_directories(ost_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ost_core PUBLIC Qt6::Core Qt6::Sql Qt6::Concurrent)
ost_set_warnings(ost_core)
