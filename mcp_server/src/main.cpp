//  The MIT License
//
//  Copyright (C) 2025 Giuseppe Mastrangelo
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  'Software'), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be
//  included in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include "version.h"
#include "httplib.h"
#include "popl.hpp"
#include "StdioTransport.h"
#include "SseTransport.h"
#include "HttpStreamTransport.hpp"
#include "server/Server.h"
#include "aixlog.hpp"
#include "loader/PluginsLoader.h"
#include "json.hpp"
#include "utils/MCPBuilder.h"
#include <csignal>

using namespace popl;

std::shared_ptr<vx::mcp::Server> server;
std::shared_ptr<vx::mcp::PluginsLoader> loader;

// 信号处理函数只设置原子标志，不执行任何非 async-signal-safe 操作。
// 对于 stdio 传输，SIGINT 会中断阻塞的 read() 系统调用使 Connect 循环退出。
// 对于 HTTP/SSE 传输，Connect 循环会在下次迭代检测到 isStopping_ 后退出。
volatile sig_atomic_t g_stopRequested = 0;

struct NotificationState {
    std::mutex serverNotificationMutex;
};
NotificationState notificationState;

void stop_handler(sig_atomic_t s) {
    g_stopRequested = 1;
    if (server) {
        server->RequestStop();
    }
}

/// Notification Implementation from plugins to mcp-client
void ClientNotificationCallbackImpl(const char* pluginName, const char* notification) {
    std::lock_guard<std::mutex> lock(notificationState.serverNotificationMutex);
    if (server && server->IsValid()) {
        server->SendNotification(pluginName, notification);
    }
}

