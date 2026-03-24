// wechat-copilot — RustCC C++17 edition
// adapter.cpp — iLink long-poll loop
//
// TCC-OWN:  Adapter owns Router and ILinkCreds.
// TCC-LIFE: Config reference must outlive start() — caller guarantees this.
// TCC-CONC: running_ is atomic; stop() writes from any thread safely.

#include "wcp/adapter.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

namespace wcp {

namespace {

constexpr int  MAX_FAILURES = 3;
constexpr auto BACKOFF      = std::chrono::seconds(30);
constexpr auto RETRY        = std::chrono::seconds(2);

bool is_allowed(const std::string& from_user, const Config& cfg) {
    if (cfg.allowed_from.empty()) return true;
    return std::find(cfg.allowed_from.begin(), cfg.allowed_from.end(), from_user)
           != cfg.allowed_from.end();
}

// Extract displayable text from a message (text items + auto-transcribed voice).
// Returns empty string if nothing usable.
std::string extract_text(const ILinkMessage& msg) {
    if (msg.message_type != 1) return {};  // 1 = MSG_TYPE_USER
    std::string parts;
    for (const auto& item : msg.items) {
        if (!item.text.empty()) {
            if (!parts.empty()) parts += '\n';
            if (item.type == 3) parts += "[voice] ";
            parts += item.text;
        }
    }
    return parts;
}

} // anonymous namespace

// ── Adapter ───────────────────────────────────────────────────────────────

Adapter::Adapter(const Config& cfg)
    : router_(std::make_unique<Router>(cfg))
{}

void Adapter::stop() noexcept {
    running_.store(false, std::memory_order_relaxed);
}

void Adapter::handle_message(
    const ILinkCreds&   creds,
    const ILinkMessage& msg,
    const Config&       cfg)
{
    if (!is_allowed(msg.from_user_id, cfg)) {
        std::cerr << "[adapter] blocked: " << msg.from_user_id << '\n';
        return;
    }

    const std::string text = extract_text(msg);
    if (text.empty()) return;

    std::cout << "[adapter] <- " << msg.from_user_id.substr(0, 16)
              << ": " << text.substr(0, 80) << '\n';

    std::string reply;
    try {
        reply = router_->route(text, cfg);
    } catch (const std::exception& e) {
        reply = std::string("Error: ") + e.what();
    }

    std::cout << "[adapter] -> reply (" << reply.size() << " chars)\n";

    auto rc = send_reply(creds, msg.from_user_id, reply, msg.context_token, cfg);
    if (!rc.ok)
        std::cerr << "[adapter] sendReply failed: " << rc.error << '\n';
}

void Adapter::start(const Config& cfg) {
    if (running_.exchange(true)) throw std::runtime_error("Adapter already running");

    ILinkCreds  creds    = load_creds(cfg);
    std::string sync_buf = load_sync_buf(cfg);
    int         failures = 0;

    std::cout << "[adapter] started, polling iLink...\n";

    while (running_.load(std::memory_order_relaxed)) {
        auto res = get_updates(creds, sync_buf, cfg);

        if (!res.ok) {
            ++failures;
            std::cerr << "[adapter] poll error (" << failures << '/'
                      << MAX_FAILURES << "): " << res.error << '\n';
            if (failures >= MAX_FAILURES) {
                std::cerr << "[adapter] too many failures, backing off\n";
                std::this_thread::sleep_for(BACKOFF);
                failures = 0;
            } else {
                std::this_thread::sleep_for(RETRY);
            }
            continue;
        }

        const auto& resp = res.value;

        if (resp.errcode != 0) {
            std::cerr << "[adapter] iLink errcode " << resp.errcode
                      << " — backing off\n";
            std::this_thread::sleep_for(BACKOFF);
            continue;
        }

        if (!resp.next_buf.empty() && resp.next_buf != sync_buf) {
            sync_buf = resp.next_buf;
            save_sync_buf(cfg, sync_buf);
        }

        for (const auto& msg : resp.msgs) {
            handle_message(creds, msg, cfg);
        }

        failures = 0;
    }

    std::cout << "[adapter] stopped\n";
}

} // namespace wcp
