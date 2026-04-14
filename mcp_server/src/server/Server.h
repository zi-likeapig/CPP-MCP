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

#ifndef MCP_SERVER_SERVER_H
#define MCP_SERVER_SERVER_H

#include <memory>
#include <queue>
#include <thread>
#include <condition_variable>
#include "ITransport.h"
#include "json.hpp"

using json = nlohmann::json;

#define MAX_PARSER_ERRORS 50

namespace vx::mcp {

    enum Capabilities {
        RESOURCES = 0 << 1,
        TOOLS = 0 << 2,
        PROMPTS = 0 << 3,
    };

    class Server {
    public:
        Server();
        ~Server();

        // Make Server non-copyable and non-movable for simplicity with threads/mutexes
        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;
        Server(Server&&) = delete;
        Server& operator=(Server&&) = delete;

        // 同步启动服务
        bool Connect(const std::shared_ptr<ITransport>& transport);
        // 异步启动服务
        bool ConnectAsync(const std::shared_ptr<ITransport> &transport);

        // 同步停止服务
        void Stop();
        // 异步停止服务
        void StopAsync();

        // 信号安全的停止请求：仅设置原子标志，不做任何线程操作。
        // 可在信号处理函数中安全调用，Connect 循环检测到标志后自行退出。
        void RequestStop();

        // 检查服务是否有效，即是否已绑定transport
        inline bool IsValid() { return transport_ != nullptr; }
        // 设置日志verbose级别，即详细程度，0不输出，1输出基本信息，2输出详细信息
        inline void VerboseLevel(int level) { verboseLevel_ = level; }
        // 设置服务名称
        inline void Name(const std::string& name) { name_ = name; }

        // 覆盖重写某个MCP方法回调，即处理逻辑
        // Server默认有一套方法处理逻辑，但也允许外部把某些方法替换掉，以此来实现类似从插件系统中加载某些插件的功能
        bool OverrideCallback(const std::string &method, std::function<json(const json&)> function);
        // 发送通知MCP Client，用于通知客户端某些事件发生
        void SendNotification(const std::string& pluginName, const char* notification);

    private:
        // 后台写线程循环从通知队列中读取通知，并发送给MCP Client
        void WriterLoop();
        // 处理MCP Client的请求，校验请求、读取method、到路由表里找对应的处理函数、执行处理函数、返回结果
        json HandleRequest(const json& request);

        json InitializeCmd(const json& request);
        json PingCmd(const json& request);
        json NotificationInitializedCmd(const json& request);

        json ToolsListCmd(const json& request);
        json ToolsCallCmd(const json& request);

        json ResourcesListCmd(const json& request);
        json ResourcesReadCmd(const json& request);
        json ResourcesSubscribeCmd(const json& request);
        json ResourcesUnsubscribeCmd(const json& request);

        json PromptsListCmd(const json& request);
        json PromptsGetCmd(const json& request);

        json LoggingSetLevelCmd(const json& request);
        json CompletionCompleteCmd(const json& request);
        json RootsListCmd(const json& request);

        json NotificationCancelledCmd(const json& request);
        json NotificationProgressCmd(const json& request);
        json NotificationRootsListChangedCmd(const json& request);
        json NotificationResourcesListChangedCmd(const json& request);
        json NotificationResourcesUpdatedCmd(const json& request);
        json NotificationPromptsListChangedCmd(const json& request);
        json NotificationToolsListChangedCmd(const json& request);
        json NotificationMessageCmd(const json& request);

    private:
        // 路由表，用于将MCP Client的请求映射到对应的处理函数（即上面那些返回json的方法）
        std::unordered_map<std::string, std::function<json(const json&)>> functionMap;

        // 服务是否正在停止
        std::atomic<bool> isStopping_{false};
        std::atomic<bool> isSyncCleaned_{false};
        std::atomic<bool> isAsyncCleaned_{false};
        
        // 日志verbose级别
        int verboseLevel_ = 0;
        // 解析错误次数
        int parserErrors_ = 0;
        // 服务名称
        std::string name_ = "mcp-server";

        // 传输层接口，用于与MCP Client通信，不绑定具体实现，可以是stdio、SSE等
        std::shared_ptr<ITransport> transport_; // Store transport pointer

        // 通知队列，用于存储需要发送给MCP Client的通知
        std::queue<std::string> notification_queue_;
        // 通知队列互斥锁
        std::mutex output_mutex_; // Protects both queue and transport writes
        // 通知队列条件变量
        std::condition_variable queue_cv_;

        // 后台写线程
        std::thread writer_thread_;
        // 后台写线程是否正在运行
        std::atomic<bool> writer_running_{false};
        // 后台读线程
        std::thread reader_thread_;
        // 后台读线程是否正在运行
        std::atomic<bool> reader_running_ = false;
    };

}

#endif //MCP_SERVER_SERVER_H
