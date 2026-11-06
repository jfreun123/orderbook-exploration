# ---- Tests ----

include(GoogleTest)

foreach(name leetcode_test book_test agreement_test)
  add_executable(${name} source/${name}.cpp)
  target_link_libraries(${name} PRIVATE orderbook-exploration_lib GTest::gtest_main)
  target_compile_features(${name} PRIVATE cxx_std_23)
  set_target_properties(${name} PROPERTIES
      CXX_CLANG_TIDY "clang-tidy"
      CXX_CPPCHECK "cppcheck;--inline-suppr;--library=googletest;--library=${PROJECT_SOURCE_DIR}/cmake/cppcheck-gtest.cfg"
  )
  gtest_discover_tests(${name})
endforeach()
