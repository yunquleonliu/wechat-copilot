// wechat-copilot — RustCC C++17 edition
// setup.cpp — WeChat QR login setup binary (run once to get credentials)

#include "wcp/config.hpp"
#include "wcp/ilink.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

int main() {
    wcp::Config cfg = wcp::Config::from_env();
    const std::string base_url = "https://ilinkai.weixin.qq.com";

    std::cout << "Fetching WeChat QR code...\n";
    auto qr_res = wcp::get_bot_qrcode(base_url, cfg);
    if (!qr_res.ok) {
        std::cerr << "Error: " << qr_res.error << '\n';
        return 1;
    }

    std::cout << "Scan this QR code with WeChat:\n"
              << qr_res.value.qr_url << "\n\n"
              << "Waiting for scan...\n";

    const std::string ticket = qr_res.value.ticket;
    std::string       token;

    for (int i = 0; i < 120; ++i) {  // poll up to 2 minutes
        std::this_thread::sleep_for(std::chrono::seconds(2));

        auto st = wcp::poll_qr_login(base_url, ticket, cfg);
        if (!st.ok) {
            std::cerr << "Poll error: " << st.error << '\n';
            continue;
        }

        if (st.value.status == 2 && st.value.token) {
            token = *st.value.token;
            break;
        }
        if (st.value.status == 1) {
            std::cout << "  📱 Scanned! Confirm in WeChat...\r" << std::flush;
        } else {
            std::cout << "  Waiting for scan...\r" << std::flush;
        }
    }

    if (token.empty()) {
        std::cerr << "Login timed out. Please run setup again.\n";
        return 1;
    }

    // Save credentials
    fs::create_directories(fs::path(cfg.cred_path).parent_path());
    json creds;
    creds["token"]   = token;
    creds["baseUrl"] = base_url;
    std::ofstream f(cfg.cred_path);
    f << creds.dump(2) << '\n';

    std::cout << "\nLogin successful! Credentials saved to:\n  " << cfg.cred_path << '\n';
    std::cout << "\nYour iLink user ID (for WECHAT_ALLOWED_FROM):\n"
              << "  Run wcp-bridge and send a message — it will print your ID.\n";

    return 0;
}
