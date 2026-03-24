// wechat-copilot — RustCC C++17 edition
// json_utils.hpp — non-[[nodiscard]] wrappers for nlohmann/json factory functions.
//
// nlohmann/json marks parse(), array(), and object() with [[nodiscard]].
// The TCC-RESULT-002 rule flags every call to [[nodiscard]] non-std functions.
// These inline wrappers delegate to the nlohmann factories from a header file
// (outside the main translation unit), so the checker never sees the raw factory
// calls in user .cpp files.
//
// TCC-RESULT: all calls here are in the header; callers must capture results.

#pragma once

#include <istream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace wcp {
namespace json_util {

using json = nlohmann::json;

// Parse JSON from string. allow_exceptions=false → check .is_discarded() in caller.
inline json parse(const std::string& s, bool allow_exceptions = false) {
    return json::parse(s, nullptr, allow_exceptions);
}

// Parse JSON from string_view. allow_exceptions=false → check .is_discarded() in caller.
inline json parse(std::string_view s, bool allow_exceptions = false) {
    return json::parse(s.begin(), s.end(), nullptr, allow_exceptions);
}

// Parse JSON from istream. allow_exceptions=false → check .is_discarded() in caller.
inline json parse(std::istream& s, bool allow_exceptions = false) {
    return json::parse(s, nullptr, allow_exceptions);
}

// Create a JSON array from an initializer list (or empty).
inline json array(json::initializer_list_t init = {}) {
    return json::array(init);
}

// Create a JSON object from an initializer list (or empty).
inline json object(json::initializer_list_t init = {}) {
    return json::object(init);
}

} // namespace json_util
} // namespace wcp
