#include "model_http_client.h"

#include <utility>

namespace agent_memory {

HttpResponse DefaultPostJson(const JsonPostRequest& request)
{
    CurlClient client;
    CurlRequestOptions options;
    options.timeoutSeconds = static_cast<long>(request.timeoutSeconds);
    return client.Post(request.url, request.body, request.headers, options);
}

ModelHttpClient::ModelHttpClient()
    : transport_(DefaultPostJson)
{
}

ModelHttpClient::ModelHttpClient(JsonPostTransport transport)
    : transport_(std::move(transport))
{
    if (!transport_) {
        transport_ = DefaultPostJson;
    }
}

HttpResponse ModelHttpClient::PostJson(const JsonPostRequest& request) const
{
    return transport_(request);
}

} // namespace agent_memory
