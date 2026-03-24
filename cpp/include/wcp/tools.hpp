// wechat-copilot — RustCC C++17 edition
// tools.hpp — local tool execution for the agentic loop
//
// TCC-OWN:  ToolResult owns its output string.
// TCC-LIFE: WORK_DIR borrowed from Config; never stored.
// TCC-CONC: All functions are stateless — safe to call concurrently.

#pragma once

#include "wcp/config.hpp"
#include <string>
#include <string_view>

namespace wcp {

struct ToolResult {
    bool        ok;
    std::string output;   // stdout+stderr, truncated if too large
};

// ── Tool definitions (what the LLM can call) ──────────────────────────────
//
// read_file(path)            — read a file under WORK_DIR
// list_dir(path)             — list a directory
// run_command(command)       — run a shell command in WORK_DIR (30s timeout)
// write_file(path, content)  — write/overwrite a file under WORK_DIR
// git_command(args)          — run a git command in WORK_DIR

// Returns the JSON array of tool definitions for the Copilot API request.
std::string tool_definitions_json();

// Dispatch a tool call by name with its JSON arguments string.
// All paths are resolved relative to cfg.work_dir for safety.
ToolResult dispatch_tool(
    std::string_view   tool_name,
    std::string_view   args_json,   // JSON object of arguments
    const Config&      cfg);

} // namespace wcp
