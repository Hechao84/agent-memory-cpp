#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

#include "agent_memory/builtin_memory_runtime.h"
#include "httplib.h"
#include "memory_http_server.h"
#include "memory_mcp_protocol.h"
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

struct RunningServer
{
    httplib::Server server;
    std::thread thread;
    int port{0};

    ~RunningServer()
    {
        server.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }
};

bool StartServer(RunningServer& running)
{
    running.port = running.server.bind_to_any_port("127.0.0.1");
    if (running.port <= 0) {
        return false;
    }
    running.thread = std::thread([&running]() {
        running.server.listen_after_bind();
    });
    running.server.wait_until_ready();
    return true;
}

nlohmann::json ParseBody(const httplib::Result& result)
{
    if (!result) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(result->body);
}

bool TestHttpEndpointsAndAuth()
{
    auto dataPath = std::filesystem::temp_directory_path() / "agent_memory_server_http_endpoint_test";
    std::filesystem::remove_all(dataPath);
    MemoryConfig config;
    config.dataPath = dataPath.string();
    config.enablePayloadOffload = true;
    config.offloadThresholdChars = 1;
    BuiltinMemoryRuntime runtime(config);
    MemoryHttpServer memoryServer(runtime, "secret", true, 1024 * 1024, "/mcp", 1024 * 1024);
    RunningServer running;
    memoryServer.RegisterRoutes(running.server);
    if (!Check(StartServer(running), "server bind failed")) {
        return false;
    }
    httplib::Client client("127.0.0.1", running.port);

    auto health = client.Get("/health");
    bool ok = Check(health && health->status == 200, "health should not require auth");
    auto unauthorized = client.Get("/v1/stats");
    ok = Check(ok && unauthorized && unauthorized->status == 401, "auth should reject missing token");
    client.set_bearer_token_auth("secret");

    nlohmann::json event = {{"type", static_cast<int>(MemoryEventType::MESSAGE_APPENDED)},
                            {"agentId", "agent-1"},
                            {"sessionId", "session-1"},
                            {"role", "user"},
                            {"content", "hello memory"}};
    auto append = client.Post("/v1/events", event.dump(), "application/json");
    ok = Check(ok && append && append->status == 200 && ParseBody(append).value("ok", false), "events endpoint failed");

    auto context = client.Post("/v1/context", nlohmann::json({{"agentId", "agent-1"}, {"sessionId", "session-1"}}).dump(), "application/json");
    ok = Check(ok && context && context->status == 200 && ParseBody(context).value("ok", false), "context endpoint failed");

    auto payload = client.Post("/v1/payloads", nlohmann::json({{"agentId", "agent-1"},
                                                                {"sessionId", "session-1"},
                                                                {"content", "payload-content"},
                                                                {"toolName", "tool"}}).dump(), "application/json");
    auto payloadBody = ParseBody(payload);
    ok = Check(ok && payload && payload->status == 200 && payloadBody.value("ok", false), "payload write endpoint failed");
    std::string uri = payloadBody["data"]["payload"].value("uri", std::string());
    std::string filePath = uri.rfind("file://", 0) == 0 ? uri.substr(7) : std::string();
    auto payloadRead = client.Get(("/v1/payloads/" + filePath).c_str());
    ok = Check(ok && payloadRead && payloadRead->status == 200 && ParseBody(payloadRead).value("ok", false), "payload read endpoint failed");

    auto search = client.Post("/v1/search", nlohmann::json({{"agentId", "agent-1"}, {"sessionId", "session-1"}, {"query", "hello"}}).dump(), "application/json");
    ok = Check(ok && search && search->status == 200 && ParseBody(search).value("ok", false), "search endpoint failed");

    auto consolidate = client.Post("/v1/consolidate", nlohmann::json({{"agentId", "agent-1"}, {"sessionId", "session-1"}, {"maxEvents", 10}}).dump(), "application/json");
    ok = Check(ok && consolidate && consolidate->status == 200 && ParseBody(consolidate).value("ok", false), "consolidate endpoint failed");

    auto stats = client.Get("/v1/stats");
    ok = Check(ok && stats && stats->status == 200 && ParseBody(stats).value("ok", false), "stats endpoint failed");

    auto malformed = client.Post("/v1/events", "{", "application/json");
    ok = Check(ok && malformed && malformed->status == 400, "malformed REST JSON should fail");

    std::filesystem::remove_all(dataPath);
    return ok;
}

