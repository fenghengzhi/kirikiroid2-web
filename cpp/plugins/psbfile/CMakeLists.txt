cmake_minimum_required(VERSION 3.19)
project(psbfile)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
    PSBRawFile.cpp
    PSBMedia.cpp
    PSBMediaRegistry.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE krkr2core)
