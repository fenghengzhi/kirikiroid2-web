cmake_minimum_required(VERSION 3.16)
project(TestBaseModule LANGUAGES CXX)

add_executable(async-binary-stream async-binary-stream.cpp main.cpp)

target_link_libraries(async-binary-stream
    PRIVATE
        Catch2::Catch2
    PUBLIC
        krkr2core
)
target_include_directories(async-binary-stream PRIVATE "${TEST_CONFIG_DIR}")
catch_discover_tests(async-binary-stream)
