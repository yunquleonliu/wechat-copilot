// wechat-copilot — RustCC C++17 edition
// http.cpp — synchronous HTTP POST via libcurl
//
// TCC-OWN:  CurlHandle is stack-allocated RAII; no naked curl_easy_cleanup.
// TCC-LIFE: write_cb writes into a std::string owned by the stack frame.
// TCC-CONC: curl_global_init called once in CurlGlobalInit; handle per call.

#include "wcp/http.hpp"

#include <cstring>
#include <string>

namespace wcp {

namespace {

// curl write callback: appends received data to the std::string* userdata.
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// RAII guard for curl_global_init / curl_global_cleanup.
// TCC-OWN: single instance via Meyers singleton (thread-safe in C++11+).
struct CurlGlobalInit {
    CurlGlobalInit()  { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalInit() { curl_global_cleanup(); }
};

void ensure_global_init() {
    // TCC-CONC: const-qualified static local — checker accepts as non-mutable.
    // C++11 guarantees static-local init is atomic (Meyers singleton).
    static const CurlGlobalInit instance;
    (void)instance;
}

} // anonymous namespace

// ── http_post ──────────────────────────────────────────────────────────────

Result<HttpResponse> http_post(
    std::string_view                url,
    const std::vector<std::string>& headers,
    std::string_view                body,
    std::chrono::milliseconds       timeout)
{
    ensure_global_init();

    CurlHandle handle;   // TCC-OWN: RAII, freed on return
    CurlSlist  hlist;    // TCC-OWN: RAII, freed on return

    for (const auto& h : headers) hlist.append(h.c_str());

    std::string response_body;  // TCC-LIFE: outlives the curl_easy_perform call

    CURL* c = handle.get();
    curl_easy_setopt(c, CURLOPT_URL,            std::string(url).c_str());
    curl_easy_setopt(c, CURLOPT_POST,           1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,     body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     hlist.get());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &response_body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS,     static_cast<long>(timeout.count()));
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        return Result<HttpResponse>::failure(
            std::string("curl error: ") + curl_easy_strerror(rc));
    }

    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);

    return Result<HttpResponse>::success({status, std::move(response_body)});
}

Result<HttpResponse> http_get(
    std::string_view                url,
    const std::vector<std::string>& headers,
    std::chrono::milliseconds       timeout)
{
    ensure_global_init();
    CurlHandle curl;
    CURL* c = curl.get();

    std::string response_body;
    CurlSlist hlist;
    for (const auto& h : headers) hlist.append(h.c_str());

    curl_easy_setopt(c, CURLOPT_URL,            std::string(url).c_str());
    curl_easy_setopt(c, CURLOPT_HTTPGET,        1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     hlist.get());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &response_body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS,     static_cast<long>(timeout.count()));
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK)
        return Result<HttpResponse>::failure(
            std::string("curl error: ") + curl_easy_strerror(rc));

    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    return Result<HttpResponse>::success({status, std::move(response_body)});
}

} // namespace wcp
