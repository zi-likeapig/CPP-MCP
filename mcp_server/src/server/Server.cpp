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

#include <iostream>
#include <utility>
#include "Server.h"
#include "aixlog.hpp"
#include "version.h"
#include "../utils/MCPBuilder.h"

namespace vx::mcp {

    Server::Server() {
        functionMap = {
                {"initialize", [this](const json& req) { return this->InitializeCmd(req); }},
                {"ping", [this](const json& req) { return this->PingCmd(req); }},
                {"resources/list", [this](const json& req) { return this->ResourcesListCmd(req); }},
                {"resources/read", [this](const json& req) { return this->ResourcesReadCmd(req); }},
                {"tools/list", [this](const json& req) { return this->ToolsListCmd(req); }},
                {"tools/call", [this](const json& req) { return this->ToolsCallCmd(req); }},
                {"resources/subscribe", [this](const json& req) { return this->ResourcesSubscribeCmd(req); }},
                {"resources/unsubscribe", [this](const json& req) { return this->ResourcesUnsubscribeCmd(req); }},
                {"prompts/list", [this](const json& req) { return this->PromptsListCmd(req); }},
                {"prompts/get", [this](const json& req) { return this->PromptsGetCmd(req); }},
                {"logging/setLevel", [this](const json& req) { return this->LoggingSetLevelCmd(req); }},
                {"completion/complete", [this](const json& req) { return this->CompletionCompleteCmd(req); }},
                {"roots/list", [this](const json& req) { return this->RootsListCmd(req); }},
                {"notifications/initialized", [this](const json& req) { return this->NotificationInitializedCmd(req); }},
                {"notifications/cancelled", [this](const json& req) { return this->NotificationCancelledCmd(req); }},
                {"notifications/progress", [this](const json& req) { return this->NotificationProgressCmd(req); }},
                {"notifications/roots/list_changed", [this](const json& req) { return this->NotificationRootsListChangedCmd(req); }},
                {"notifications/resources/list_changed", [this](const json& req) { return this->NotificationResourcesListChangedCmd(req); }},
                {"notifications/resources/updated", [this](const json& req) { return this->NotificationResourcesUpdatedCmd(req); }},
                {"notifications/prompts/list_changed", [this](const json& req) { return this->NotificationPromptsListChangedCmd(req); }},
                {"notifications/tools/list_changed", [this](const json& req) { return this->NotificationToolsListChangedCmd(req); }},
                {"notifications/message", [this](const json& req) { return this->NotificationMessageCmd(req); }}
        };
    }

    Server::~Server() {
        Stop();
    }

