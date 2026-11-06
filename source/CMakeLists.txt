# ---- Library ----

add_library(orderbook-exploration_lib INTERFACE)

target_include_directories(
    orderbook-exploration_lib
    INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>"
)

target_compile_features(orderbook-exploration_lib INTERFACE cxx_std_23)
