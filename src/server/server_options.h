#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

namespace agent_memory {

struct HttpServerOptions
{
    std::string host = "127.0.0.1";
    int port = 8090;
    std::size_t maxPayloadBytes = 1024 * 1024;
    int readTimeoutSeconds = 0;
    int writeTimeoutSeconds = 0;
    int threadCount = 0;
};

struct McpServerOptions
{
    std::string mode = "http";
    std::string path = "/mcp";
    std::size_t maxMessageBytes = 1024 * 1024;
};

struct ServerOptions
{
    std::string dataPath = "./data";
    nlohmann::json modelConfig = nlohmann::json::object();
    bool strictModelConfig = false;
    bool enablePayloadOffload = true;
    int offloadThreshold = 8000;
    int tokenBudget = 4096;
    bool debugErrors = false;
    std::string apiToken;

    HttpServerOptions http;
    McpServerOptions mcp;
};

ServerOptions LoadServerOptions(const nlohmann::json& j);
ServerOptions LoadServerOptionsFile(const std::string& path);
void ValidateServerOptions(const ServerOptions& opts, bool httpMode);

} // namespace agent_memory
