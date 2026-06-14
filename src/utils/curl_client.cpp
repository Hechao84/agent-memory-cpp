#include "curl_client.h"

#include <chrono>
#include <new>
#include <thread>

namespace agent_memory {

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    try {
        buffer->append(static_cast<char*>(contents), total);
    } catch (const std::bad_alloc&) {
        return 0;
    }
    return total;
}

struct curl_slist* BuildSlist(const std::unordered_map<std::string, std::string>& headers)
{
    struct curl_slist* list = nullptr;
    for (const auto& h : headers) {
        list = curl_slist_append(list, (h.first + ": " + h.second).c_str());
    }
    return list;
}

bool HasContentType(const std::unordered_map<std::string, std::string>& headers)
{
    const std::string target = "content-type";
    for (const auto& h : headers) {
        if (h.first.size() != target.size()) {
            continue;
        }
        bool match = true;
        for (std::string::size_type i = 0; i < target.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(h.first[i])) != target[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

} // namespace

CurlClient::CurlClient()
    : share_(curl_share_init())
{
    if (!share_) {
        return;
    }
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    curl_share_setopt(share_, CURLSHOPT_LOCKFUNC, ShareLockCallback);
    curl_share_setopt(share_, CURLSHOPT_UNLOCKFUNC, ShareUnlockCallback);
    curl_share_setopt(share_, CURLSHOPT_USERDATA, &shareMutex_);
}

CurlClient::~CurlClient()
{
    if (share_) {
        curl_share_cleanup(share_);
    }
}

CurlClient::CurlClient(CurlClient&& other) noexcept
    : share_(other.share_)
{
    other.share_ = nullptr;
    if (share_) {
        curl_share_setopt(share_, CURLSHOPT_USERDATA, &shareMutex_);
    }
}

CurlClient& CurlClient::operator=(CurlClient&& other) noexcept
{
    if (this != &other) {
        if (share_) {
            curl_share_cleanup(share_);
        }
        share_ = other.share_;
        other.share_ = nullptr;
        if (share_) {
            curl_share_setopt(share_, CURLSHOPT_USERDATA, &shareMutex_);
        }
    }
    return *this;
}

void CurlClient::ShareLockCallback(CURL*, curl_lock_data, curl_lock_access, void* userptr)
{
    auto* mutex = static_cast<std::mutex*>(userptr);
    mutex->lock();
}

void CurlClient::ShareUnlockCallback(CURL*, curl_lock_data, void* userptr)
{
    auto* mutex = static_cast<std::mutex*>(userptr);
    mutex->unlock();
}

CurlResponse CurlClient::DoRequest(const std::string& url,
                                   const std::unordered_map<std::string, std::string>& headers,
                                   const CurlRequestOptions& options,
                                   bool isPost,
                                   const std::string& postBody) const
{
    CurlResponse response;
    int attempts = 1 + options.maxRetries;

    for (int attempt = 0; attempt < attempts; ++attempt) {
        response = CurlResponse{};
        CURL* curl = curl_easy_init();
        if (!curl) {
            response.curlCode = CURLE_FAILED_INIT;
            return response;
        }

        char errorBuffer[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

        if (share_) {
            curl_easy_setopt(curl, CURLOPT_SHARE, share_);
        }

        struct curl_slist* headerList = BuildSlist(headers);
        if (isPost && !HasContentType(headers)) {
            headerList = curl_slist_append(headerList, "Content-Type: application/json");
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, options.timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                         std::min(options.connectTimeoutSeconds, options.timeoutSeconds));
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "agent-memory-cpp/1.0");
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");

        if (options.followRedirects) {
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        }

        if (isPost) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postBody.size()));
        }

        response.curlCode = curl_easy_perform(curl);
        if (response.curlCode == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
        } else {
            response.errorMessage = errorBuffer;
        }

        if (headerList) {
            curl_slist_free_all(headerList);
        }
        curl_easy_cleanup(curl);

        if (response.curlCode == CURLE_OK && response.status >= 200 && response.status < 500
            && response.status != 429) {
            break;
        }
        if (attempt < attempts - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
        }
    }

    return response;
}

CurlResponse CurlClient::Get(const std::string& url,
                             const std::unordered_map<std::string, std::string>& headers,
                             CurlRequestOptions options) const
{
    return DoRequest(url, headers, options, false);
}

CurlResponse CurlClient::Post(const std::string& url,
                              const std::string& body,
                              const std::unordered_map<std::string, std::string>& extraHeaders,
                              CurlRequestOptions options) const
{
    return DoRequest(url, extraHeaders, options, true, body);
}

std::string CurlClient::UrlEscape(const std::string& str)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        return str;
    }
    char* escaped = curl_easy_escape(curl, str.c_str(), static_cast<int>(str.size()));
    std::string result = escaped ? escaped : str;
    if (escaped) {
        curl_free(escaped);
    }
    curl_easy_cleanup(curl);
    return result;
}

void CurlClient::GlobalInit()
{
    curl_global_init(CURL_GLOBAL_ALL);
}

void CurlClient::GlobalCleanup()
{
    curl_global_cleanup();
}

} // namespace agent_memory