cmake_minimum_required(VERSION 3.10)
project(stanxxProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(stanxx)
add_subdirectory(stanxx-server)
add_subdirectory(test)