bool TestMcpProtocolCoverage()
{
    auto dataPath = std::filesystem::temp_directory_path() / "agent_memory_server_mcp_test";
    std::filesystem::remove_all(dataPath);
    MemoryConfig config;
    config.dataPath = dataPath.string();
    config.enablePayloadOffload = true;
    config.offloadThresholdChars = 1;
    BuiltinMemoryRuntime runtime(config);
    MemoryMcpProtocol protocol(runtime, true);

    auto initialize = protocol.HandleJsonRpc({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}});
    bool ok = Check(initialize.contains("result"), "MCP initialize failed");
    auto list = protocol.HandleJsonRpc({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    ok = Check(ok && list["result"]["tools"].size() >= 7, "MCP tools/list failed");

    auto call = [&](int id, const std::string& name, const nlohmann::json& args) {
        return protocol.HandleJsonRpc({{"jsonrpc", "2.0"},
                                       {"id", id},
                                       {"method", "tools/call"},
                                       {"params", {{"name", name}, {"arguments", args}}}});
    };
    ok = Check(ok && call(3, "memory_append_event", {{"type", static_cast<int>(MemoryEventType::MESSAGE_APPENDED)},
                                                      {"agentId", "agent-1"},
                                                      {"sessionId", "session-1"},
                                                      {"role", "user"},
                                                      {"content", "hello mcp"}}).contains("result"),
               "MCP append tool failed");
    ok = Check(ok && call(4, "memory_build_context", {{"agentId", "agent-1"}, {"sessionId", "session-1"}}).contains("result"),
               "MCP context tool failed");
    auto writePayload = call(5, "memory_write_payload", {{"agentId", "agent-1"},
                                                          {"sessionId", "session-1"},
                                                          {"content", "payload content"},
                                                          {"toolName", "tool"}});
    ok = Check(ok && writePayload.contains("result"), "MCP write payload tool failed");
    auto text = writePayload["result"]["content"][0].value("text", std::string());
    std::string uri = nlohmann::json::parse(text)["data"]["payload"].value("uri", std::string());
    ok = Check(ok && call(6, "memory_read_payload", {{"uri", uri}}).contains("result"), "MCP read payload tool failed");
    ok = Check(ok && call(7, "memory_consolidate", {{"agentId", "agent-1"}, {"sessionId", "session-1"}, {"maxEvents", 10}}).contains("result"),
               "MCP consolidate tool failed");
    ok = Check(ok && call(8, "memory_search", {{"agentId", "agent-1"}, {"sessionId", "session-1"}, {"query", "hello"}}).contains("result"),
               "MCP search tool failed");
    ok = Check(ok && call(9, "memory_stats", nlohmann::json::object()).contains("result"), "MCP stats tool failed");

    auto unknownMethod = protocol.HandleJsonRpc({{"jsonrpc", "2.0"}, {"id", 10}, {"method", "unknown"}});
    ok = Check(ok && unknownMethod["error"].value("code", 0) == -32601, "MCP unknown method should fail");
    auto unknownTool = call(11, "unknown_tool", nlohmann::json::object());
    ok = Check(ok && unknownTool.contains("result"), "MCP unknown tool should return tool result");
    auto parseError = protocol.HandleJsonRpcText("{");
    ok = Check(ok && nlohmann::json::parse(parseError)["error"].value("code", 0) == -32700, "MCP malformed JSON should fail");
    auto notification = protocol.HandleJsonRpc({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    ok = Check(ok && notification.empty(), "MCP notification should not produce response");

    std::filesystem::remove_all(dataPath);
    return ok;
}

bool TestHttpLimits()
{
    auto dataPath = std::filesystem::temp_directory_path() / "agent_memory_server_http_limit_test";
    std::filesystem::remove_all(dataPath);
    MemoryConfig config;
    config.dataPath = dataPath.string();
    BuiltinMemoryRuntime runtime(config);
    MemoryHttpServer memoryServer(runtime, std::string(), true, 10, "/mcp", 128);
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
    if (!TestHttpEndpointsAndAuth()) {
        return 1;
    }
    if (!TestMcpProtocolCoverage()) {
        return 1;
    }
    if (!TestHttpLimits()) {
        return 1;
    }
    return 0;
}
