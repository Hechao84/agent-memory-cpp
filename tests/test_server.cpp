#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

#include "agent_memory/builtin_memory_runtime.h"
#include "httplib.h"
#include "memory_http_server.h"
#include "server_cli.h"
#include "server_common.h"
#include "server_options.h"
#include <nlohmann/json.hpp>

using namespace agent_memory;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool ExpectRuntimeError(const nlohmann::json& j, const std::string& message)
{
    try {
        (void)LoadServerOptions(j);
    } catch (const std::runtime_error&) {
        return true;
    }
    std::cerr << message << "\n";
    return false;
}

bool TestCliParsing()
{
    int port = 0;
    if (!Check(ParsePort("1", port) && port == 1, "port 1 parse failed")) {
        return false;
    }
    if (!Check(ParsePort("65535", port) && port == 65535, "port 65535 parse failed")) {
        return false;
    }
    if (!Check(!ParsePort("0", port) && !ParsePort("65536", port) && !ParsePort("12x", port), "invalid ports accepted")) {
        return false;
    }

    const char* argvRaw[] = {"memory-server", "--host", "127.0.0.1", "--config", "config.json"};
    char** argv = const_cast<char**>(argvRaw);
    std::string path;
    if (!Check(FindConfigPath(5, argv, path) && path == "config.json", "config path parse failed")) {
        return false;
    }
    return true;
}

bool TestServerOptions()
{
    nlohmann::json valid = {
        {"memory", {{"dataPath", "./data/server-test"}, {"tokenBudget", 2048}}},
        {"model", {{"enabled", false}}},
        {"server", {{"debugErrors", true},
                    {"auth", {{"apiToken", "token"}}},
                    {"http", {{"host", "127.0.0.1"}, {"port", 8123}, {"maxPayloadBytes", static_cast<std::uint64_t>(32)}}},
                    {"mcp", {{"mode", "http"}, {"path", "/mcp-test"}, {"maxMessageBytes", static_cast<std::uint64_t>(64)}}}}}
    };
    auto options = LoadServerOptions(valid);
    if (!Check(options.dataPath == "./data/server-test" && options.http.port == 8123 && options.http.maxPayloadBytes == 32 &&
                   options.mcp.path == "/mcp-test" && options.mcp.maxMessageBytes == 64 && options.apiToken == "token" &&
                   options.debugErrors,
               "valid server options failed")) {
        return false;
    }

    nlohmann::json badKey = valid;
    badKey["server"]["http"]["unexpected"] = true;
    if (!ExpectRuntimeError(badKey, "unknown config key accepted")) {
        return false;
    }

    nlohmann::json badSize = valid;
    badSize["server"]["http"]["maxPayloadBytes"] = "large";
    if (!ExpectRuntimeError(badSize, "non-unsigned size accepted")) {
        return false;
    }

    nlohmann::json badMcp = valid;
    badMcp["server"]["mcp"]["path"] = "mcp";
    try {
        ValidateServerOptions(LoadServerOptions(badMcp), true);
    } catch (const std::runtime_error&) {
        return true;
    }
    std::cerr << "invalid mcp path accepted\n";
    return false;
}

bool TestServerSetupProbe()
{
    auto dataPath = std::filesystem::temp_directory_path() / "agent_memory_server_probe_test";
    std::filesystem::remove_all(dataPath);
    ServerOptions options;
    options.dataPath = dataPath.string();
    options.modelConfig = nlohmann::json::object();
    auto setup = CreateServerSetup(options);
    if (!Check(setup.runtime != nullptr && setup.config.dataPath.find("agent_memory_server_probe_test") != std::string::npos,
               "server setup failed")) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(setup.config.dataPath)) {
        if (entry.path().filename().string().find(".agent_memory_write_test_") == 0) {
            std::cerr << "write probe was not cleaned\n";
            return false;
        }
    }
    std::filesystem::remove_all(dataPath);
    return true;
}

bool TestHttpLimits()
{
    auto dataPath = std::filesystem::temp_directory_path() / "agent_memory_server_http_limit_test";
    std::filesystem::remove_all(dataPath);
    MemoryConfig config;
    config.dataPath = dataPath.string();
    BuiltinMemoryRuntime runtime(config);
    MemoryHttpServer memoryServer(runtime, nullptr, std::string(), true, 10, "/mcp", 128);
    httplib::Server server;
    memoryServer.RegisterRoutes(server);
    int port = server.bind_to_any_port("127.0.0.1");
    if (!Check(port > 0, "server bind failed")) {
        std::filesystem::remove_all(dataPath);
        return false;
    }
    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    server.wait_until_ready();
    httplib::Client client("127.0.0.1", port);

    auto restRes = client.Post("/v1/events", std::string(11, 'x'), "application/json");
    bool ok = Check(restRes && restRes->status == 413, "REST payload limit failed");

    auto mcpRes = client.Post("/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})", "application/json");
    ok = Check(ok && mcpRes && mcpRes->status == 200 && mcpRes->body.find("tools") != std::string::npos,
               "MCP should allow larger than REST limit");

    auto largeMcpRes = client.Post("/mcp", std::string(129, 'x'), "application/json");
    ok = Check(ok && largeMcpRes && largeMcpRes->status == 413, "MCP payload limit failed");

    server.stop();
    if (serverThread.joinable()) {
        serverThread.join();
    }
    std::filesystem::remove_all(dataPath);
    return ok;
}

} // namespace

int main()
{
    if (!TestCliParsing()) {
        return 1;
    }
    if (!TestServerOptions()) {
        return 1;
    }
    if (!TestServerSetupProbe()) {
        return 1;
    }
    if (!TestHttpLimits()) {
        return 1;
    }
    return 0;
}
