# Version the index.js script tag emitted by Emscripten's {{{ SCRIPT }}}
# expansion. shell.html cannot add a query string to that placeholder itself.
if(NOT DEFINED HTML OR NOT EXISTS "${HTML}")
    message(FATAL_ERROR "version_web_entrypoint: HTML does not exist: ${HTML}")
endif()
if(NOT DEFINED BUILD_VERSION OR BUILD_VERSION STREQUAL "")
    message(FATAL_ERROR "version_web_entrypoint: BUILD_VERSION is empty")
endif()
if(NOT BUILD_VERSION MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "version_web_entrypoint: unsafe BUILD_VERSION: ${BUILD_VERSION}")
endif()

file(READ "${HTML}" html_content)
set(versioned_src "src=\"index.js?v=${BUILD_VERSION}\"")

if(NOT html_content MATCHES "index\\.js\\?v=${BUILD_VERSION}")
    set(original_content "${html_content}")
    string(REPLACE "src=\"index.js\"" "${versioned_src}" html_content "${html_content}")
    string(REPLACE "src='index.js'" "${versioned_src}" html_content "${html_content}")
    string(REPLACE "src=index.js " "${versioned_src} " html_content "${html_content}")
    string(REPLACE "src=index.js>" "${versioned_src}>" html_content "${html_content}")
    if(html_content STREQUAL original_content)
        message(FATAL_ERROR
            "version_web_entrypoint: Emscripten index.js script tag was not found in ${HTML}")
    endif()
endif()

file(WRITE "${HTML}" "${html_content}")
message(STATUS
    "version_web_entrypoint: index.js query version set to ${BUILD_VERSION}")
