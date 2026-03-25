// wechat-copilot — RustCC C++17 edition
// router.cpp — prefix-based message dispatch
//
// TCC-OWN:  copilot_ is unique_ptr — sole owner, transfer on move only.
// TCC-LIFE: text and cfg borrowed for the call duration; no storage.

#include "wcp/router.hpp"
#include "wcp/agents/sidecar.hpp"

#include <algorithm>
#include <sstream>

namespace wcp {

Router::Router(const Config& cfg)
    : copilot_(std::make_unique<CopilotAgent>(cfg))
{}

std::string Router::route(std::string_view text_view, const Config& cfg,
                          std::string_view user_id) {
    auto start = text_view.find_first_not_of(" \t\r\n");
    auto end   = text_view.find_last_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "Empty message.";
    std::string text(text_view.substr(start, end - start + 1));

    // ── built-in commands ──────────────────────────────────────────────────

    if (text == "/status") {
        std::ostringstream oss;
        oss << "wechat-copilot (C++17 RustCC edition)\n"
            << "  " << copilot_->status(cfg) << "\n"
            << "  OmniCode: " << cfg.omnicode_url << "\n"
            << "  Gemma:    " << cfg.gemma_url;
        return oss.str();
    }

    if (text == "/reset") {
        copilot_->reset();
        return "Copilot session reset. Starting fresh.";
    }

    if (text == "/help") {
        return
            "wechat-copilot commands:\n"
            "  (message)     -> VSCode agent (gpt-4o, full tools)\n"
            "  /vsc  <msg>   -> VSCode agent (explicit)\n"
            "  /code <msg>   -> OmniCode 9B (local :8081)\n"
            "  /ask  <msg>   -> Gemma 9B (local :8080)\n"
            "  /status       -> system status\n"
            "  /reset        -> reset conversation history";
    }

    // ── sidecar routing ────────────────────────────────────────────────────

    if (text.size() > 6 && text.substr(0, 6) == "/code ") {
        auto prompt = text.substr(6);
        auto s = prompt.find_first_not_of(' ');
        if (s == std::string::npos) return "Usage: /code <question>";
        return "[OmniCode]\n" + query_omnicode(prompt.substr(s), cfg);
    }

    if (text.size() > 5 && text.substr(0, 5) == "/ask ") {
        auto prompt = text.substr(5);
        auto s = prompt.find_first_not_of(' ');
        if (s == std::string::npos) return "Usage: /ask <question>";
        return "[Gemma]\n" + query_gemma(prompt.substr(s), cfg);
    }

    if (text.size() > 5 && text.substr(0, 5) == "/vsc ") {
        auto prompt = text.substr(5);
        auto s = prompt.find_first_not_of(' ');
        if (s == std::string::npos) return "Usage: /vsc <question>";
        return "[VSCode]\n" + query_vscode(prompt.substr(s), cfg, user_id);
    }

    // ── default: VSCode agent ──────────────────────────────────────────────
    return "[VSCode]\n" + query_vscode(std::move(text), cfg, user_id);
}

} // namespace wcp
