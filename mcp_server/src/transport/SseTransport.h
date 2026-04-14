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
//   included in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// -----------------------------------------------------------------------------
//
//  Contributors:
//    - Erdenebileg Byamba (https://github.com/erd3n)
//        * Contribution: Initial implementation of SSE Transport
//    - Giuseppe Mastrangelo (https://github.com/peppemas)
//          * Contribution: Fixed code to be compatible with MCP Server specification
//
// -----------------------------------------------------------------------------

#ifndef MCP_SERVER_SSE_TRANSPORT_HPP
#define MCP_SERVER_SSE_TRANSPORT_HPP

#include "ITransport.h"
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <httplib.h>
#include "utils/SessionBuilder.h"

namespace vx::transport {

    class SSE : public vx::ITransport {
    public:
        explicit SSE(int port = 8080, std::string  host = "127.0.0.1");
        ~SSE();

        // Copy const. and assignment disabled
        SSE(const SSE&) = delete;
        SSE& operator=(const SSE&) = delete;

        // Move semantics disabled
        SSE(SSE&&) = delete;
        SSE& operator=(SSE&&) = delete;

        // Transport interface
        std::pair<size_t, std::string> Read() override;
        void Write(const std::string& json_data) override;

        std::future<std::pair<size_t, std::string>> ReadAsync() override;
        std::future<void> WriteAsync(const std::string& json_data) override;

        std::string GetName() override { return "sse"; };
        std::string GetVersion() override { return "0.4"; };
        int GetPort() override { return port_; };

        bool Start() override;
        void Stop() override;
        bool IsRunning() override { return server_running_.load(); }

    private:
        // 设置HTTP路由
        void SetupRoutes();
        // 处理客户端建立SSE长连接请求
        void HandleSSEConnection(const httplib::Request& req, httplib::Response& res);
        // 处理客户端发来的普通POST请求
        void HandlePostMessage(const httplib::Request& req, httplib::Response& res);

        // 处理客户端OPTIONS请求
        static void HandleOptionsRequest(const httplib::Request& req, httplib::Response& res);
        // 给http响应补上一组CORS相关响应头，允许跨域访问
        static void SetCORSHeaders(httplib::Response& res);
        // 这两个函数只是处理SSE请求的辅助函数，不依赖sse对象的成员状态，所以不需要实例化

        // 内部维护了一个HTTP服务器，用于处理SSE长连接请求
        std::string host_;
        int port_;

        // SSE方式需要一个HTTP服务器
        std::unique_ptr<httplib::Server> server_;
        // 后台HTTP服务器线程
        std::thread server_thread_;
        // HTTP服务器是否正在运行
        std::atomic<bool> server_running_ {false};
        // 客户端是否已建立SSE长连接
        std::atomic<bool> client_connected_ {false};

        // Message queues for bidirectional connections
        // 消息队列，用于存储客户端发来的消息和Server需要发送给客户端的消息
        std::queue<std::string> incoming_messages_;
        std::queue<std::string> outgoing_messages_;
        // 消息队列互斥锁
        std::mutex incoming_mutex_;
        std::mutex outgoing_mutex_;
        // 消息队列条件变量
        std::condition_variable incoming_cv_;
        std::condition_variable outgoing_cv_;

        // SSE connection management
        // SSE连接是否活跃
        std::atomic<bool> sse_active_ {false};
    };

}

#endif //MCP_SERVER_SSE_TRANSPORT_HPP