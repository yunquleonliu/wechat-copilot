// wechat-copilot — RustCC C++17 edition
// http.hpp — RAII libcurl wrappers
//
// TCC-OWN:  CurlHandle, CurlSlist own their libcurl resources; no raw free() calls.
// TCC-LIFE: Handles are non-copyable; move-only ownership transfer.
// TCC-CONC: Each request creates its own CurlHandle — no shared handles.

#pragma once

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <curl/curl.h>

namespace wcp {

// Result<T>: either a value or an error string.
// Avoids exceptions for HTTP errors (connection refused, non-200, etc.).
template<typename T>
struct Result {
    bool ok;
    T    value;
    std::string error;

    static Result<T> success(T v)             { return {true,  std::move(v), {}}; }
    static Result<T> failure(std::string err) { return {false, {},   std::move(err)}; }
};

// Specialization for Result<void> — no value field.
template<>
struct Result<void> {
    bool ok;
    std::string error;

    static Result<void> success()                 { return {true,  {}}; }
    static Result<void> failure(std::string err)  { return {false, std::move(err)}; }
};

// ── RAII wrappers ──────────────────────────────────────────────────────────

// Owns a curl_slist* header list.
// TCC-OWN: destructor unconditionally frees.
class CurlSlist {
public:
    CurlSlist() = default;
    ~CurlSlist() { curl_slist_free_all(list_); }

    CurlSlist(const CurlSlist&)            = delete;
    CurlSlist& operator=(const CurlSlist&) = delete;
    CurlSlist(CurlSlist&& o) noexcept : list_(o.list_) { o.list_ = nullptr; }

    void append(const char* header) {
        list_ = curl_slist_append(list_, header);
    }
    curl_slist* get() const noexcept { return list_; }

private:
    curl_slist* list_ = nullptr;
};

// Owns a CURL* easy handle.
// TCC-OWN: one handle = one owner; non-copyable.
class CurlHandle {
public:
    CurlHandle() : handle_(curl_easy_init()) {
        if (!handle_) throw std::runtime_error("curl_easy_init failed");
    }
    ~CurlHandle() { if (handle_) curl_easy_cleanup(handle_); }

    CurlHandle(const CurlHandle&)            = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
    CurlHandle(CurlHandle&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }

    CURL* get() const noexcept { return handle_; }

private:
    CURL* handle_;
};

// ── HTTP request builder ───────────────────────────────────────────────────

struct HttpResponse {
    long        status_code = 0;
    std::string body;
};

// Performs a single synchronous POST.
// Returns Result<HttpResponse>; never throws on network errors.
//
// TCC-OWN:  handle is stack-local RAII.
// TCC-LIFE: response_body captured via write callback; no dangling refs.
// TCC-CONC: stateless — safe to call from multiple threads concurrently.
Result<HttpResponse> http_post(
    std::string_view                         url,
    const std::vector<std::string>&          headers,
    std::string_view                         body,
    std::chrono::milliseconds                timeout);

} // namespace wcp
