#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include <curl/curl.h>

namespace agent_memory {

struct CurlResponse
{
    long status{0};
    std::string body;
    CURLcode curlCode{CURLE_OK};
    std::string errorMessage;

    CurlResponse() = default;
    CurlResponse(long s, std::string b, CURLcode c = CURLE_OK, std::string errMsg = "")
        : status(s), body(std::move(b)), curlCode(c), errorMessage(std::move(errMsg))
    {
    }

    bool Ok() const { return curlCode == CURLE_OK && status >= 200 && status < 300; }
};

struct CurlRequestOptions
{
    long timeoutSeconds{10};
    long connectTimeoutSeconds{5};
    bool followRedirects{true};
    int maxRetries{0};
};

class CurlClient
{
public:
    CurlClient();
    ~CurlClient();

    CurlClient(CurlClient&&) noexcept;
    CurlClient& operator=(CurlClient&&) noexcept;

    CurlClient(const CurlClient&) = delete;
    CurlClient& operator=(const CurlClient&) = delete;

    CurlResponse Get(const std::string& url,
                     const std::unordered_map<std::string, std::string>& headers = {},
                     CurlRequestOptions options = {}) const;

    CurlResponse Post(const std::string& url,
                      const std::string& body,
                      const std::unordered_map<std::string, std::string>& extraHeaders = {},
                      CurlRequestOptions options = {}) const;

    static std::string UrlEscape(const std::string& str);
    static void GlobalInit();
    static void GlobalCleanup();

private:
    CurlResponse DoRequest(const std::string& url,
                           const std::unordered_map<std::string, std::string>& headers,
                           const CurlRequestOptions& options,
                           bool isPost,
                           const std::string& postBody = "") const;

    mutable std::mutex shareMutex_;
    CURLSH* share_;

    static void ShareLockCallback(CURL*, curl_lock_data, curl_lock_access, void*);
    static void ShareUnlockCallback(CURL*, curl_lock_data, void*);
};

class CurlGlobalScope
{
public:
    CurlGlobalScope() { CurlClient::GlobalInit(); }
    ~CurlGlobalScope() { CurlClient::GlobalCleanup(); }
    CurlGlobalScope(const CurlGlobalScope&) = delete;
    CurlGlobalScope& operator=(const CurlGlobalScope&) = delete;
};

} // namespace agent_memory