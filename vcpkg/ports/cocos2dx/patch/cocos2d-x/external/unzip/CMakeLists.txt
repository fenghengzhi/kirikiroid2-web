cmake_minimum_required(VERSION 3.6)

set(lib_name unzip)
set(target_name ext_${lib_name})

project(${lib_name})

set(${target_name}_src
  ioapi.cpp
  unzip.cpp
  ioapi_mem.cpp
)

add_library(${target_name} STATIC
  ${${target_name}_src}
)

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/"
    DESTINATION "include/unzip"
    FILES_MATCHING PATTERN "*.h")

target_include_directories(${target_name}
    INTERFACE
        $<INSTALL_INTERFACE:include/unzip>
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
)

get_target_property(zlib_header ext_zlib INTERFACE_INCLUDE_DIRECTORIES)
if(zlib_header)
  target_include_directories(${target_name} PRIVATE ${zlib_header})
endif()

set_target_properties(${target_name}
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "External"
)
