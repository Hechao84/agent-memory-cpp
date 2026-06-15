#include "memory_http_server.h"

#include <iostream>
#include <utility>

#include "agent_memory/runtime.h"
#include "json_memory_codec.h"
#include "memory_mcp_protocol.h"

namespace agent_memory {

namespace {

bool ConstantTimeEquals(const std::string& lhs, const std::string& rhs)
{
    size_t diff = lhs.size() ^ rhs.size();
    size_t maxSize = lhs.size() > rhs.size() ? lhs.size() : rhs.size();
    for (size_t i = 0; i < maxSize; ++i) {
        unsigned char l = i < lhs.size() ? static_cast<unsigned char>(lhs[i]) : 0;
        unsigned char r = i < rhs.size() ? static_cast<unsigned char>(rhs[i]) : 0;
        diff |= static_cast<size_t>(l ^ r);
    }
    return diff == 0;
}

} // namespace

MemoryHttpServer::MemoryHttpServer(MemoryRuntime& runtime, std::string apiToken, bool debugErrors, size_t maxPayloadBytes, std::string mcpPath, size_t maxMcpMessageBytes)
    : runtime_(runtime), apiToken_(std::move(apiToken)), debugErrors_(debugErrors), maxPayloadBytes_(maxPayloadBytes),
      mcpPath_(std::move(mcpPath)), maxMcpMessageBytes_(maxMcpMessageBytes),
      mcpProtocol_(std::make_unique<MemoryMcpProtocol>(runtime_, debugErrors_))
{
}

MemoryHttpServer::~MemoryHttpServer() = default;

void MemoryHttpServer::RegisterRoutes(httplib::Server& server)
{
    server.set_payload_max_length(maxPayloadBytes_ > maxMcpMessageBytes_ ? maxPayloadBytes_ : maxMcpMessageBytes_);
    RegisterAuth(server);
    RegisterMcpRoutes(server);

    server.Post("/v1/events", [this](const httplib::Request& req, httplib::Response& res) {
        HandleJsonPost(req, res, [this](const nlohmann::json& j) {
            auto result = runtime_.AppendEvent(EventFromJson(j));
            return result ? SuccessEnvelope({{"succeeded", true}}) : ErrorEnvelope(result.error);
        });
    });

    server.Post("/v1/context", [this](const httplib::Request& req, httplib::Response& res) {
        HandleJsonPost(req, res, [this](const nlohmann::json& j) {
            auto result = runtime_.BuildContext(ContextRequestFromJson(j));
            return result ? SuccessEnvelope({{"context", ContextPackageToJson(result.context)}}) : ErrorEnvelope(result.error);
        });
    });

    server.Post("/v1/payloads", [this](const httplib::Request& req, httplib::Response& res) {
        HandleJsonPost(req, res, [this](const nlohmann::json& j) {
            auto result = runtime_.WritePayload(PayloadWriteRequestFromJson(j));
            return result ? SuccessEnvelope(PayloadWriteResultToJson(result)) : ErrorEnvelope(result.error);
        });
    });

    server.Get(R"(/v1/payloads/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string match = req.matches[1].str();
        HandleJsonGet(res, [this, match]() {
            std::string uri = "file://" + match;
            auto result = runtime_.ReadPayload(uri);
            return result ? SuccessEnvelope({{"uri", uri}, {"content", result.content}}) : ErrorEnvelope(result.error);
        });
    });

    server.Post("/v1/consolidate", [this](const httplib::Request& req, httplib::Response& res) {
        HandleJsonPost(req, res, [this](const nlohmann::json& j) {
            auto result = runtime_.Consolidate(ConsolidationRequestFromJson(j));
            return result ? SuccessEnvelope(ConsolidationResultToJson(result)) : ErrorEnvelope(result.error);
        });
    });

    server.Post("/v1/search", [this](const httplib::Request& req, httplib::Response& res) {
        HandleJsonPost(req, res, [this](const nlohmann::json& j) {
            auto result = runtime_.SearchMemory(SearchRequestFromJson(j));
            return result ? SuccessEnvelope(SearchResponseToJson(result.results)) : ErrorEnvelope(result.error);
        });
    });

    server.Get("/v1/stats", [this](const httplib::Request&, httplib::Response& res) {
        HandleJsonGet(res, [this]() {
            auto result = runtime_.GetStats();
            return result ? SuccessEnvelope({{"stats", StatsToJson(result.stats)}}) : ErrorEnvelope(result.error);
        });
    });

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(nlohmann::json({{"status", "ok"}}).dump(), "application/json");
    });
}

void MemoryHttpServer::RegisterMcpRoutes(httplib::Server& server)
{
    server.Post(mcpPath_, [this](const httplib::Request& req, httplib::Response& res) {
        if (req.body.size() > maxMcpMessageBytes_) {
            res.status = 413;
            res.set_content(mcpProtocol_->MakeError(nullptr, -32600, "message too large").dump(), "application/json");
            return;
        }
        auto response = mcpProtocol_->HandleJsonRpcText(req.body);
        if (response.empty()) {
            res.status = 202;
            return;
        }
        res.set_content(response, "application/json");
    });
}

void MemoryHttpServer::RegisterAuth(httplib::Server& server)
{
    if (apiToken_.empty()) {
        return;
    }
    server.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (req.path == "/health") {
            return httplib::Server::HandlerResponse::Unhandled;
        }
        std::string expected = "Bearer " + apiToken_;
        if (ConstantTimeEquals(req.get_header_value("Authorization"), expected)) {
            return httplib::Server::HandlerResponse::Unhandled;
        }
        res.status = 401;
        res.set_content(nlohmann::json({{"ok", false}, {"error", "unauthorized"}}).dump(), "application/json");
        return httplib::Server::HandlerResponse::Handled;
    });
}

std::string MemoryHttpServer::ErrorMessage(const std::exception& e, const std::string& fallback) const
{
    if (debugErrors_) {
        return e.what();
    }
    std::cerr << e.what() << "\n";
    return fallback;
}

} // namespace agent_memory
