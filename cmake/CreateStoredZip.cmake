cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

file(GLOB_RECURSE _archive_entries
    LIST_DIRECTORIES FALSE
    RELATIVE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/*"
)
list(SORT _archive_entries)

if(NOT _archive_entries)
    message(FATAL_ERROR "No files found in ${CMAKE_CURRENT_BINARY_DIR}")
endif()

file(ARCHIVE_CREATE
    OUTPUT "${OUTPUT_FILE}"
    PATHS ${_archive_entries}
    FORMAT zip
    COMPRESSION None
)
