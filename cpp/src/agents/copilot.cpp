// wechat-copilot — RustCC C++17 edition
// agents/copilot.cpp — GitHub Copilot API with agentic tool-use loop
//
// Flow:
//   1. Send messages + tool_definitions to Copilot API
//   2. If finish_reason == "tool_calls": execute each tool locally, append
//      assistant tool_calls message + tool result messages, go to 1
//   3. If finish_reason == "stop": return final text to caller
//
// TCC-OWN:  history_ owned; tool results appended as owned messages.
// TCC-LIFE: token_ immutable after ctor; cfg borrowed per call.
// TCC-CONC: single-threaded per agent instance; Router ensures sequential calls.

#include "wcp/agents/copilot.hpp"
#include "wcp/http.hpp"
#include "wcp/tools.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace wcp {

using json = nlohmann::json;

static constexpr int MAX_TOOL_ROUNDS = 10;  // guard against infinite loops

// ── static helpers ─────────────────────────────────────────────────────────

std::string CopilotAgent::load_token() {
    const char* env = std::getenv("GITHUB_TOKEN");
    if (env && *env) return env;

    const char* home = std::getenv("HOME");
    if (!home) throw std::runtime_error("No GitHub token: set GITHUB_TOKEN");

    std::ifstream f(std::string(home) + "/.git-credentials");
    if (!f) throw std::runtime_error("No GitHub token: set GITHUB_TOKEN");

    std::string line;
    const std::regex pattern(R"(https://\d+:(gho_[^@\s]+)@github\.com)");
    while (std::getline(f, line)) {
        std::smatch m;
        if (std::regex_search(line, m, pattern)) return m[1];
    }
    throw std::runtime_error("No GitHub token found in ~/.git-credentials");
}

std::string CopilotAgent::system_prompt() {
    return
        "You are an agentic coding assistant for the RustCC profiler project, "
        "running on the local machine and accessible via WeChat. "
        "You have tools to read files, list directories, run shell commands, "
        "and write files — all executing locally on the server. "
        "Use tools proactively: when asked about code, builds, tests, or errors, "
        "read the relevant files and run the relevant commands instead of guessing. "
        "RustCC rules: enforce ownership (TCC-OWN), lifetime (TCC-LIFE), "
        "concurrency (TCC-CONC) semantics in all C++ code you produce. "
        "Reply in plain text only — no markdown, WeChat does not render it. Be concise.";
}

// ── CopilotAgent ───────────────────────────────────────────────────────────

CopilotAgent::CopilotAgent(const Config& /*cfg*/)
    : token_(load_token())
{}

void CopilotAgent::reset() noexcept { history_.clear(); }

std::string CopilotAgent::status(const Config& cfg) const {
    std::ostringstream oss;
    oss << "Copilot: " << cfg.copilot_model
        << " | history=" << history_.size()
        << " | WORK_DIR=" << cfg.work_dir;
    return oss.str();
}

// ── Agentic query loop ────────────────────────────────────────────────────

std::string CopilotAgent::query(std::string prompt, const Config& cfg) {
    history_.push_back({"user", prompt, {}, {}});
    if (static_cast<int>(history_.size()) > cfg.max_history)
        history_.erase(history_.begin(),
                       history_.begin() + (history_.size() - cfg.max_history));

    // Build request headers
    std::vector<std::string> headers = {
        "Content-Type: application/json",
        std::string("Authorization: Bearer ") + token_,
        "Copilot-Integration-Id: vscode-chat",
        "Editor-Version: vscode/1.95.0",
    };

    // Parse tool definitions once
    json tools_json = json::parse(tool_definitions_json());

    for (int round = 0; round < MAX_TOOL_ROUNDS; ++round) {

        // Build messages array from history
        json messages = json::array();
        messages.push_back({{"role", "system"}, {"content", system_prompt()}});

        for (const auto& m : history_) {
            if (m.role == "tool") {
                // Tool result message
                json tmsg;
                tmsg["role"]         = "tool";
                tmsg["tool_call_id"] = m.tool_call_id;
                tmsg["content"]      = m.content;
                messages.push_back(std::move(tmsg));
            } else if (!m.tool_calls.empty()) {
                // Assistant message that contained tool calls
                json amsg;
                amsg["role"]       = "assistant";
                amsg["content"]    = nullptr;
                amsg["tool_calls"] = m.tool_calls;
                messages.push_back(std::move(amsg));
            } else {
                messages.push_back({{"role", m.role}, {"content", m.content}});
            }
        }

        json body_json;
        body_json["model"]    = cfg.copilot_model;
        body_json["messages"] = std::move(messages);
        body_json["tools"]    = tools_json;
        body_json["stream"]   = false;
        std::string body = body_json.dump();

        auto res = http_post(
            cfg.copilot_api + "/chat/completions",
            headers, body, cfg.query_timeout_ms);

        if (!res.ok) return "Copilot error: " + res.error;
        if (res.value.status_code != 200) {
            std::cerr << "[copilot] 400 body: " << res.value.body << '\n';
            std::cerr << "[copilot] full request:\n" << body << '\n';
            return "Copilot HTTP " + std::to_string(res.value.status_code) +
                   ": " + res.value.body;
        }

        auto j = json::parse(res.value.body, nullptr, false);
        if (j.is_discarded()) return "Copilot: invalid JSON response";

        const auto& choice      = j.at("choices").at(0);
        std::string finish      = choice.value("finish_reason", "stop");
        const auto& msg         = choice.at("message");

        // ── Tool call round ───────────────────────────────────────────────
        if (finish == "tool_calls") {
            const auto& tool_calls = msg.at("tool_calls");

            // Store the assistant's tool_calls message in history
            Message asst_msg;
            asst_msg.role       = "assistant";
            asst_msg.content    = "";
            asst_msg.tool_calls = tool_calls;
            history_.push_back(std::move(asst_msg));

            // Execute each tool and store results
            for (const auto& tc : tool_calls) {
                std::string tc_id   = tc.value("id", "");
                std::string tc_name = tc.at("function").value("name", "");
                std::string tc_args = tc.at("function").value("arguments", "{}");

                std::cout << "[tool] calling " << tc_name
                          << "(" << tc_args.substr(0, 80) << ")\n";

                ToolResult result = dispatch_tool(tc_name, tc_args, cfg);

                std::string output = result.ok
                    ? result.output
                    : "ERROR: " + result.output;

                std::cout << "[tool] result: " << output.substr(0, 80) << "...\n";

                Message tool_msg;
                tool_msg.role         = "tool";
                tool_msg.content      = std::move(output);
                tool_msg.tool_call_id = tc_id;
                history_.push_back(std::move(tool_msg));
            }

            // Loop: send tool results back to LLM
            continue;
        }

        // ── Final answer ──────────────────────────────────────────────────
        std::string reply;
        try {
            reply = msg.at("content").get<std::string>();
            auto s = reply.find_first_not_of(" \t\r\n");
            auto e = reply.find_last_not_of(" \t\r\n");
            if (s != std::string::npos) reply = reply.substr(s, e - s + 1);
        } catch (...) {
            reply = "(no response)";
        }

        history_.push_back({"assistant", reply, {}, {}});
        return reply;
    }

    return "Agent exceeded maximum tool rounds. Please rephrase your request.";
}

} // namespace wcp

