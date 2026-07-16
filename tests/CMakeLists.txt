ost_add_tests(ost_tests
  retention_policy_test.cpp
  scanner_layout_test.cpp
  scanner_database_integration_test.cpp
  analyzer_integration_test.cpp
  cleaner_integration_test.cpp
  process_activity_test.cpp
)
target_link_libraries(ost_tests PRIVATE ost::core Qt6::Core Qt6::Sql)
