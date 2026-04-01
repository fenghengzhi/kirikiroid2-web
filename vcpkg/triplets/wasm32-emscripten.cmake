set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_BUILD_TYPE release)

list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_C_FLAGS=-pthread -fwasm-exceptions"
    "-DCMAKE_CXX_FLAGS=-pthread -fwasm-exceptions"
)

if(DEFINED ENV{EMSDK})
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
elseif(DEFINED ENV{EMSCRIPTEN})
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{EMSCRIPTEN}/cmake/Modules/Platform/Emscripten.cmake")
endif()
