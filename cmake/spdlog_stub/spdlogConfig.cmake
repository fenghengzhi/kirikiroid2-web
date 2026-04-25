include(CMakeFindDependencyMacro)

find_dependency(fmt CONFIG REQUIRED)

if(NOT TARGET krkr2_spdlog_stub)
    add_library(krkr2_spdlog_stub INTERFACE)
    target_include_directories(krkr2_spdlog_stub INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
    )
    target_compile_definitions(krkr2_spdlog_stub INTERFACE
        KRKR2_USE_STUB_SPDLOG=1
    )
    target_link_libraries(krkr2_spdlog_stub INTERFACE fmt::fmt)
endif()

if(NOT TARGET spdlog::spdlog)
    add_library(spdlog::spdlog ALIAS krkr2_spdlog_stub)
endif()

if(NOT TARGET spdlog::spdlog_header_only)
    add_library(spdlog::spdlog_header_only ALIAS krkr2_spdlog_stub)
endif()
