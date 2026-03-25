// wechat-copilot — RustCC C++17 edition
// config.cpp

#include "wcp/config.hpp"

#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace wcp {

// ── helpers ────────────────────────────────────────────────────────────────

std::string Config::env_or(const char* name, std::string fallback) {
    const char* val = std::getenv(name);
    return (val && *val) ? std::string(val) : std::move(fallback);
}

std::string Config::expand_home(std::string path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

// ── factory ────────────────────────────────────────────────────────────────

Config Config::from_env() {
    Config c;

    c.copilot_model = env_or("COPILOT_MODEL", "claude-sonnet-4.6");

    c.omnicode_url   = env_or("OMNICODE_URL",   "http://localhost:8081/v1");
    c.omnicode_model = env_or("OMNICODE_MODEL", "omnicode-9b");
    c.gemma_url      = env_or("GEMMA_URL",      "http://localhost:8080/v1");
    c.gemma_model    = env_or("GEMMA_MODEL",    "gemma2-9b");
    c.vscode_url     = env_or("VSCODE_AGENT_URL", "http://127.0.0.1:9191");

    c.cred_path = expand_home(
        env_or("WECHAT_CRED_PATH", "~/.claude/channels/wechat/account.json"));
    c.sync_path = expand_home(
        env_or("WECHAT_SYNC_PATH", "~/.claude/channels/wechat/sync_buf.txt"));

    // Work dir for tool execution: default to cwd if not set
    const char* wd = std::getenv("WORK_DIR");
    if (wd && *wd) {
        c.work_dir = wd;
    } else {
        char cwd_buf[4096];
        c.work_dir = (getcwd(cwd_buf, sizeof(cwd_buf))) ? cwd_buf : ".";
    }

    // Allowed-from: comma-separated list
    const char* af = std::getenv("WECHAT_ALLOWED_FROM");
    if (af && *af) {
        std::istringstream ss(af);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            // trim spaces
            auto start = tok.find_first_not_of(" \t");
            auto end   = tok.find_last_not_of(" \t");
            if (start != std::string::npos)
                c.allowed_from.emplace_back(tok.substr(start, end - start + 1));
        }
    }

    const char* tms = std::getenv("QUERY_TIMEOUT_MS");
    if (tms && *tms) c.query_timeout_ms = std::chrono::milliseconds(std::stol(tms));

    return c;
}

} // namespace wcp
