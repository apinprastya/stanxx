cmake_minimum_required(VERSION 3.10)
project(stanxxProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -march=native")

add_subdirectory(stanxx)
add_subdirectory(stanxx-server)
add_subdirectory(tests)
add_subdirectory(benchmarks)