    void Server::WriterLoop() {
        LOG(INFO) << "Writer thread started." << std::endl;
        while (writer_running_.load()) {
            std::string notification_to_send; 
            {   
                // 只想在操作队列的时候加锁，所以用{}缩小锁的范围，离开作用域自动解锁
                // unique_lock比lock_guard更灵活，可以手动解锁
                // 所以与条件变量配合的时候必须使用unique_lock，因为wait操作内部会自动解锁
                std::unique_lock<std::mutex> lock(output_mutex_);

                // 使用条件变量，等待队列不为空或者WriterLoop停止时唤醒
                // 不满足时内部会自动解锁，并等待条件变量满足时被唤醒再次加锁
                queue_cv_.wait(lock, [this] { return !notification_queue_.empty() || !writer_running_.load(); });

                // 醒来后再检查一遍运行标志和队列是否为空，如果都为空则退出循环
                if (!writer_running_.load() && notification_queue_.empty()) {
                    break; // Exit loop if stopped and queue is empty
                }

                if (!notification_queue_.empty()) {
                    notification_to_send = std::move(notification_queue_.front());
                    notification_queue_.pop();
                }
            } // Release lock before potentially blocking write

            if (!notification_to_send.empty() && transport_) {
                try {
                    // Note: Write itself is not locked here, assuming transport handles internal sync
                    // If transport->Write is not thread-safe, the lock needs to span this call too.
                    // For stdio, writing from one thread should be okay, but locking provides safety.
                    // Re-locking here for safety with potential other writes (responses).
                    std::lock_guard<std::mutex> write_lock(output_mutex_);
                    if (transport_) { // Check transport again after potential delay
                        LOG(DEBUG) << "Sending Notification: " << notification_to_send << std::endl;
                        transport_->Write(notification_to_send);
                    }
                } catch (const std::exception& e) {
                    LOG(ERROR) << "Error writing notification: " << e.what() << std::endl;
                    // Decide how to handle write errors (e.g., log, ignore, stop?)
                }
            }
            // Small sleep to prevent tight loop if errors occur rapidly
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        LOG(INFO) << "Writer thread stopped." << std::endl;
    }

    bool Server::Connect(const std::shared_ptr<ITransport> &transport) {
        if (!transport) {
            LOG(ERROR) << "Connect called with null transport." << std::endl;
            return false;
        }

        transport_ = transport; // Store the transport pointer
        isStopping_ = false;
        isSyncCleaned_ = false;

        // 启动后台写线程，该程序用于主动发送notification给MCP Client
        writer_running_ = true;
        // 创建一个线程执行WriterLoop函数，用于将通知发送给MCP Client，因为是成员函数所以需要指定属于当前对象
        writer_thread_ = std::thread(&Server::WriterLoop, this);

        // Start transport (required for SSE; should be a no-op/true for stdio)
        if (!transport_->Start()) {
            LOG(ERROR) << "Failed to start transport: " << transport_->GetName() << std::endl;
            return false;
        }

        while (!isStopping_) {
            // 因为是同步模型，所以不用另起一个read_thread，直接在主线程中读取就好
            auto [length, json_string] = transport->Read();
            if (isStopping_) break;

            if (length == 0 && json_string.empty()) {
                LOG(INFO) << "Read returned empty data, potentially client disconnected." << std::endl;
                isStopping_ = true;
                break;
            }

            try {
                if (json_string.empty()) continue;  // 修改read后应该可以去掉了，但保留以防万一
                LOG(DEBUG) << "Received: " << json_string << std::endl;
                json request = json::parse(json_string);
                parserErrors_ = 0; // reset parser error
                json response = HandleRequest(request); // 执行处理函数、返回结果
                if (response != nullptr) {
                    // lock_guard是C++11引入的RAII机制，用于管理互斥锁的锁定和解锁
                    // 构造lock对象的时候会自动将output_mutex_加锁
                    // 在lock对象生命周期结束时会自动解锁
                    // 这里用lock_guard来保护output_mutex_，确保在写入响应时不会被其他线程干扰
                    std::lock_guard<std::mutex> lock(output_mutex_);
                    LOG(DEBUG) << "Sending Response: " << response.dump() << std::endl;
                    // 这里是普通同步模型，所以直接调用transport_->Write把请求的
                    transport_->Write(response.dump());
                    // 在这里解锁
                }
            } catch (json::parse_error &e) {
                // 如果prase失败了，不是合法的json字符串
                // ok... what should we do in this case ? exit process ? does nothing ?
                // for now, we manage a max parser consecutive errors
                // 记录出错次数，这里设置了一个最大解析错误次数，如果超过这个次数，则停止服务器
                LOG(ERROR) << "Error parsing JSON: " << e.what() << std::endl;
                if (++parserErrors_ > MAX_PARSER_ERRORS) return false;  // **这里为什么不设置isStopping_为true？
            }
        }

        Stop();

        return true;
    }

    // **有点奇怪，先不要用这个异步
    bool Server::ConnectAsync(const std::shared_ptr<ITransport> &transport) {
        if (!transport) {
            LOG(ERROR) << "ConnectAsync called with null transport." << std::endl;
            return false;
        }

        transport_ = transport;
        isStopping_ = false;
        isAsyncCleaned_ = false;

        // Start the writer thread
        writer_running_ = true;
        writer_thread_ = std::thread(&Server::WriterLoop, this);

        // Start the async reader thread
        // 多创建一个线程用于异步读取MCP Client的请求
        reader_running_ = true;
        reader_thread_ = std::thread([this]() {
            LOG(INFO) << "Async Reader thread started." << std::endl;
            while (reader_running_ && !isStopping_) {
                try {
                    // 这里调用reader_thread_异步读取请求
                    // 先返回一个future对象，这个对象代表一个异步操作的结果
                    auto future = transport_->ReadAsync();
                    // 然后再调用future.get()获取异步操作的结果
                    auto [length, json_string] = future.get();

                    // 这里有点奇怪，为什么这里不stop了，但在reader_thread_里自己join自己也不太好，不知道要怎么修改一下
                    if (isStopping_ || (length == 0 && json_string.empty())) {
                        LOG(INFO) << "Empty message or stopping. Reader exiting.";
                        break;
                        // 这里break后读线程就结束了
                    }

                    if (!json_string.empty()) {
                        LOG(DEBUG) << "Received: " << json_string << std::endl;
                        json request = json::parse(json_string);
                        parserErrors_ = 0;

                        json response = HandleRequest(request);
                        if (response != nullptr) {
                            std::lock_guard<std::mutex> lock(output_mutex_);
                            LOG(DEBUG) << "Sending Response: " << response.dump() << std::endl;
                            transport_->Write(response.dump());
                        }
                    }
                } catch (json::parse_error &e) {
                    LOG(ERROR) << "Error parsing JSON: " << e.what() << std::endl;
                    // 如果解析错误次数超过最大解析错误次数，则停止服务器
                    if (++parserErrors_ > MAX_PARSER_ERRORS) {
                        isStopping_ = true;
                        break;
                    }
                } catch (const std::exception &e) {
                    // 如果读取线程抛出了异常，则停止服务器
                    LOG(ERROR) << "Reader thread exception: " << e.what() << std::endl;
                    isStopping_ = true;
                    break;
                }
                // 这里sleep一下，防止读取线程过于频繁地尝试读取，循环出错，导致CPU占用过高
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            LOG(INFO) << "Async Reader thread exiting." << std::endl;
        });

        return true;
    }

    void Server::Stop() {
        if (isSyncCleaned_.exchange(true)) return; // 确保清理逻辑只执行一次

        isStopping_ = true; // 同时确保循环退出

        // Stop transport (SSE shuts server down; stdio can no-op 无操作)
        if (transport_) {
            LOG(INFO) << "Stopping transport..." << std::endl;
            transport_->Stop();
            transport_.reset(); // 释放transport_智能指针
            LOG(INFO) << "Transport stopped." << std::endl;
         }

        LOG(INFO) << "Stopping server..." << std::endl;

        // Signal and join writer thread
        // 设置writer_running_为false，唤醒并告诉等待条件变量的writer_thread_退出循环
        writer_running_ = false;
        queue_cv_.notify_one(); // Wake up the writer thread if waiting
        if (writer_thread_.joinable()) {    // 先看看writer_thread_是否代表一个还没被回收的线程（是否还关联着一个没join()的真是线程）
            // 优雅地等待writer_thread_退出
            // join()会阻塞当前线程，直到writer_thread_退出
            // 退出后会自动回收writer_thread_关联的线程资源
            writer_thread_.join();
            LOG(INFO) << "Writer thread joined." << std::endl;
        }

        LOG(INFO) << "Server stopped." << std::endl;
    }

    void Server::RequestStop() {
        isStopping_.store(true);
    }


    void Server::SendNotification(const std::string& pluginName, const char* notification) {
        if (isStopping_.load()) {
            // 这里并不是代表这个server只负责这个plugin
            // 而是代表这个server正在停止，所以这个plugin不能发送通知
            LOG(WARNING) << pluginName << " attempted to send notification while server stopping." << std::endl;
            return;
        }

        // Add notification to the queue (protected by the mutex)
        {
            std::lock_guard<std::mutex> lock(output_mutex_);
            notification_queue_.emplace(notification);
        }
        queue_cv_.notify_one(); // Notify the writer thread
    }

    // 处理MCP Client的请求，校验请求、读取method、到路由表里找对应处理函数，执行处理函数、返回结果
    json Server::HandleRequest(const json &request) {
        // log the request
        if (verboseLevel_ == 1) {
            LOG(DEBUG) << "=== Request START ===" << std::endl;
            LOG(DEBUG) << request.dump(4) << std::endl; // 4缩进格式化输出请求，方便调试
            LOG(DEBUG) << "=== Request END ===" << std::endl;
        }

        // mandatory checks
        if (!request.contains("method")) {
            return MCPBuilder::Error(MCPBuilder::InvalidRequest, request["id"], "Missing method");
        }

        // handle command
        std::string methodName = request["method"];
        auto it = functionMap.find(methodName);
        // 如果找到了对应处理函数，则执行处理函数、返回结果
        if (it != functionMap.end()) {
            json response = it->second(request);
            if (response != nullptr) {
                if (verboseLevel_ == 1) {
                    LOG(DEBUG) << "=== Response START ===" << std::endl;
                    LOG(DEBUG) << response.dump(4) << std::endl;
                    LOG(DEBUG) << "=== Response END ===" << std::endl;
                }
            }
            return response;
        }

        // 如果没找到对应处理函数，则返回错误
        int id = request["id"];
        return MCPBuilder::Error(MCPBuilder::MethodNotFound, std::to_string(id), "Method not found");
    }

    bool Server::OverrideCallback(const std::string &method, std::function<json(const json &)> function) {
        // 只要这个method在路由表里存在，则替换掉
        if (functionMap.find(method) != functionMap.end()) {
            // 使用std::move将function移动到functionMap中，避免不必要的拷贝
            // 移动后原function对象不再有效，后续不再保留原值
            functionMap[method] = std::move(function);
            return true;
        }
        // 如果没找到对应处理函数，则返回false
        return false;
    }

    // 初始化命令，用于初始化客户端和服务器之间的连接
    // 主要还是打印初始化请求的信息，然后返回一个初始化响应
    json Server::InitializeCmd(const json &request) {
        LOG(INFO) << "InitializeCommand" << std::endl;
        if (request.contains("params")) {
            json params = request["params"];

            // Access rootUri 工作区根目录URI
            if (params.contains("rootUri")) {
                std::string rootUri = params["rootUri"].get<std::string>();
                LOG(INFO) << "rootUri: " << rootUri << std::endl;
            }

            // Access rootPath (deprecated)
            if (params.contains("rootPath")) {
                std::string rootPath = params["rootPath"].get<std::string>();
                LOG(INFO) << "rootPath: " << rootPath << std::endl;
            }

            // Access initializationOptions
            if (params.contains("initializationOptions")) {
                json initializationOptions = params["initializationOptions"];
                // Access specific initialization options as needed
                LOG(INFO) << "initializationOptions: " << initializationOptions.dump() << std::endl;
            }

            // Access capabilities 客户端能力声明
            if (params.contains("capabilities")) {
                json capabilities = params["capabilities"];

                // Access workspace capabilities 客户端是否支持工作区文件夹能力
                if (capabilities.contains("workspace") && capabilities["workspace"].contains("workspaceFolders")) {
                    bool workspaceFolders = capabilities["workspace"]["workspaceFolders"].get<bool>();
                    LOG(INFO) << "workspaceFolders: " << workspaceFolders << std::endl;
                }

                // Access textDocument capabilities 客户端是否支持文本文档同步能力，即变更后要怎么通知同步
                if (capabilities.contains("textDocument") && capabilities["textDocument"].contains("synchronization")) {
                    json synchronization = capabilities["textDocument"]["synchronization"];
                    if (synchronization.contains("didChange") && synchronization["didChange"].contains("synchronizationKind")){
                        int synchronizationKind = synchronization["didChange"]["synchronizationKind"].get<int>();
                        LOG(INFO) << "synchronizationKind: " << synchronizationKind << std::endl;
                    }
                }

                // Access completion capabilities 客户端是否支持补全能力
                if (capabilities.contains("textDocument") && capabilities["textDocument"].contains("completion") && capabilities["textDocument"]["completion"].contains("completionItem")) {
                    json completionItem = capabilities["textDocument"]["completion"]["completionItem"];
                    if (completionItem.contains("snippetSupport")){
                        bool snippetSupport = completionItem["snippetSupport"].get<bool>();
                        LOG(INFO) << "snippetSupport: " << snippetSupport << std::endl;
                    }
                }
            }

            // Access trace
            if (params.contains("trace")) {
                std::string trace = params["trace"].get<std::string>();
                LOG(INFO) << "trace: " << trace << std::endl;
            }

            // Access workspaceFolders array
            if (params.contains("workspaceFolders")) {
                json workspaceFoldersArray = params["workspaceFolders"];
                for (const auto& folder : workspaceFoldersArray) {
                    std::string uri = folder["uri"].get<std::string>();
                    std::string name = folder["name"].get<std::string>();
                    LOG(INFO) << "workspaceFolder uri: " << uri << " name: " << name << std::endl;
                }
            }
        }

        nlohmann::ordered_json response = {};

        response["jsonrpc"] = "2.0";
        response["id"] = request["id"];
        response["result"]["protocolVersion"] = request["params"]["protocolVersion"];
        response["result"]["capabilities"]["tools"] = json::object({{"listChanged", true}});
        response["result"]["capabilities"]["prompts"] = json::object({{"listChanged", true}});
        response["result"]["capabilities"]["resources"]["subscribe"] = true;
        response["result"]["capabilities"]["resources"]["listChanged"] = true;
        response["result"]["capabilities"]["logging"] = json::object();
        response["result"]["serverInfo"]["name"] = name_;
        response["result"]["serverInfo"]["version"] = PROJECT_VERSION;
        return response;
    }

    json Server::PingCmd(const json &request) {
        nlohmann::ordered_json response = {};
        response["jsonrpc"] = "2.0";
        response["id"] = request["id"];
        response["result"] = json::object();
        return response;
    }

    json Server::ResourcesListCmd(const json &request) {
        nlohmann::ordered_json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request["id"];
        response["result"]["resources"] = json::array();
        return response;
    }

    json Server::ResourcesReadCmd(const json &request) {
        return json();
    }

    json Server::ToolsListCmd(const json &request) {
        nlohmann::ordered_json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request["id"];
        response["result"]["tools"] = json::array();    // 工具列表，这里返回空数组
        // 会在main.cpp中OverrideCallback这个函数里填充工具列表
        return response;
    }

    json Server::ToolsCallCmd(const json &request) {
        LOG(DEBUG) << "ToolsCallCmd called" << std::endl;
        nlohmann::ordered_json response;
        nlohmann::ordered_json defaultTextContent;

        defaultTextContent["type"] = "text";
        defaultTextContent["text"] = "you should override this method in your plugin.";

        response["jsonrpc"] = "2.0";
        response["id"] = request["id"];
        response["result"]["content"] = json::array();
        response["result"]["content"].push_back(defaultTextContent);
        response["result"]["isError"] = true;

        return response;
    }

    json Server::ResourcesSubscribeCmd(const json &request) {
        LOG(WARNING) << "ResourcesSubscribeCmd called but NOT YET IMPLEMENTED" << std::endl;
        return MCPBuilder::Error(MCPBuilder::MethodNotFound, request["id"], "Method not found");
    }

    json Server::ResourcesUnsubscribeCmd(const json &request) {
        LOG(WARNING) << "ResourcesUnsubscribeCmd called but NOT YET IMPLEMENTED" << std::endl;
        return MCPBuilder::Error(MCPBuilder::MethodNotFound, request["id"], "Method not found");
    }

    json Server::PromptsListCmd(const json &request) {
        nlohmann::ordered_json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request["id"];
        response["result"]["prompts"] = json::array();
        return response;
    }

    json Server::PromptsGetCmd(const json &request) {
        return MCPBuilder::Error(MCPBuilder::MethodNotFound, request["id"], "Method not found");
    }

    json Server::LoggingSetLevelCmd(const json &request) {
        return MCPBuilder::Error(MCPBuilder::MethodNotFound, request["id"], "Method not found");
    }

    json Server::CompletionCompleteCmd(const json &request) {
        return MCPBuilder::Error(MCPBuilder::MethodNotFound, request["id"], "Method not found");
    }

    json Server::RootsListCmd(const json &request) {
        return MCPBuilder::Error(MCPBuilder::MethodNotFound, request["id"], "Method not found");
    }

    json Server::NotificationInitializedCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationCancelledCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationProgressCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationRootsListChangedCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationResourcesListChangedCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationResourcesUpdatedCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationPromptsListChangedCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationToolsListChangedCmd(const json &request) {
        return nullptr;
    }

    json Server::NotificationMessageCmd(const json &request) {
        return nullptr;
    }

    // **这个异步关闭也有点奇怪，最好别用
    void Server::StopAsync() {
        if (isAsyncCleaned_.exchange(true)) return;

        isStopping_ = true;
        LOG(INFO) << "Stopping async server..." << std::endl;

        // 为什么这里不判断transport了？

        // Stop writer thread
        writer_running_ = false;
        queue_cv_.notify_one();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
            LOG(INFO) << "Writer thread joined." << std::endl;
        }

        // Stop reader thread
        reader_running_ = false;
        if (reader_thread_.joinable()) {
            reader_thread_.join();
            LOG(INFO) << "Reader thread joined." << std::endl;
        }

        LOG(INFO) << "Async server stopped." << std::endl;
    }
}
