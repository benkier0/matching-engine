cmake_minimum_required(VERSION 3.20)
project(matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Per-config compile flags
add_compile_options(
  $<$<CONFIG:Release>:-O3>
  $<$<CONFIG:Release>:-march=native>
  $<$<CONFIG:Release>:-DNDEBUG>
  $<$<CONFIG:Debug>:-g>
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
  $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)
add_link_options(
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

include_directories(include)

# Core engine logic — compiled once, linked into every target.
add_library(engine_core STATIC
  src/order_book.cpp
  src/fix_parser.cpp
)

# ── main executable (demo + --replay mode) ────────────────────────────────────
add_executable(matching_engine src/main.cpp)
target_link_libraries(matching_engine PRIVATE engine_core)
set_target_properties(matching_engine
  PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src
)

# ── unit tests ────────────────────────────────────────────────────────────────
add_executable(order_book_test tests/order_book_test.cpp)
target_link_libraries(order_book_test PRIVATE engine_core)
set_target_properties(order_book_test
  PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# ── latency / throughput benchmark ────────────────────────────────────────────
add_executable(benchmark bench/benchmark.cpp)
target_link_libraries(benchmark PRIVATE engine_core)
set_target_properties(benchmark
  PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench
)

# ── ctest integration ─────────────────────────────────────────────────────────
enable_testing()
add_test(NAME order_book_test COMMAND order_book_test)
