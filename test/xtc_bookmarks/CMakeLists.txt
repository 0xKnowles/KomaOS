add_executable(XtcBookmarksTest
  XtcBookmarksTest.cpp
  ${REPO_ROOT}/src/util/XtcBookmarks.cpp
)

# Compiles the pure encode/decode/toggle half only; load()/save() need HalStorage.
target_compile_definitions(XtcBookmarksTest PRIVATE XTC_BOOKMARKS_HOST_TEST)

target_include_directories(XtcBookmarksTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(XtcBookmarksTest PRIVATE
  komaos_test_common
  GTest::gtest_main
)

gtest_discover_tests(XtcBookmarksTest)
