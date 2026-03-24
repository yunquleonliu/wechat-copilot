// wechat-copilot — RustCC C++17 edition
// router.hpp — prefix-based message dispatch
//
// TCC-OWN:  Router owns its CopilotAgent (unique_ptr).
// TCC-LIFE: route() borrows text and cfg for the duration of the call.
// TCC-CONC: Router is single-threaded; each message processed sequentially.

#pragma once

#include "wcp/config.hpp"
#include "wcp/agents/copilot.hpp"
#include <memory>
#include <string>
#include <string_view>

namespace wcp {

class Router {
public:
    explicit Router(const Config& cfg);

    // Dispatch a message to the appropriate agent.
    // Returns the reply string (never throws — errors become reply text).
    std::string route(std::string_view text, const Config& cfg);

private:
    // TCC-OWN: unique_ptr makes ownership explicit and non-nullable after ctor
    std::unique_ptr<CopilotAgent> copilot_;
};

} // namespace wcp
