#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "curl_client.h"

namespace agent_memory {

using HttpResponse = CurlResponse;

struct JsonPostRequest
{
    std::string url;
    std::string body;
    int timeoutSeconds{60};
    std::unordered_map<std::string, std::string> headers;
};

using JsonPostTransport = std::function<HttpResponse(const JsonPostRequest&)>;

HttpResponse PostJson(const JsonPostRequest& request);
HttpResponse DefaultPostJson(const JsonPostRequest& request);
void SetJsonPostTransportForTesting(JsonPostTransport transport);
void ResetJsonPostTransportForTesting();

} // namespace agent_memory