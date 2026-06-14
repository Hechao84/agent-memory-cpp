#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "curl_client.h"
#include "memory_http_server.h"
#include "server_cli.h"
#include "server_common.h"

using namespace agent_memory;

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

void RequestStop(int)
{
    gStopRequested = 1;
}

void PrintHelp()
{
    std::printf("memory-server [options]\n"
                "  --config <path>         Set server config JSON\n"
                "  --host <ip>             Override HTTP host (default: 127.0.0.1)\n"
                "  --port <n>              Override HTTP port 1-65535 (default: 8090)\n"
                "  --data <path>           Override data directory (default: ./data)\n"
                "  --help                  Show this help\n");
}

bool IsLoopbackHost(const std::string& host)
{
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

void ApplyHttpOptions(httplib::Server& server, const HttpServerOptions& options)
{
    if (options.readTimeoutSeconds > 0) {
        server.set_read_timeout(options.readTimeoutSeconds, 0);
    }
    if (options.writeTimeoutSeconds > 0) {
        server.set_write_timeout(options.writeTimeoutSeconds, 0);
    }
    if (options.threadCount > 0) {
        server.new_task_queue = [count = options.threadCount]() {
            return new httplib::ThreadPool(static_cast<size_t>(count));
        };
    }
}

int Run(int argc, char* argv[])
{
    CurlGlobalScope curlScope;
    ServerOptions options;
    std::string configPath;

    if (HasHelpOption(argc, argv)) {
        PrintHelp();
        return 0;
    }
    if (!FindConfigPath(argc, argv, configPath)) {
        return 1;
    }
    if (!configPath.empty()) {
        options = LoadServerOptionsFile(configPath);
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") {
            if (!RequireCliValue(i, argc, arg)) {
                return 1;
            }
            ++i;
        } else if (arg == "--host") {
            if (!RequireCliValue(i, argc, arg)) {
                return 1;
            }
            options.http.host = argv[++i];
        } else if (arg == "--port") {
            if (!RequireCliValue(i, argc, arg)) {
                return 1;
            }
            if (!ParsePort(argv[++i], options.http.port)) {
                std::cerr << "invalid --port, expected 1-65535\n";
                return 1;
            }
        } else if (arg == "--data") {
            if (!RequireCliValue(i, argc, arg)) {
                return 1;
            }
            options.dataPath = argv[++i];
        } else {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        }
    }

    ValidateServerOptions(options, true);
    auto setup = CreateServerSetup(options);

    httplib::Server server;
    gStopRequested = 0;
    std::signal(SIGINT, RequestStop);
    std::signal(SIGTERM, RequestStop);
    std::thread stopThread([&server]() {
        while (!gStopRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server.stop();
    });
    ApplyHttpOptions(server, options.http);
    MemoryHttpServer memoryServer(*setup.runtime, setup.model.get(), options.apiToken, options.debugErrors,
                                  options.http.maxPayloadBytes, options.mcp.path, options.mcp.maxMessageBytes);
    memoryServer.RegisterRoutes(server);

    if (!IsLoopbackHost(options.http.host)) {
        std::cerr << "Warning: memory-server is listening on non-loopback host " << options.http.host << "\n";
    }

    std::cout << "memory-server starting on " << options.http.host << ":" << options.http.port
              << " data=" << setup.config.dataPath
              << " model=" << (setup.model ? "enabled" : "disabled") << std::endl;

    bool listened = server.listen(options.http.host, options.http.port);
    gStopRequested = 1;
    if (stopThread.joinable()) {
        stopThread.join();
    }
    if (!listened) {
        std::cerr << "Failed to listen on " << options.http.host << ":" << options.http.port << std::endl;
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return Run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "memory-server failed: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "memory-server failed: unknown error\n";
    }
    return 1;
}
