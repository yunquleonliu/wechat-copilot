cmake_minimum_required(VERSION 3.17)
project(wechat-copilot-cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags (RustCC style: strict warnings, no UB) ─────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -fno-omit-frame-pointer
)

# ── Dependencies ──────────────────────────────────────────────────────────
find_package(CURL REQUIRED)

# nlohmann/json — single header vendored in vendor/
set(JSON_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json.hpp)
if(NOT EXISTS ${JSON_HEADER})
    message(FATAL_ERROR "vendor/json.hpp missing. Run: "
        "curl -fsSL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp "
        "-o vendor/json.hpp")
endif()

# ── Include paths ─────────────────────────────────────────────────────────
include_directories(
    include
    vendor              # for #include <nlohmann/json.hpp>
)

# Alias vendor/ so #include <nlohmann/json.hpp> resolves to vendor/json.hpp
# by placing vendor/ in path and adding a redirect header at vendor/nlohmann/json.hpp
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann/json.hpp)
    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann/json.hpp
        "#pragma once\n#include \"../json.hpp\"\n")
endif()

# ── Library: wcp (all shared modules) ────────────────────────────────────
set(WCP_SOURCES
    src/config.cpp
    src/http.cpp
    src/ilink.cpp
    src/router.cpp
    src/adapter.cpp
    src/agents/copilot.cpp
    src/agents/sidecar.cpp
)

add_library(wcp STATIC ${WCP_SOURCES})
target_link_libraries(wcp PUBLIC CURL::libcurl)

# ── Executable: wcp-bridge (main bridge process) ─────────────────────────
add_executable(wcp-bridge src/main.cpp)
target_link_libraries(wcp-bridge PRIVATE wcp)

# ── Executable: wcp-setup (WeChat QR login) ──────────────────────────────
add_executable(wcp-setup src/setup.cpp)
target_link_libraries(wcp-setup PRIVATE wcp)

# ── Install ───────────────────────────────────────────────────────────────
install(TARGETS wcp-bridge wcp-setup DESTINATION bin)
