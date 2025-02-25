cmake_minimum_required(VERSION 3.10)

# Find Google Benchmark (assumes you've installed it or use FetchContent)
find_package(benchmark REQUIRED)

add_executable(benchmark_message_parser benchmark_message_parser.cpp)
target_include_directories(benchmark_message_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../stanxx)
target_link_libraries(benchmark_message_parser PRIVATE stanxx benchmark::benchmark)
set_target_properties(benchmark_message_parser PROPERTIES CXX_STANDARD 23)
