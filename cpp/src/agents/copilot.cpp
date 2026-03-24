// wechat-copilot — RustCC C++17 edition
// agents/copilot.cpp — GitHub Copilot API (OpenAI-compat)
//
// Uses GitHub OAuth token from ~/.git-credentials (same account as VSCode).
// TCC-OWN:  history_ is owned; trimmed when it exceeds max_history.
// TCC-LIFE: token_ loaded once in ctor; immutable thereafter.

#include "wcp/agents/copilot.hpp"
#include "wcp/http.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace wcp {

using json = nlohmann::json;

// ── static helpers ─────────────────────────────────────────────────────────

std::string CopilotAgent::load_token() {
    // 1. Environment variable takes priority
    const char* env = std::getenv("GITHUB_TOKEN");
    if (env && *env) return env;

    // 2. Parse ~/.git-credentials
    const char* home = std::getenv("HOME");
    if (!home) throw std::runtime_error("No GitHub token: set GITHUB_TOKEN");

    std::ifstream f(std::string(home) + "/.git-credentials");
    if (!f) throw std::runtime_error("No GitHub token: set GITHUB_TOKEN");

    std::string line;
    // TCC-LIFE: regex is stack-local; match refers to line (same scope)
    const std::regex pattern(R"(https://\d+:(gho_[^@\s]+)@github\.com)");
    while (std::getline(f, line)) {
        std::smatch m;
        if (std::regex_search(line, m, pattern)) return m[1];
    }
    throw std::runtime_error("No GitHub token found in ~/.git-credentials");
}

std::string CopilotAgent::system_prompt() {
    return
        "You are a coding agent for the RustCC profiler project, accessible via WeChat. "
        "You have full access to the local filesystem, build tools, git, and the RustCC profiler. "
        "RustCC rules: enforce ownership (TCC-OWN), lifetime (TCC-LIFE), concurrency (TCC-CONC) semantics. "
        "Reply in plain text only — no markdown, WeChat does not render it. Be concise.";
}

// ── CopilotAgent ───────────────────────────────────────────────────────────

CopilotAgent::CopilotAgent(const Config& /*cfg*/)
    : token_(load_token())
{}

void CopilotAgent::reset() noexcept {
    history_.clear();
}

std::string CopilotAgent::status(const Config& cfg) const {
    std::ostringstream oss;
    oss << "Copilot: " << cfg.copilot_model
        << " via api.githubcopilot.com | history=" << history_.size();
    return oss.str();
}

std::string CopilotAgent::query(std::string prompt, const Config& cfg) {
    history_.push_back({"user", std::move(prompt)});

    // Trim history — keep last max_history entries
    if (static_cast<int>(history_.size()) > cfg.max_history)
        history_.erase(history_.begin(),
                       history_.begin() + (history_.size() - cfg.max_history));

    // Build messages array: system + history
    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt()}});
    for (const auto& m : history_)
        messages.push_back({{"role", m.role}, {"content", m.content}});

    json body_json;
    body_json["model"]    = cfg.copilot_model;
    body_json["messages"] = std::move(messages);
    body_json["stream"]   = false;
    std::string body = body_json.dump();

    std::vector<std::string> headers = {
        "Content-Type: application/json",
        std::string("Authorization: Bearer ") + token_,
        "Copilot-Integration-Id: vscode-chat",
        "Editor-Version: vscode/1.95.0",
    };

    auto res = http_post(
        cfg.copilot_api + "/chat/completions",
        headers,
        body,
        cfg.query_timeout_ms);

    if (!res.ok) return "Copilot error: " + res.error;
    if (res.value.status_code != 200)
        return "Copilot HTTP " + std::to_string(res.value.status_code) +
               ": " + res.value.body;

    auto j = json::parse(res.value.body, nullptr, false);
    if (j.is_discarded()) return "Copilot: invalid JSON response";

    std::string reply;
    try {
        reply = j.at("choices").at(0).at("message").at("content").get<std::string>();
        // Trim whitespace
        auto s = reply.find_first_not_of(" \t\r\n");
        auto e = reply.find_last_not_of(" \t\r\n");
        if (s != std::string::npos) reply = reply.substr(s, e - s + 1);
    } catch (...) {
        reply = "(no response)";
    }

    history_.push_back({"assistant", reply});
    return reply;
}

} // namespace wcp
