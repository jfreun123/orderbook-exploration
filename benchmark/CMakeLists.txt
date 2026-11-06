# ---- Benchmarks ----

foreach(name benchmark latency)
  add_executable(${name} source/${name}.cpp)
  target_link_libraries(${name} PRIVATE orderbook-exploration_lib)
  target_compile_features(${name} PRIVATE cxx_std_23)
  set_target_properties(${name} PROPERTIES
      CXX_CLANG_TIDY "clang-tidy"
  )
endforeach()
