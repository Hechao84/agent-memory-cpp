#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace agent_memory {

class BuiltinMemoryRuntime;
class MemoryMcpProtocol
{
public:
    MemoryMcpProtocol(BuiltinMemoryRuntime& runtime, bool debugErrors = false);
    nlohmann::json HandleJsonRpc(const nlohmann::json& request);
    std::string HandleJsonRpcText(const std::string& body);
    nlohmann::json MakeError(const nlohmann::json& id, int code, const std::string& message);

private:
    nlohmann::json HandleRequest(const nlohmann::json& req);
    nlohmann::json CallTool(const std::string& name, const nlohmann::json& args);
    nlohmann::json MakeTextResult(const nlohmann::json& value);
    nlohmann::json Success(const nlohmann::json& id, const nlohmann::json& result);
    nlohmann::json Error(const nlohmann::json& id, int code, const std::string& message);
    nlohmann::json ToolSchema(const std::string& name, const std::string& description, const nlohmann::json& properties,
                              const std::vector<std::string>& required);
    std::string ErrorMessage(const std::exception& e, const std::string& fallback) const;
    void InitToolHandlers();
    nlohmann::json BuildToolsList();

    BuiltinMemoryRuntime& runtime_;
    bool debugErrors_;
    nlohmann::json cachedTools_;
    std::unordered_map<std::string, std::function<nlohmann::json(const nlohmann::json&)>> toolHandlers_;
    std::unordered_map<std::string, std::function<nlohmann::json(const nlohmann::json&, const nlohmann::json&)>> methodHandlers_;
};

} // namespace agent_memory
