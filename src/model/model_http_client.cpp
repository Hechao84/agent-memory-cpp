#include "model_http_client.h"

#include <mutex>
#include <utility>

namespace agent_memory {

namespace {

std::mutex g_transportMutex;
JsonPostTransport g_transport;
CurlClient g_defaultClient;

} // namespace

HttpResponse DefaultPostJson(const JsonPostRequest& request)
{
    CurlRequestOptions options;
    options.timeoutSeconds = static_cast<long>(request.timeoutSeconds);
    return g_defaultClient.Post(request.url, request.body, request.headers, options);
}

HttpResponse PostJson(const JsonPostRequest& request)
{
    JsonPostTransport transport;
    {
        std::lock_guard<std::mutex> lock(g_transportMutex);
        transport = g_transport;
    }
    if (transport) {
        return transport(request);
    }
    return DefaultPostJson(request);
}

void SetJsonPostTransportForTesting(JsonPostTransport transport)
{
    std::lock_guard<std::mutex> lock(g_transportMutex);
    g_transport = std::move(transport);
}

void ResetJsonPostTransportForTesting()
{
    std::lock_guard<std::mutex> lock(g_transportMutex);
    g_transport = nullptr;
}

} // namespace agent_memory