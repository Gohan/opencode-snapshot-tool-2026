include(FetchContent)
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

function(ost_add_tests name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE GTest::gtest_main)
  ost_set_warnings(${name})
  include(GoogleTest)
  gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()
