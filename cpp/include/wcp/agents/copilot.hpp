// wechat-copilot — RustCC C++17 edition
// agents/copilot.hpp — GitHub Copilot API agent
//
// TCC-OWN:  CopilotAgent owns its history (vector of owned strings).
// TCC-LIFE: query() borrows prompt by value (cheap, avoids lifetime issues).
// TCC-CONC: single-threaded agent; each WeChat session owns its own instance.

#pragma once

#include "wcp/config.hpp"
#include "wcp/http.hpp"
#include <string>
#include <vector>

namespace wcp {

class CopilotAgent {
public:
    explicit CopilotAgent(const Config& cfg);

    // Append user message, call Copilot API, append assistant reply.
    // Returns reply text or error string.
    // TCC-LIFE: cfg borrowed for the duration of the call only.
    std::string query(std::string prompt, const Config& cfg);

    // Clear conversation history.
    void reset() noexcept;

    // Status summary for /status command.
    std::string status(const Config& cfg) const;

private:
    struct Message {
        std::string role;    // "system" | "user" | "assistant"
        std::string content;
    };

    // TCC-OWN: owned history; trimmed when it exceeds cfg.max_history
    std::vector<Message> history_;
    std::string          token_;   // GitHub OAuth token; loaded once in ctor

    static std::string load_token();
    static std::string system_prompt();
};

} // namespace wcp
