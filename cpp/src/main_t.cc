// wechat-copilot — RustCC C++17 edition
// main.cpp — entry point
//
// TCC-OWN:  Config and Adapter are stack-owned; no naked new.
// TCC-LIFE: cfg outlives adapter (declared first, destroyed last).
// TCC-CONC: signal handler writes atomic via stop(); safe.

#include "wcp/adapter.hpp"
#include "wcp/config.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
// TCC-CONC: std::atomic<T*> — safe to write from signal handler, read from main.
std::atomic<wcp::Adapter*> g_adapter{nullptr};

void signal_handler(int /*sig*/) {
    auto* a = g_adapter.load();
    if (a) a->stop();
}
} // anonymous namespace

int main() {
    std::cout << "wechat-copilot (C++17 RustCC edition) starting...\n";

    // TCC-OWN: cfg owned on stack; never heap-allocated
    wcp::Config cfg = wcp::Config::from_env();

    wcp::Adapter adapter(cfg);  // TCC-OWN: stack-owned

    g_adapter.store(&adapter);  // non-owning borrow for signal handler
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        adapter.start(cfg);  // blocks until stop()
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
