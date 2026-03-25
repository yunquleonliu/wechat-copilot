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

# ── RCC (RustCC Profiler) enforcement gate ────────────────────────────────
# rcc_check(target): adds a PRE_BUILD custom command that runs rcc-check on
# all _t.cc sources of the target. Violations in _t.cc files are fatal.
#
# Usage:
#   rcc_check(wcp)          # enforce on library sources
#   rcc_check(wcp-bridge)   # enforce on main_t.cc
#
# Skip: set RCC_SKIP=1 in the environment or cmake -DRCC_SKIP=ON.
# ──────────────────────────────────────────────────────────────────────────
option(RCC_SKIP "Skip RCC safety checks (not recommended)" OFF)

# Locate the rcc-check binary (prefer sibling build dirs)
find_program(RCC_CHECK_BIN
    NAMES rcc-check
    HINTS
        "${CMAKE_CURRENT_SOURCE_DIR}/../Tough-C-Profiler/build-linux/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../Tough-C-Profiler/build-linux/src"
        "/datapai/Tough-C-Profiler/build-linux/src"
        "/datapai/Tough-C-Profiler.worktrees/copilot-worktree-2026-03-24T02-26-05/build-linux/src"
    NO_DEFAULT_PATH
)

if(RCC_CHECK_BIN)
    message(STATUS "RCC: found rcc-check at ${RCC_CHECK_BIN}")
else()
    message(STATUS "RCC: rcc-check not found — RCC enforcement disabled (build from Tough-C-Profiler first)")
    set(RCC_SKIP ON)
endif()

function(rcc_check target_name)
    if(RCC_SKIP)
        return()
    endif()

    # Collect _t.cc sources from the target
    get_target_property(_srcs ${target_name} SOURCES)
    set(_tcc_srcs "")
    foreach(src ${_srcs})
        if(src MATCHES "_t\\.cc$")
            list(APPEND _tcc_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        endif()
    endforeach()

    if(NOT _tcc_srcs)
        return()
    endif()

    # Get include dirs for this target
    get_target_property(_incs ${target_name} INCLUDE_DIRECTORIES)
    set(_inc_args "")
    foreach(inc ${_incs})
        list(APPEND _inc_args "-I${inc}")
    endforeach()

    foreach(src_file ${_tcc_srcs})
        get_filename_component(_base "${src_file}" NAME)
        add_custom_command(TARGET ${target_name} PRE_BUILD
            COMMAND "${RCC_CHECK_BIN}" "${src_file}"
                --auto-stdcpp-includes
                --
                -std=c++17
                ${_inc_args}
            COMMENT "RCC [ENFORCE] ${_base}"
            VERBATIM
        )
    endforeach()
endfunction()

# ── Alias vendor/ so #include <nlohmann/json.hpp> resolves to vendor/json.hpp
# by placing vendor/ in path and adding a redirect header at vendor/nlohmann/json.hpp
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann/json.hpp)
    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann/json.hpp
        "#pragma once\n#include \"../json.hpp\"\n")
endif()

# ── Library: wcp (all shared modules) ────────────────────────────────────
set(WCP_SOURCES
    src/config_t.cc
    src/http_t.cc
    src/ilink_t.cc
    src/tools_t.cc
    src/router_t.cc
    src/adapter_t.cc
    src/agents/copilot_t.cc
    src/agents/sidecar_t.cc
)

add_library(wcp STATIC ${WCP_SOURCES})
target_link_libraries(wcp PUBLIC CURL::libcurl)
target_include_directories(wcp PUBLIC include vendor)
rcc_check(wcp)

# ── Executable: wcp-bridge (main bridge process) ─────────────────────────
add_executable(wcp-bridge src/main_t.cc)
target_link_libraries(wcp-bridge PRIVATE wcp)
rcc_check(wcp-bridge)

# ── Executable: wcp-setup (WeChat QR login) ──────────────────────────────
add_executable(wcp-setup src/setup_t.cc)
target_link_libraries(wcp-setup PRIVATE wcp)
rcc_check(wcp-setup)

# ── Install ───────────────────────────────────────────────────────────────
install(TARGETS wcp-bridge wcp-setup DESTINATION bin)
