
cmake_minimum_required(VERSION 3.10)

# Find GTest (assumes you've installed it or use FetchContent)
find_package(GTest REQUIRED)

add_executable(test_stanxx test_stanxx.cpp)
target_include_directories(test_stanxx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../stanxx)
target_link_libraries(test_stanxx PRIVATE stanxx GTest::GTest GTest::Main)
set_target_properties(test_stanxx PROPERTIES CXX_STANDARD 23)

enable_testing()
add_test(NAME test_stanxx COMMAND test_stanxx)