add_executable(XthPixelsTest
  XthPixelsTest.cpp
)

target_include_directories(XthPixelsTest PRIVATE
  ${REPO_ROOT}/lib/Xtc
)

target_link_libraries(XthPixelsTest PRIVATE
  komaos_test_common
  GTest::gtest_main
)

gtest_discover_tests(XthPixelsTest)
