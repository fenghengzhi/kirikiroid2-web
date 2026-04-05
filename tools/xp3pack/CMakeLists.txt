cmake_minimum_required(VERSION 3.28)
project(xp3pack LANGUAGES CXX VERSION 1.0.0)

add_executable(${PROJECT_NAME}
    main.cpp
)

find_package(argparse CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE argparse::argparse ZLIB::ZLIB)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROGRAM_NAME="${PROJECT_NAME}"
    VERSION="${PROJECT_VERSION}"
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