/// main entry point
int main(int argc, char **argv) {
    std::string name;
    std::string plugins_directory;
    std::string logs_directory;
    bool verbose;

    std::shared_ptr<vx::ITransport> transport;
    loader = std::make_shared<vx::mcp::PluginsLoader>();
    server = std::make_shared<vx::mcp::Server>();

    //============================================================================================
    // setup signal handler (Ctrl+C)
    //============================================================================================
    signal(SIGINT, stop_handler);

    //============================================================================================
    // setup command line options
    //============================================================================================
    OptionParser op("Allowed options");
    auto help_option = op.add<Switch>("", "help", "produce help message");
    auto name_option = op.add<Value<std::string>>("n", "name", "the name of the server", "mcp-server");
    auto plugins_directory_option = op.add<Value<std::string>>("p", "plugins", "the directory where to load the plugins", "./plugins");
    auto logs_directory_option = op.add<Value<std::string>>("l", "logs", "the directory where to store the logs", "./logs");
    auto verbose_option = op.add<Value<bool>>("v", "verbose", "enable verbose", verbose);
    auto use_sse_server = op.add<Switch>("s", "sse", "start as sse server");
    auto use_httpstream_server = op.add<Switch>("t", "httpstream", "start as http stream server");
    name_option->assign_to(&name);
    plugins_directory_option->assign_to(&plugins_directory);
    logs_directory_option->assign_to(&logs_directory);
    verbose_option->assign_to(&verbose);

    //============================================================================================
    // parse options
    //============================================================================================
    try {
        op.parse(argc, argv);
        if (help_option->count() == 1) {
            std::cout << op << std::endl;
            return 0;
        }
    } catch (const popl::invalid_option& e) {
        std::cerr << "Invalid Option Exception: " << e.what() << std::endl;
        return -1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }

    //============================================================================================
    // setup transport
    //============================================================================================
    if (use_sse_server->count() > 0) {
        transport = std::make_shared<vx::transport::SSE>();
    } else if (use_httpstream_server->count() > 0) {
        transport = std::make_shared<vx::transport::HttpStream>();
    } else {
        transport = std::make_shared<vx::transport::Stdio>();
    }

    //============================================================================================
    // setup logger
    //============================================================================================
    // Get the current time as ISO 8601 string
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H-%M-%S");
    std::string iso_date = ss.str();

    // Concatenate ISO date to logname
    std::string logFilename = logs_directory + "/mcp-server_" + iso_date + ".log";
    auto sink_file = std::make_shared<AixLog::SinkFile>(AixLog::Severity::trace, logFilename);
    AixLog::Log::init({sink_file});

    //============================================================================================
    // print logo and info
    //============================================================================================
    LOG(INFO) << " __  __  _____ _____        _____ ______ _______      ________ _____  " << std::endl;
    LOG(INFO) << "|  \\/  |/ ____|  __ \\      / ____|  ____|  __ \\ \\    / /  ____|  __ \\ " << std::endl;
    LOG(INFO) << "| \\  / | |    | |__) |____| (___ | |__  | |__) \\ \\  / /| |__  | |__) |" << std::endl;
    LOG(INFO) << "| |\\/| | |    |  ___/______\\___ \\|  __| |  _  / \\ \\/ / |  __| |  _  / " << std::endl;
    LOG(INFO) << "| |  | | |____| |          ____) | |____| | \\ \\  \\  /  | |____| | \\ \\ " << std::endl;
    LOG(INFO) << "|_|  |_|\\_____|_|         |_____/|______|_|  \\_\\  \\/   |______|_|  \\_\\" << std::endl;
    LOG(INFO) << "Starting mcp-server v" << PROJECT_VERSION << " (transport: " << transport->GetName() << " v" << transport->GetVersion() << ") on port: " << transport->GetPort() << std::endl;
    LOG(INFO) << "Press Ctrl+C to exit." << std::endl;

    //============================================================================================
    // load all plugins from the plugins directory
    //============================================================================================
    // 在加载插件前设置回调，新插件加载成功后自动挂载通知系统
    loader->SetOnPluginLoaded([](vx::mcp::PluginEntry& plugin) {
        plugin.instance->notifications = new NotificationSystem();
        plugin.instance->notifications->SendToClient = ClientNotificationCallbackImpl;
    });

    // 插件列表变化后按类型通知客户端重新拉取
    loader->SetOnPluginsChanged([](bool toolsChanged, bool promptsChanged, bool resourcesChanged) {
        if (server && server->IsValid()) {
            if (toolsChanged) {
                server->SendNotification("mcp-server",
                    MCPBuilder::NotificationToolsListChanged().dump().c_str());
            }
            if (promptsChanged) {
                server->SendNotification("mcp-server",
                    MCPBuilder::NotificationPromptsListChanged().dump().c_str());
            }
            if (resourcesChanged) {
                server->SendNotification("mcp-server",
                    MCPBuilder::NotificationResourcesListChanged().dump().c_str());
            }
        }
    });

    if (loader->LoadPlugins(plugins_directory)) {
        LOG(INFO) << "Successfully loaded plugins" << std::endl;
    }

    // 启动后台线程，每5秒扫描插件目录变化（新增、更新、删除）
    loader->StartWatching(plugins_directory, std::chrono::seconds(5));

    //============================================================================================
    // start server
    //============================================================================================
    server->Name(name);
    server->VerboseLevel(verbose ? 1 : 0);
    server->OverrideCallback("tools/list", [](const json& request) {
        nlohmann::ordered_json response = MCPBuilder::Response(request);
        response["result"]["tools"] = json::array();

        // 获取快照后立即释放锁，遍历期间不阻塞热加载
        auto plugins = loader->GetPluginsSnapshot();
        for (const auto& plugin : plugins) {
            if (plugin->instance->GetType() == PLUGIN_TYPE_TOOLS) {
                for (int i = 0; i < plugin->instance->GetToolCount(); i++) {
                    nlohmann::ordered_json tool;
                    auto pluginTool = plugin->instance->GetTool(i);
                    tool["name"] = pluginTool->name;
                    tool["description"] = pluginTool->description;
                    tool["inputSchema"] = nlohmann::json::parse(pluginTool->inputSchema);
                    response["result"]["tools"].push_back(tool);
                }
            }
        }

        return response;
    });
    server->OverrideCallback("tools/call", [](const json& request) {
        nlohmann::ordered_json response = MCPBuilder::Response(request);

        auto plugins = loader->GetPluginsSnapshot();
        for (const auto& plugin : plugins) {
            if (plugin->instance->GetType() == PLUGIN_TYPE_TOOLS) {
                for (int i = 0; i < plugin->instance->GetToolCount(); i++) {
                    auto pluginTool = plugin->instance->GetTool(i);
                    if (pluginTool->name == request["params"]["name"]) {
                        char* res_ptr = plugin->instance->HandleRequest(request.dump().c_str());
                        if (res_ptr) {
                            try {
                                response["result"] = json::parse(res_ptr);
                                response["result"]["isError"] = false;
                            } catch (const json::parse_error& e) {
                                response["result"]["isError"] = true;
                                response["result"]["content"] = json::array();
                                response["result"]["content"].push_back({{"type", "text"}, {"text", "Plugin returned malformed data."}});
                            }
                            delete[] res_ptr;
                        } else {
                            LOG(ERROR) << "Plugin " << pluginTool->name << " returned nullptr." << std::endl;
                        }
                        return response;
                    }
                }
            }
        }

        return response;
    });
    server->OverrideCallback("prompts/list", [](const json& request) {
        nlohmann::ordered_json response = MCPBuilder::Response(request);
        response["result"]["prompts"] = json::array();

        auto plugins = loader->GetPluginsSnapshot();
        for (const auto& plugin : plugins) {
            if (plugin->instance->GetType() == PLUGIN_TYPE_PROMPTS) {
                for (int i = 0; i < plugin->instance->GetPromptCount(); i++) {
                    nlohmann::ordered_json prompt;
                    auto pluginPrompt = plugin->instance->GetPrompt(i);
                    prompt["name"] = pluginPrompt->name;
                    prompt["description"] = pluginPrompt->description;
                    prompt["arguments"] = nlohmann::json::parse(pluginPrompt->arguments);
                    response["result"]["prompts"].push_back(prompt);
                }
            }
        }

        return response;
    });
    server->OverrideCallback("prompts/get", [](const json& request) {
        nlohmann::ordered_json response = MCPBuilder::Response(request);

        auto plugins = loader->GetPluginsSnapshot();
        for (const auto& plugin : plugins) {
            if (plugin->instance->GetType() == PLUGIN_TYPE_PROMPTS) {
                for (int i = 0; i < plugin->instance->GetPromptCount(); i++) {
                    auto pluginPrompt = plugin->instance->GetPrompt(i);
                    if (pluginPrompt->name == request["params"]["name"]) {
                        char* res_ptr = plugin->instance->HandleRequest(request.dump().c_str());
                        if (res_ptr) {
                            try {
                                response["result"] = json::parse(res_ptr);
                            } catch (const json::parse_error& e) {
                                LOG(ERROR) << "Plugin " << pluginPrompt->name << " returned malformed data." << std::endl;
                            }
                            delete[] res_ptr;
                        }
                        return response;
                    }
                }
            }
        }

        return response;
    });
    server->OverrideCallback("resources/list", [](const json& request) {
        nlohmann::ordered_json response = MCPBuilder::Response(request);
        response["result"]["resources"] = json::array();

        auto plugins = loader->GetPluginsSnapshot();
        for (const auto& plugin : plugins) {
            if (plugin->instance->GetType() == PLUGIN_TYPE_RESOURCES) {
                for (int i = 0; i < plugin->instance->GetResourceCount(); i++) {
                    nlohmann::ordered_json resource;
                    auto pluginResource = plugin->instance->GetResource(i);
                    resource["name"] = pluginResource->name;
                    resource["description"] = pluginResource->description;
                    resource["uri"] = pluginResource->uri;
                    resource["mimeType"] = pluginResource->mime;
                    response["result"]["resources"].push_back(resource);
                }
            }
        }

        return response;
    });
    server->OverrideCallback("resources/read", [](const json& request) {
        nlohmann::ordered_json response = MCPBuilder::Response(request);

        auto plugins = loader->GetPluginsSnapshot();
        for (const auto& plugin : plugins) {
            if (plugin->instance->GetType() == PLUGIN_TYPE_RESOURCES) {
                for (int i = 0; i < plugin->instance->GetResourceCount(); i++) {
                    auto pluginResource = plugin->instance->GetResource(i);
                    if (pluginResource->uri == request["params"]["uri"]) {
                        char* res_ptr = plugin->instance->HandleRequest(request.dump().c_str());
                        if (res_ptr) {
                            try {
                                response["result"] = json::parse(res_ptr);
                            } catch (const json::parse_error& e) {
                                LOG(ERROR) << "Plugin " << pluginResource->name << " returned malformed data." << std::endl;
                            }
                            delete[] res_ptr;
                        }
                    }
                }
            }
        }

        return response;
    });

    server->Connect(transport);

    // server->Connect 返回后（正常退出或被 Stop），执行清理
    // 如果是 SIGINT 触发的，由下面的逻辑统一处理
    if (g_stopRequested) {
        LOG(INFO) << "Shutdown requested via signal, cleaning up..." << std::endl;
    }

    loader->StopWatching();
    loader->UnloadPlugins();

    if (server && server->IsValid()) {
        server->Stop();
    }

    LOG(INFO) << "Server shutdown complete." << std::endl;

    return 0;
}
