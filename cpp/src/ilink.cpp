// wechat-copilot — RustCC C++17 edition
// ilink.cpp — WeChat iLink gateway implementation
//
// TCC-OWN:  All heap strings returned by value (move).
// TCC-LIFE: json objects are temporaries; fields extracted before scope ends.
// TCC-CONC: All functions are stateless; thread-safe by design.

#include "wcp/ilink.hpp"
#include "wcp/http.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace wcp {

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── helpers ────────────────────────────────────────────────────────────────

namespace {

std::string random_hex(size_t bytes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    std::string out;
    out.reserve(bytes * 2);
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t b = dist(gen);
        out += hex[b >> 4];
        out += hex[b & 0xf];
    }
    return out;
}

// Build iLink auth headers (shared by all API calls).
std::vector<std::string> ilink_headers(const std::string& token, size_t body_len) {
    return {
        "Content-Type: application/json",
        std::string("Authorization: Bearer ") + token,
        "AuthorizationType: ilink_bot_token",
        std::string("Content-Length: ") + std::to_string(body_len),
    };
}

// Parse a single item from item_list; returns ILinkItem.
ILinkItem parse_item(const json& j) {
    ILinkItem item;
    item.type = j.value("type", 0);
    if (item.type == 1) {  // MSG_ITEM_TEXT
        item.text = j.value(json::json_pointer("/text_item/text"), std::string{});
    } else if (item.type == 3) {  // MSG_ITEM_VOICE (iLink auto-transcribes)
        item.text = j.value(json::json_pointer("/voice_item/text"), std::string{});
    }
    return item;
}

} // anonymous namespace

// ── credential persistence ─────────────────────────────────────────────────

ILinkCreds load_creds(const Config& cfg) {
    if (!fs::exists(cfg.cred_path)) {
        throw std::runtime_error(
            "No WeChat credentials at " + cfg.cred_path +
            ". Run: ./wcp-setup");
    }
    std::ifstream f(cfg.cred_path);
    auto j = json::parse(f);
    ILinkCreds creds;
    creds.token    = j.at("token").get<std::string>();
    creds.base_url = j.value("baseUrl", "https://ilinkai.weixin.qq.com");
    return creds;
}

std::string load_sync_buf(const Config& cfg) {
    std::ifstream f(cfg.sync_path);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), {});
}

void save_sync_buf(const Config& cfg, std::string_view buf) {
    fs::create_directories(fs::path(cfg.sync_path).parent_path());
    std::ofstream f(cfg.sync_path, std::ios::trunc);
    f << buf;
}

// ── get_updates ────────────────────────────────────────────────────────────

Result<GetUpdatesResponse> get_updates(
    const ILinkCreds& creds,
    std::string_view  sync_buf,
    const Config&     cfg)
{
    json payload;
    payload["get_updates_buf"] = std::string(sync_buf);
    std::string body = payload.dump();

    auto res = http_post(
        creds.base_url + "/ilink/bot/getupdates",
        ilink_headers(creds.token, body.size()),
        body,
        cfg.long_poll_ms);

    if (!res.ok) {
        // Timeout maps to CURLE_OPERATION_TIMEDOUT — treat as empty poll
        if (res.error.find("Timeout") != std::string::npos ||
            res.error.find("timed out") != std::string::npos) {
            return Result<GetUpdatesResponse>::success({0, {}, std::string(sync_buf)});
        }
        return Result<GetUpdatesResponse>::failure(std::move(res.error));
    }

    auto j = json::parse(res.value.body, nullptr, false);
    if (j.is_discarded())
        return Result<GetUpdatesResponse>::failure("iLink: invalid JSON response");

    GetUpdatesResponse out;
    out.errcode  = j.value("errcode", 0);
    out.next_buf = j.value("get_updates_buf", std::string(sync_buf));

    for (const auto& raw : j.value("msgs", json::array())) {
        ILinkMessage msg;
        msg.message_type  = raw.value("message_type", 0);
        msg.from_user_id  = raw.value("from_user_id",  "");
        msg.context_token = raw.value("context_token", "");
        for (const auto& item : raw.value("item_list", json::array()))
            msg.items.push_back(parse_item(item));
        out.msgs.push_back(std::move(msg));
    }

    return Result<GetUpdatesResponse>::success(std::move(out));
}

// ── send_reply ─────────────────────────────────────────────────────────────

Result<void> send_reply(
    const ILinkCreds& creds,
    std::string_view  to_user,
    std::string_view  text,
    std::string_view  context_token,
    const Config&     cfg)
{
    std::string client_id = "wcp-" + random_hex(8);

    json payload;
    payload["msg"] = {
        {"from_user_id",  ""},
        {"to_user_id",    std::string(to_user)},
        {"client_id",     client_id},
        {"message_type",  2},
        {"message_state", 2},
        {"context_token", std::string(context_token)},
        {"item_list", json::array({
            {{"type", 1}, {"text_item", {{"text", std::string(text)}}}}
        })},
    };
    std::string body = payload.dump();

    auto res = http_post(
        creds.base_url + "/ilink/bot/sendmessage",
        ilink_headers(creds.token, body.size()),
        body,
        cfg.api_timeout_ms);

    if (!res.ok) return Result<void>::failure(std::move(res.error));
    if (res.value.status_code != 200)
        return Result<void>::failure(
            "sendMessage HTTP " + std::to_string(res.value.status_code) +
            ": " + res.value.body);

    return Result<void>::success();
}

// ── QR login helpers ───────────────────────────────────────────────────────

Result<QRCodeInfo> get_bot_qrcode(std::string_view base_url, const Config& cfg) {
    std::string body = R"({"bot_type":3})";
    auto res = http_post(
        std::string(base_url) + "/ilink/bot/get_bot_qrcode",
        {"Content-Type: application/json"},
        body,
        cfg.api_timeout_ms);

    if (!res.ok) return Result<QRCodeInfo>::failure(std::move(res.error));

    auto j = json::parse(res.value.body, nullptr, false);
    if (j.is_discarded()) return Result<QRCodeInfo>::failure("Invalid JSON");

    QRCodeInfo info;
    info.qr_url = j.value("qr_url", "");
    info.ticket = j.value("ticket", "");
    info.token  = j.value("token",  "");
    return Result<QRCodeInfo>::success(std::move(info));
}

Result<QRLoginStatus> poll_qr_login(
    std::string_view base_url,
    std::string_view ticket,
    const Config&    cfg)
{
    json payload;
    payload["ticket"] = std::string(ticket);
    std::string body = payload.dump();

    auto res = http_post(
        std::string(base_url) + "/ilink/bot/query_qrcode_status",
        {"Content-Type: application/json"},
        body,
        cfg.api_timeout_ms);

    if (!res.ok) return Result<QRLoginStatus>::failure(std::move(res.error));

    auto j = json::parse(res.value.body, nullptr, false);
    if (j.is_discarded()) return Result<QRLoginStatus>::failure("Invalid JSON");

    QRLoginStatus s;
    s.status = j.value("status", 0);
    if (j.contains("token") && !j["token"].is_null())
        s.token = j["token"].get<std::string>();
    return Result<QRLoginStatus>::success(std::move(s));
}

} // namespace wcp
