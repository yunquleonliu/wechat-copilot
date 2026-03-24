// wechat-copilot — RustCC C++17 edition
// adapter.hpp — iLink long-poll loop
//
// TCC-OWN:  Adapter owns Router (unique_ptr) and ILinkCreds (value).
// TCC-LIFE: start() blocks the calling thread; stop() is safe from any thread.
// TCC-CONC: running_ is std::atomic<bool>; stop() is thread-safe.

#pragma once

#include "wcp/config.hpp"
#include "wcp/ilink.hpp"
#include "wcp/router.hpp"
#include <atomic>
#include <memory>

namespace wcp {

class Adapter {
public:
    explicit Adapter(const Config& cfg);

    // Block and run the poll loop until stop() is called.
    // Throws std::runtime_error if credentials cannot be loaded.
    void start(const Config& cfg);

    // Signal the poll loop to exit. Thread-safe.
    // TCC-CONC: atomic store; no lock needed.
    void stop() noexcept;

private:
    // TCC-OWN: unique_ptr; Router must outlive Adapter (guaranteed here)
    std::unique_ptr<Router> router_;

    // TCC-CONC: atomic flag — written by stop() (any thread), read by start() loop
    std::atomic<bool> running_{false};

    void handle_message(
        const ILinkCreds&   creds,
        const ILinkMessage& msg,
        const Config&       cfg);
};

} // namespace wcp
