cmake_minimum_required(VERSION 3.10)

# Find GTest (assumes you've installed it or use FetchContent)
find_package(GTest REQUIRED)

add_executable(test_message_parser test_message_parser.cpp)
target_include_directories(test_message_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../stanxx)
target_link_libraries(test_message_parser PRIVATE stanxx GTest::GTest GTest::Main GTest::gmock GTest::gmock_main pthread)
set_target_properties(test_message_parser PROPERTIES CXX_STANDARD 23)

enable_testing()
add_test(NAME test_message_parser COMMAND test_message_parser)