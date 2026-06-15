#include "memory_mcp_protocol.h"

#include <iostream>

#include "agent_memory/builtin_memory_runtime.h"
#include "json_memory_codec.h"

namespace agent_memory {

MemoryMcpProtocol::MemoryMcpProtocol(BuiltinMemoryRuntime& runtime, bool debugErrors)
    : runtime_(runtime), debugErrors_(debugErrors)
{
    InitToolHandlers();
    cachedTools_ = BuildToolsList();
}

void MemoryMcpProtocol::InitToolHandlers()
{
    toolHandlers_["memory_append_event"] = [this](const nlohmann::json& args) {
        auto result = runtime_.AppendEvent(EventFromJson(args));
        return MakeTextResult(result ? SuccessEnvelope({{"succeeded", true}}) : ErrorEnvelope(result.error));
    };
    toolHandlers_["memory_build_context"] = [this](const nlohmann::json& args) {
        auto result = runtime_.BuildContext(ContextRequestFromJson(args));
        return MakeTextResult(result ? SuccessEnvelope({{"context", ContextPackageToJson(result.context)}}) : ErrorEnvelope(result.error));
    };
    toolHandlers_["memory_write_payload"] = [this](const nlohmann::json& args) {
        auto result = runtime_.WritePayload(PayloadWriteRequestFromJson(args));
        return MakeTextResult(result ? SuccessEnvelope(PayloadWriteResultToJson(result)) : ErrorEnvelope(result.error));
    };
    toolHandlers_["memory_read_payload"] = [this](const nlohmann::json& args) {
        std::string uri = args.value("uri", std::string());
        auto result = runtime_.ReadPayload(uri);
        return MakeTextResult(result ? SuccessEnvelope({{"uri", uri}, {"content", result.content}}) : ErrorEnvelope(result.error));
    };
    toolHandlers_["memory_consolidate"] = [this](const nlohmann::json& args) {
        auto result = runtime_.Consolidate(ConsolidationRequestFromJson(args));
        return MakeTextResult(result ? SuccessEnvelope(ConsolidationResultToJson(result)) : ErrorEnvelope(result.error));
    };
    toolHandlers_["memory_search"] = [this](const nlohmann::json& args) {
        auto result = runtime_.SearchMemory(SearchRequestFromJson(args));
        return MakeTextResult(result ? SuccessEnvelope(SearchResponseToJson(result.results)) : ErrorEnvelope(result.error));
    };
    toolHandlers_["memory_stats"] = [this](const nlohmann::json&) {
        auto result = runtime_.GetStats();
        return MakeTextResult(result ? SuccessEnvelope({{"stats", StatsToJson(result.stats)}}) : ErrorEnvelope(result.error));
    };

    methodHandlers_["initialize"] = [this](const nlohmann::json&, const nlohmann::json& id) {
        return Success(id, {{"protocolVersion", "2024-11-05"},
                            {"serverInfo", {{"name", "memory-server"}, {"version", "0.1.0"}}},
                            {"capabilities", {{"tools", nlohmann::json::object()}}}});
    };
    methodHandlers_["tools/list"] = [this](const nlohmann::json&, const nlohmann::json& id) {
        return Success(id, cachedTools_);
    };
    methodHandlers_["tools/call"] = [this](const nlohmann::json& req, const nlohmann::json& id) {
        auto params = req.value("params", nlohmann::json::object());
        return Success(id, CallTool(params.value("name", std::string()),
                                    params.value("arguments", nlohmann::json::object())));
    };
}

nlohmann::json MemoryMcpProtocol::HandleJsonRpc(const nlohmann::json& request)
{
    bool isNotification = !request.contains("id");
    if (isNotification) {
        return nlohmann::json();
    }
    return HandleRequest(request);
}

std::string MemoryMcpProtocol::HandleJsonRpcText(const std::string& body)
{
    try {
        auto req = nlohmann::json::parse(body);
        auto result = HandleJsonRpc(req);
        if (result.empty()) {
            return {};
        }
        return result.dump();
    } catch (const std::exception& e) {
        return Error(nullptr, -32700, ErrorMessage(e, "parse error")).dump();
    }
}

nlohmann::json MemoryMcpProtocol::MakeError(const nlohmann::json& id, int code, const std::string& message)
{
    return Error(id, code, message);
}

nlohmann::json MemoryMcpProtocol::MakeTextResult(const nlohmann::json& value)
{
    return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", value.dump()}}})}};
}

nlohmann::json MemoryMcpProtocol::Error(const nlohmann::json& id, int code, const std::string& message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json MemoryMcpProtocol::Success(const nlohmann::json& id, const nlohmann::json& result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

nlohmann::json MemoryMcpProtocol::ToolSchema(const std::string& name, const std::string& description,
                                            const nlohmann::json& properties,
                                            const std::vector<std::string>& required)
{
    return {{"name", name},
            {"description", description},
            {"inputSchema", {{"type", "object"}, {"properties", properties}, {"required", required}}}};
}

nlohmann::json MemoryMcpProtocol::BuildToolsList()
{
    nlohmann::json tools = nlohmann::json::array();
    tools.push_back(ToolSchema("memory_append_event", "Append a memory event.",
                                {{"type", {{"type", "integer"}}},
                                 {"agentId", {{"type", "string"}}},
                                 {"sessionId", {{"type", "string"}}},
                                 {"role", {{"type", "string"}}},
                                 {"content", {{"type", "string"}}},
                                 {"toolCallId", {{"type", "string"}}},
                                 {"toolName", {{"type", "string"}}},
                                 {"payloadRef", {{"type", "string"}}},
                                 {"metadata", {{"type", "object"}}},
                                 {"timestamp", {{"type", "string"}}}},
                                {"type", "agentId", "sessionId"}));
    tools.push_back(ToolSchema("memory_build_context", "Build memory context.",
                                {{"agentId", {{"type", "string"}}},
                                 {"sessionId", {{"type", "string"}}},
                                 {"query", {{"type", "string"}}},
                                 {"tokenBudget", {{"type", "integer"}}},
                                 {"includeSections", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                                 {"metadata", {{"type", "object"}}}},
                                {"agentId", "sessionId"}));
    tools.push_back(ToolSchema("memory_write_payload", "Write or offload a payload.",
                                {{"agentId", {{"type", "string"}}},
                                 {"sessionId", {{"type", "string"}}},
                                 {"content", {{"type", "string"}}},
                                 {"contentType", {{"type", "string"}}},
                                 {"toolCallId", {{"type", "string"}}},
                                 {"toolName", {{"type", "string"}}},
                                 {"metadata", {{"type", "object"}}}},
                                {"content"}));
    tools.push_back(ToolSchema("memory_read_payload", "Read an offloaded payload.",
                                {{"uri", {{"type", "string"}}}}, {"uri"}));
    tools.push_back(ToolSchema("memory_consolidate", "Trigger memory consolidation.",
                                {{"agentId", {{"type", "string"}}},
                                 {"sessionId", {{"type", "string"}}},
                                 {"maxEvents", {{"type", "integer"}}},
                                 {"forceReprocess", {{"type", "boolean"}}},
                                 {"metadata", {{"type", "object"}}}},
                                {"agentId"}));
    tools.push_back(ToolSchema("memory_search", "Search memory.",
                                {{"agentId", {{"type", "string"}}},
                                 {"sessionId", {{"type", "string"}}},
                                 {"query", {{"type", "string"}}},
                                 {"limit", {{"type", "integer"}}},
                                 {"includeSections", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                                 {"metadata", {{"type", "object"}}}},
                                {"query"}));
    tools.push_back(ToolSchema("memory_stats", "Return memory stats.", nlohmann::json::object(), {}));
    return {{"tools", tools}};
}

nlohmann::json MemoryMcpProtocol::CallTool(const std::string& name, const nlohmann::json& args)
{
    try {
        auto it = toolHandlers_.find(name);
        if (it != toolHandlers_.end()) {
            return it->second(args);
        }
        return MakeTextResult({{"ok", false}, {"error", "unknown tool: " + name}});
    } catch (const std::exception& e) {
        return MakeTextResult({{"ok", false}, {"error", ErrorMessage(e, "tool call failed")}});
    }
}

nlohmann::json MemoryMcpProtocol::HandleRequest(const nlohmann::json& req)
{
    nlohmann::json id = req.contains("id") ? req["id"] : nullptr;
    std::string method = req.value("method", std::string());

    try {
        auto it = methodHandlers_.find(method);
        if (it != methodHandlers_.end()) {
            return it->second(req, id);
        }
    } catch (const std::exception& e) {
        return Error(id, -32603, ErrorMessage(e, "internal error"));
    }
    return Error(id, -32601, "method not found: " + method);
}

std::string MemoryMcpProtocol::ErrorMessage(const std::exception& e, const std::string& fallback) const
{
    if (debugErrors_) {
        return e.what();
    }
    std::cerr << e.what() << "\n";
    return fallback;
}

} // namespace agent_memory