add_executable(ReadingDirectionTest
  ReadingDirectionTest.cpp
)

target_include_directories(ReadingDirectionTest PRIVATE
  ${REPO_ROOT}/lib/Xtc
)

target_link_libraries(ReadingDirectionTest PRIVATE
  komaos_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingDirectionTest)
