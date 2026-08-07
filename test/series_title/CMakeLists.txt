add_executable(SeriesTitleTest
  SeriesTitleTest.cpp
  ${REPO_ROOT}/src/util/SeriesTitle.cpp
)

target_include_directories(SeriesTitleTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(SeriesTitleTest PRIVATE
  komaos_test_common
  GTest::gtest_main
)

gtest_discover_tests(SeriesTitleTest)
