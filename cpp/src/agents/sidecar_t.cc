// wechat-copilot — RustCC C++17 edition
// agents/sidecar.cpp — local LLM sidecar agents (OmniCode, Gemma)
//
// Stateless: no history, no mutable state.
// TCC-CONC: safe to call from multiple threads.

#include "wcp/agents/sidecar.hpp"
#include "wcp/http.hpp"
#include "wcp/json_utils.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <string>

namespace wcp {

using json = nlohmann::json;
namespace ju  = json_util;

namespace {

// Shared implementation for any OpenAI-compat endpoint.
// TCC-LIFE: url, model borrowed — used only within this call.
std::string query_llm(
    std::string_view  url,
    std::string_view  model,
    std::string       prompt,
    std::chrono::milliseconds timeout)
{
    json body_json;
    body_json["model"]    = std::string(model);
    body_json["messages"] = ju::array({
        {{"role", "user"}, {"content", std::move(prompt)}}
    });
    body_json["stream"] = false;
    std::string body = body_json.dump();

    auto res = http_post(
        url,
        {"Content-Type: application/json"},
        body,
        timeout);

    if (!res.ok) return "Sidecar error: " + res.error;
    if (res.value.status_code != 200)
        return "Sidecar HTTP " + std::to_string(res.value.status_code);

    auto j = ju::parse(res.value.body);
    if (j.is_discarded()) return "(invalid JSON from sidecar)";

    try {
        auto reply = j.at("choices").at(0).at("message").at("content")
                      .get<std::string>();
        auto s = reply.find_first_not_of(" \t\r\n");
        auto e = reply.find_last_not_of(" \t\r\n");
        if (s != std::string::npos) return reply.substr(s, e - s + 1);
        return reply;
    } catch (...) {
        return "(no response)";
    }
}

} // anonymous namespace

std::string query_omnicode(std::string prompt, const Config& cfg) {
    return query_llm(
        cfg.omnicode_url + "/chat/completions",
        cfg.omnicode_model,
        std::move(prompt),
        cfg.api_timeout_ms * 8);
}

std::string query_gemma(std::string prompt, const Config& cfg) {
    return query_llm(
        cfg.gemma_url + "/chat/completions",
        cfg.gemma_model,
        std::move(prompt),
        cfg.api_timeout_ms * 8);
}

std::string query_vscode(std::string prompt, const Config& cfg,
                         std::string_view user_id) {
    json body_json;
    body_json["text"]    = std::move(prompt);
    body_json["user_id"] = user_id.empty() ? "default" : std::string(user_id);
    std::string body  = body_json.dump();

    auto res = http_post(
        cfg.vscode_url + "/chat",
        {"Content-Type: application/json"},
        body,
        cfg.query_timeout_ms);

    if (!res.ok) return "VSCode agent error: " + res.error;
    if (res.value.status_code != 200)
        return "VSCode agent HTTP " + std::to_string(res.value.status_code);

    auto j = ju::parse(res.value.body);
    if (j.is_discarded()) return "(invalid JSON from vsc-agent)";

    try {
        return j.at("reply").get<std::string>();
    } catch (...) {
        // Fallback: surface raw body for debugging
        return res.value.body.substr(0, 512);
    }
}

} // namespace wcp
