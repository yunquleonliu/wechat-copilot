// wechat-copilot — RustCC C++17 edition
// ilink.hpp — WeChat iLink gateway types and API calls
//
// TCC-OWN:  ILinkCreds is a value type; callers own their copy.
// TCC-LIFE: string_view borrows from caller — only used within call scope.

#pragma once

#include "wcp/config.hpp"
#include "wcp/http.hpp"
#include <optional>
#include <string>
#include <vector>

namespace wcp {

// ── Data types ─────────────────────────────────────────────────────────────

struct ILinkCreds {
    std::string token;
    std::string base_url;
};

struct ILinkItem {
    int         type = 0;          // 1=text, 3=voice
    std::string text;              // extracted from text_item or voice_item
};

struct ILinkMessage {
    int         message_type  = 0; // 1=user, 2=bot
    std::string from_user_id;
    std::string context_token;
    std::vector<ILinkItem> items;
};

struct GetUpdatesResponse {
    int                         errcode = 0;
    std::vector<ILinkMessage>   msgs;
    std::string                 next_buf;
};

struct QRCodeInfo {
    std::string qr_url;
    std::string ticket;
    std::string token;
};

struct QRLoginStatus {
    int                     status = 0;
    std::optional<std::string> token;
};

// ── Credential persistence ─────────────────────────────────────────────────

// Load iLink credentials from cred_path.
// Throws std::runtime_error if file missing or malformed.
ILinkCreds load_creds(const Config& cfg);

// Load/save the sync_buf cursor that tracks the poll position.
std::string  load_sync_buf(const Config& cfg);
void         save_sync_buf(const Config& cfg, std::string_view buf);

// ── API calls ──────────────────────────────────────────────────────────────

// Long-poll for new messages. Blocks up to cfg.long_poll_ms.
// Returns empty msgs list on timeout (not an error).
Result<GetUpdatesResponse> get_updates(
    const ILinkCreds& creds,
    std::string_view  sync_buf,
    const Config&     cfg);

// Send a text reply to a user.
Result<void> send_reply(
    const ILinkCreds& creds,
    std::string_view  to_user,
    std::string_view  text,
    std::string_view  context_token,
    const Config&     cfg);

// QR login helpers (used by setup binary).
Result<QRCodeInfo>   get_bot_qrcode(std::string_view base_url, const Config& cfg);
Result<QRLoginStatus> poll_qr_login(std::string_view base_url,
                                    std::string_view ticket,
                                    const Config&    cfg);

} // namespace wcp
