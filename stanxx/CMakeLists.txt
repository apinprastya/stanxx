cmake_minimum_required(VERSION 3.10)

project(stanxx VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Seastar REQUIRED)
find_package(Valgrind REQUIRED)
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_library(${PROJECT_NAME} STATIC
    server.cpp
    transport_tcp.cpp
    client.cpp
    parser.cpp
    subscriber.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} Seastar::seastar spdlog::spdlog ${Boost_LIBRARIES} Threads::Threads)