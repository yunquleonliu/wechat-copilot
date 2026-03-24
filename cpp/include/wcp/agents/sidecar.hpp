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

} // namespace wcp
