// wechat-copilot — RustCC C++17 edition
// agents/copilot.hpp — GitHub Copilot API agent with agentic tool-use loop
//
// TCC-OWN:  CopilotAgent owns its history (vector of owned Message objects).
// TCC-LIFE: query() borrows cfg for the duration of the call only.
// TCC-CONC: single-threaded agent; Router ensures sequential calls.

#pragma once

#include "wcp/config.hpp"
#include "wcp/http.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace wcp {

class CopilotAgent {
public:
    explicit CopilotAgent(const Config& cfg);

    // Agentic query: may call local tools multiple times before returning.
    // Returns final reply text or error string.
    std::string query(std::string prompt, const Config& cfg);

    void reset() noexcept;
    std::string status(const Config& cfg) const;

private:
    // Unified message type covering all roles in the tool-use protocol.
    // TCC-OWN: tool_calls is nlohmann::json (owned value), never a view.
    struct Message {
        std::string role;          // "user" | "assistant" | "tool"
        std::string content;
        nlohmann::json tool_calls; // non-null only for assistant tool-call turns
        std::string tool_call_id;  // non-empty only for role=="tool"
    };

    // TCC-OWN: history owned; trimmed to cfg.max_history each query
    std::vector<Message> history_;
    std::string          token_;

    static std::string load_token();
    static std::string system_prompt();
};

} // namespace wcp
