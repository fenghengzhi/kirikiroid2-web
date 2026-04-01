cmake_minimum_required(VERSION 3.28)
project(ksdec LANGUAGES CXX VERSION 1.0.0)

find_package(ZLIB REQUIRED)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROGRAM_NAME="${PROJECT_NAME}"
    VERSION="${PROJECT_VERSION}"
)
