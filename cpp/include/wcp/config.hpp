// wechat-copilot — RustCC C++17 edition
// config.hpp — environment configuration (TCC-OWN: all values owned by Config)
//
// All configuration is loaded once at startup into a const Config object.
// Callers borrow via const-ref; no mutation after construction.

#pragma once

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

namespace wcp {

// Immutable configuration loaded from environment variables.
// TCC-OWN: constructed once, passed by const-ref (borrow), never mutated.
struct Config {
    // GitHub Copilot
    std::string copilot_api   = "https://api.githubcopilot.com";
    std::string copilot_model = "claude-sonnet-4.6";

    // Local sidecar endpoints
    std::string omnicode_url   = "http://localhost:8081/v1";
    std::string omnicode_model = "omnicode-9b";
    std::string gemma_url      = "http://localhost:8080/v1";
    std::string gemma_model    = "gemma2-9b";

    // iLink credential file paths
    std::string cred_path;    // ~/.claude/channels/wechat/account.json
    std::string sync_path;    // ~/.claude/channels/wechat/sync_buf.txt

    // Access control: empty = allow all
    std::vector<std::string> allowed_from;

    // Timeouts
    std::chrono::milliseconds query_timeout_ms   {180'000};
    std::chrono::milliseconds long_poll_ms        { 35'000};
    std::chrono::milliseconds api_timeout_ms      { 15'000};

    int max_history = 20;

    // Factory — reads env, expands ~ paths
    static Config from_env();

private:
    static std::string env_or(const char* name, std::string fallback);
    static std::string expand_home(std::string path);
};

} // namespace wcp
