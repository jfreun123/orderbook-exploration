# ---- Replay ----

add_executable(replay source/replay.cpp)
target_link_libraries(replay PRIVATE orderbook-exploration_lib)
target_compile_features(replay PRIVATE cxx_std_23)

set_target_properties(replay PROPERTIES
    CXX_CLANG_TIDY "clang-tidy"
)
