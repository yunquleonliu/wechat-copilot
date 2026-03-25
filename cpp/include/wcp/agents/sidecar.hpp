// wechat-copilot — RustCC C++17 edition
// agents/sidecar.hpp — local LLM sidecar agents (OmniCode, Gemma)
//
// Stateless functions: each call is independent (no history).
// TCC-OWN: no heap allocation beyond string returns.
// TCC-CONC: safe to call from any thread concurrently.

#pragma once

#include "wcp/config.hpp"
#include <string>

namespace wcp {

// POST to OmniCode 9B on :8081 — for /code prefix
std::string query_omnicode(std::string prompt, const Config& cfg);

// POST to Gemma 9B on :8080 — for /ask prefix
std::string query_gemma(std::string prompt, const Config& cfg);

// POST to VSCode agent bridge on :9191 — for /vsc prefix
// vsc-agent.py must be running: python3 vsc-agent.py
std::string query_vscode(std::string prompt, const Config& cfg,
                         std::string_view user_id = "");

} // namespace wcp
