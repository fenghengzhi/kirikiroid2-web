cmake_minimum_required(VERSION 3.19)

add_subdirectory(minizip)

if(NOT EMSCRIPTEN)
    add_subdirectory(libbpg)
endif()
