add_executable(FullPageLayoutTest
  FullPageLayoutTest.cpp
  ${REPO_ROOT}/src/util/FullPageLayout.cpp
)

target_include_directories(FullPageLayoutTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(FullPageLayoutTest PRIVATE
  komaos_test_common
  GTest::gtest_main
)

gtest_discover_tests(FullPageLayoutTest)
