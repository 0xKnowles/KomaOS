# Header-only: decodeSplitGeometry and pageStartMapBytes live in XtcTypes.h.
# The parser's map reader needs HalStorage and a real file, so it is not here.
add_executable(XtcSplitGeometryTest
  XtcSplitGeometryTest.cpp
)

target_include_directories(XtcSplitGeometryTest PRIVATE
  ${REPO_ROOT}/lib/Xtc
)

target_link_libraries(XtcSplitGeometryTest PRIVATE
  komaos_test_common
  GTest::gtest_main
)

gtest_discover_tests(XtcSplitGeometryTest)
