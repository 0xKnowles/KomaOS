add_executable(XtcProgressTest
  XtcProgressTest.cpp
  ${REPO_ROOT}/src/util/XtcProgress.cpp
)

# Compiles the pure encode/decode half only; cachePathFor/read/write need HalStorage.
target_compile_definitions(XtcProgressTest PRIVATE XTC_PROGRESS_HOST_TEST)

target_include_directories(XtcProgressTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(XtcProgressTest PRIVATE
  komaos_test_common
  GTest::gtest_main
)

gtest_discover_tests(XtcProgressTest)
