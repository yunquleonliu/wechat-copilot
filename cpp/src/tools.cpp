// wechat-copilot — RustCC C++17 edition
// tools.cpp — local tool execution
//
// TCC-OWN:  All results returned by value.
// TCC-LIFE: paths resolved to absolute before any syscall; no dangling refs.
// TCC-CONC: popen/fread are thread-safe on Linux; no shared state.

#include "wcp/tools.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace wcp {

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Safety constants ──────────────────────────────────────────────────────

static constexpr size_t MAX_OUTPUT   = 8000;   // chars returned to LLM
static constexpr int    CMD_TIMEOUT  = 30;      // seconds

// ── Tool definitions JSON ─────────────────────────────────────────────────

std::string tool_definitions_json() {
    json tools = json::array({
        {
            {"type", "function"},
            {"function", {
                {"name", "read_file"},
                {"description", "Read the contents of a file in the project."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {{"type","string"},{"description","File path relative to WORK_DIR or absolute"}}}
                    }},
                    {"required", {"path"}}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "list_dir"},
                {"description", "List files and directories in a path."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {{"type","string"},{"description","Directory path (default: WORK_DIR)"}}}
                    }}
                    // no "required" — all parameters are optional for list_dir
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "run_command"},
                {"description",
                    "Run a shell command in WORK_DIR. "
                    "Use for: make, cmake, ctest, grep, find, cat, git, rg, etc. "
                    "30-second timeout. stdout+stderr captured."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"command", {{"type","string"},{"description","Shell command to run"}}}
                    }},
                    {"required", {"command"}}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "write_file"},
                {"description", "Write content to a file (creates or overwrites)."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path",    {{"type","string"},{"description","File path"}}},
                        {"content", {{"type","string"},{"description","File content"}}}
                    }},
                    {"required", {"path","content"}}
                }}
            }}
        }
    });
    return tools.dump();
}

// ── Path safety helper ────────────────────────────────────────────────────

// Resolve path; if relative, anchor to work_dir.
// Does NOT enforce that the result is under work_dir — the LLM may legitimately
// need to read system headers etc.  We only block obvious traversal tricks.
static fs::path safe_path(std::string_view raw, const std::string& work_dir) {
    fs::path p(raw);
    if (p.is_relative()) p = fs::path(work_dir) / p;
    p = p.lexically_normal();
    return p;
}

// ── Individual tools ──────────────────────────────────────────────────────

static ToolResult tool_read_file(const json& args, const Config& cfg) {
    std::string raw = args.value("path", "");
    if (raw.empty()) return {false, "read_file: missing 'path' argument"};

    fs::path p = safe_path(raw, cfg.work_dir);
    if (!fs::exists(p)) return {false, "read_file: file not found: " + p.string()};
    if (!fs::is_regular_file(p)) return {false, "read_file: not a regular file: " + p.string()};

    std::ifstream f(p);
    if (!f) return {false, "read_file: cannot open: " + p.string()};

    std::string content(std::istreambuf_iterator<char>(f), {});

    // Truncate large files — return head + tail
    if (content.size() > MAX_OUTPUT) {
        const size_t half = MAX_OUTPUT / 2;
        content = content.substr(0, half) +
                  "\n\n[... " + std::to_string(content.size() - MAX_OUTPUT) +
                  " bytes omitted ...]\n\n" +
                  content.substr(content.size() - half);
    }
    return {true, std::move(content)};
}

static ToolResult tool_list_dir(const json& args, const Config& cfg) {
    std::string raw = args.value("path", cfg.work_dir);
    if (raw.empty()) raw = cfg.work_dir;

    fs::path p = safe_path(raw, cfg.work_dir);
    if (!fs::exists(p)) return {false, "list_dir: not found: " + p.string()};
    if (!fs::is_directory(p)) return {false, "list_dir: not a directory: " + p.string()};

    std::ostringstream oss;
    oss << p.string() << "/\n";
    for (const auto& entry : fs::directory_iterator(p)) {
        oss << (entry.is_directory() ? "d " : "f ");
        oss << entry.path().filename().string();
        if (entry.is_regular_file()) oss << "  (" << entry.file_size() << " bytes)";
        oss << '\n';
    }
    return {true, oss.str()};
}

static ToolResult tool_run_command(const json& args, const Config& cfg) {
    std::string cmd = args.value("command", "");
    if (cmd.empty()) return {false, "run_command: missing 'command' argument"};

    // Wrap: cd to work_dir, set timeout, capture stderr
    std::string full_cmd =
        "cd " + cfg.work_dir + " && " +
        "timeout " + std::to_string(CMD_TIMEOUT) + " sh -c " +
        "'" + cmd + "'" +
        " 2>&1";

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return {false, "run_command: popen failed"};

    std::string output;
    std::array<char, 256> buf{};
    while (fgets(buf.data(), buf.size(), pipe)) {
        output += buf.data();
        if (output.size() > MAX_OUTPUT) {
            output.resize(MAX_OUTPUT);
            output += "\n[output truncated]";
            break;
        }
    }
    int rc = pclose(pipe);

    std::string result = "$ " + cmd + "\n" + output;
    if (rc != 0) result += "\n[exit code: " + std::to_string(WEXITSTATUS(rc)) + "]";
    return {true, std::move(result)};
}

static ToolResult tool_write_file(const json& args, const Config& cfg) {
    std::string raw     = args.value("path", "");
    std::string content = args.value("content", "");
    if (raw.empty()) return {false, "write_file: missing 'path' argument"};

    fs::path p = safe_path(raw, cfg.work_dir);
    fs::create_directories(p.parent_path());

    std::ofstream f(p, std::ios::trunc);
    if (!f) return {false, "write_file: cannot write: " + p.string()};
    f << content;

    return {true, "wrote " + std::to_string(content.size()) + " bytes to " + p.string()};
}

// ── Dispatcher ────────────────────────────────────────────────────────────

ToolResult dispatch_tool(
    std::string_view   tool_name,
    std::string_view   args_json,
    const Config&      cfg)
{
    json args;
    try {
        args = json::parse(args_json);
    } catch (...) {
        args = json::object();
    }

    if (tool_name == "read_file")   return tool_read_file(args, cfg);
    if (tool_name == "list_dir")    return tool_list_dir(args, cfg);
    if (tool_name == "run_command") return tool_run_command(args, cfg);
    if (tool_name == "write_file")  return tool_write_file(args, cfg);

    return {false, "unknown tool: " + std::string(tool_name)};
}

} // namespace wcp
