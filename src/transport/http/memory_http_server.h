#pragma once

#include "httplib.h"
#include <nlohmann/json.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace agent_memory {

class MemoryMcpProtocol;
class MemoryRuntime;

class MemoryHttpServer
{
public:
    MemoryHttpServer(MemoryRuntime& runtime, std::string apiToken = std::string(), bool debugErrors = false,
                     size_t maxPayloadBytes = 1024 * 1024, std::string mcpPath = "/mcp", size_t maxMcpMessageBytes = 1024 * 1024);
    ~MemoryHttpServer();
    void RegisterRoutes(httplib::Server& server);

private:
    void RegisterAuth(httplib::Server& server);
    void RegisterMcpRoutes(httplib::Server& server);
    std::string ErrorMessage(const std::exception& e, const std::string& fallback) const;

    template<typename F>
    void HandleJsonPost(const httplib::Request& req, httplib::Response& res, F&& handler)
    {
        if (maxPayloadBytes_ > 0 && req.body.size() > maxPayloadBytes_) {
            res.status = 413;
            res.set_content(nlohmann::json({{"ok", false}, {"error", "payload too large"}}).dump(), "application/json");
            return;
        }
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(req.body);
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"ok", false}, {"error", ErrorMessage(e, "invalid JSON")}}).dump(), "application/json");
            return;
        }
        try {
            res.set_content(handler(j).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json({{"ok", false}, {"error", ErrorMessage(e, "internal server error")}}).dump(), "application/json");
        }
    }

    template<typename F>
    void HandleJsonGet(httplib::Response& res, F&& handler)
    {
        try {
            res.set_content(handler().dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json({{"ok", false}, {"error", ErrorMessage(e, "internal server error")}}).dump(), "application/json");
        }
    }

    MemoryRuntime& runtime_;
    std::string apiToken_;
    bool debugErrors_;
    size_t maxPayloadBytes_;
    std::string mcpPath_;
    size_t maxMcpMessageBytes_;
    std::unique_ptr<MemoryMcpProtocol> mcpProtocol_;
};

} // namespace agent_memory
