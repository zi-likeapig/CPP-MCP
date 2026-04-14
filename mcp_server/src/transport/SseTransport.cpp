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
//          * Contribution: Initial implementation of SSE Transport
//    - Giuseppe Mastrangelo (https://github.com/peppemas)
//          * Contribution: Fixed code to be compatible with MCP Server specification
//
// -----------------------------------------------------------------------------

#include "SseTransport.h"

#include <iostream>
#include <httplib.h>
#include <chrono>
#include <utility>
#include <iomanip>

#include "aixlog.hpp"
#include "json.hpp"

namespace vx::transport {

    SSE::SSE(const int port, std::string host) : host_(std::move(host)), port_(port), server_(std::make_unique<httplib::Server>()) {
        SetupRoutes();
    }

    SSE::~SSE() {
        SSE::Stop();
    }

    std::pair<size_t, std::string> SSE::Read() {
        // 配合着incoming_cv_条件变量，互斥访问incoming_messages_队列，会在wait里解锁等待
        std::unique_lock<std::mutex> lock(incoming_mutex_);

        LOG(TRACE) << "Read waiting for incoming message or shutdown signal" << std::endl;
        LOG(TRACE) << "queue_empty: " << incoming_messages_.empty() << std::endl;
        LOG(TRACE) << "server_running: " << server_running_.load() << std::endl;

        incoming_cv_.wait(lock, [this]() {
            // 如果队伍非空，或者服务器停机了，就唤醒等待的线程
            return !incoming_messages_.empty() || !server_running_.load();
        });

        LOG(TRACE) << "Read resumed after wait; lock reacquired" << std::endl;

        // 如果服务器停机了，并且队伍为空，则返回空字符串
        if (!server_running_.load() && incoming_messages_.empty()) {
            return {0, ""};
        }

        // 如果队伍非空，则取出第一个消息，并返回
        if (!incoming_messages_.empty()) {
            std::string message = incoming_messages_.front();
            incoming_messages_.pop();
            return { message.length(), message };
        } 
        else {
            return {0, ""};
        }
    }

    void SSE::Write(const std::string& json_data) {
        LOG(TRACE) << "RequestHandler delegated = " << json_data << std::endl;
        LOG(TRACE) << "is_client_connected = " << client_connected_.load() << std::endl;
        if (!client_connected_.load()) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(outgoing_mutex_);
            outgoing_messages_.push(json_data);
        }

        outgoing_cv_.notify_one();
    }

    std::future<std::pair<size_t, std::string>> SSE::ReadAsync() {
        return std::async(std::launch::async, [this]() -> std::pair<size_t, std::string> {
            LOG(TRACE) << "READ ASYNC CALLED!!!" << std::endl;
            return Read();
        });
    }

    std::future<void> SSE::WriteAsync(const std::string& json_data) {
        return std::async(std::launch::async, [this, json_data] () {
            Write(json_data);
        });
    }

    bool SSE::Start() {
        // 如果服务器已经运行了，就直接返回false
        if (server_running_.load()) {
            return false;   // **为什么不返回true？
        }

        server_running_.store(true);

        server_thread_ = std::thread([this]() {
            LOG(INFO) << "Starting SSE server on " << host_ << ":" << port_ << std::endl;

            if (!server_->listen(host_.c_str(), port_)) {   // 这个server_不是server.h里的server_，而是httplib::Server对象
                LOG(ERROR) << "Failed to start SSE server on " << host_ << ":" << port_ << std::endl;
                server_running_.store(false);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return server_running_.load();
    }

    void SSE::Stop() {
        if (!server_running_.load()) {
            return;
        }

        server_running_.store(false);
        client_connected_.store(false);
        sse_active_.store(false);

        if (server_) {
            server_->stop();
        }

        if (server_thread_.joinable()) {
            server_thread_.join();
        }

        incoming_cv_.notify_all();
        outgoing_cv_.notify_all();
    }

    // 设置HTTP路由，注册所有路径的请求处理函数
    void SSE::SetupRoutes() {
        // 注册所有路径的OPTIONS请求处理函数，处理跨域预检
        server_->Options("/.*", [this](const httplib::Request& req, httplib::Response& res) {
            HandleOptionsRequest(req, res);
        });

        // 注册GET /health请求处理函数，处理客户端的健康检查请求
        server_->Get("/health", [](const httplib::Request& req, httplib::Response& res) {
            res.set_content("{\"status\" : \"ok\"}", "application/json");
        });

        // 注册POST /messages请求处理函数，处理客户端发来的普通POST请求
        server_->Post("/messages", [this](const httplib::Request& req, httplib::Response& res) {
            HandlePostMessage(req, res);
        });

        // 注册GET /sse请求处理函数，处理客户端建立SSE长连接请求
        server_->Get("/sse", [this](const httplib::Request& req, httplib::Response& res) {
            HandleSSEConnection(req, res);
        });
    }

    void SSE::HandleSSEConnection(const httplib::Request& req, httplib::Response& res) {
        LOG(DEBUG) << "SSE client connected" << std::endl;
        LOG(DEBUG) << "Request headers:" << std::endl;
        for (const auto &header: req.headers) {
            LOG(DEBUG) << " - " << header.first << ": " << header.second << std::endl;
        }
        // 打印请求方法、路径、版本、远程地址、远程端口，这些连接元信息属于底层TCP连接，不在HTTP请求头里
        LOG(DEBUG) << "Request method: " << req.method << std::endl;
        LOG(DEBUG) << "Request path: " << req.path << std::endl;
        LOG(DEBUG) << "Request version: " << req.version << std::endl;
        LOG(DEBUG) << "Request remote address: " << req.remote_addr << std::endl;
        LOG(DEBUG) << "Request remote port: " << req.remote_port << std::endl;

        SetCORSHeaders(res);
        res.set_header("Content-Type", "text/event-stream"); // 设置响应内容类型为text/event-stream，这是SSE协议规定的
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive"); // 设置连接为长连接

        client_connected_.store(true);  // 存在一个活跃的SSE客户端连接
        sse_active_.store(true);  // 这个SSE推送循环是活跃状态

        // 设置响应内容提供者，用于提供SSE数据流，server不断地给client推送数据
        // 普通的set_content函数只能一次性发送一个固定的内容，而set_content_provider可以动态地提供数据流
        // 所以这里要使用这个sink对象来提供数据流
        res.set_content_provider(
            "text/event-stream",
            [this](size_t offset, httplib::DataSink& sink) -> bool {
                using clock = std::chrono::steady_clock;
                static thread_local bool first_call = true; // 第一次调用
                static thread_local auto last_ping = clock::now(); // 上一次ping的时间
                const auto ping_interval = std::chrono::seconds(15); // 心跳间隔时间

                // 定义一个终止函数，用于终止SSE数据流
                auto terminate = [this]() -> bool {
                    sse_active_.store(false); // 设置SSE推送循环为不活跃状态
                    client_connected_.store(false); // 设置客户端连接为不活跃状态
                    outgoing_cv_.notify_all(); // 唤醒所有等待的写线程
                    incoming_cv_.notify_all(); // 唤醒所有等待的读线程
                    return false;
                };

                // 尝试提供SSE数据流
                try {
                    // 仅第一次调用时，发送一个事件端点消息，告诉client端点地址sessionID
                    if (first_call) {
                        first_call = false;

                        std::string sessionId = vx::utils::SessionBuilder::GenerateUniqueSessionID();
                        std::string event_endpoint = "event: endpoint\ndata: /messages?session_id=" + sessionId + "\n\n";
                        if (!sink.write(event_endpoint.data(), event_endpoint.size())) {
                            LOG(ERROR) << "Failed to write event_endpoint message" << std::endl;
                            return terminate();
                        }
                        last_ping = clock::now();
                    }

                    // Periodically send keep-alive to detect broken connection
                    // 定期发送心跳消息保持长连接，如果对方断开连接，也可以即时检测到，并终止SSE数据流
                    if (clock::now() - last_ping >= ping_interval) {
                        // SSE comment line as keep-alive
                        const char* ping = ": ping\n\n";    // ：开头表示是注释行，不会被client解析为数据，\n\n表示换行符和两个空行
                        // If write fails, client disconnected (e.g., Ctrl+C)，如果写失败，则认为客户端断开连接
                        if (!sink.write(ping, std::strlen(ping))) {
                            LOG(ERROR) << "Keep-alive write failed; client likely disconnected" << std::endl;
                            return terminate();
                        }
                        last_ping = clock::now();
                    }

                    // If there’s no message soon, re-check writability again
                    // 注意: 这个和server.cpp里WriteLoop使用的notification_queue_不一样
                    //      那个是通知的队列，这里是所有trasport->write()写入后传输层准备由sink发送的输出队列
                    std::unique_lock<std::mutex> lock(outgoing_mutex_);
                    outgoing_cv_.wait_for(lock, std::chrono::milliseconds(200), [this]() {
                        return !outgoing_messages_.empty() || !sse_active_.load();
                    });

                    if (!sse_active_.load()) {
                        LOG(ERROR) << "SSE connection terminating (inactive)" << std::endl;
                        return terminate();
                    }

                    // Before sending, verify connection looks writable
                    // If the client Ctrl+C'd, this check or the subsequent write will fail.
                    // Note: is_writable() exists on cpp-httplib DataSink; if missing on your version,
                    // the failed write below will handle it.
#ifdef CPPHTTPLIB_HAS_SINK_IS_WRITABLE  // 如果当前cpp-httplib版本支持is_writable()函数，则检查连接是否可写
                    if (!sink.is_writable()) { // 如果连接不可写，则认为客户端断开连接
                        LOG(DEBUG) << "Sink no longer writable; client likely disconnected" << std::endl;
                        return terminate();
                    }
#endif  // 如果当前cpp-httplib版本不支持is_writable()函数，则不检查连接是否可写

                    if (!outgoing_messages_.empty()) {
                        std::string message = outgoing_messages_.front();
                        outgoing_messages_.pop();
                        lock.unlock();  // 释放互斥锁

                        std::string sse_msg = "data: " + message + "\n\n";
                        LOG(DEBUG) << "Sending SSE message: " << message << std::endl;

                        if (!sink.write(sse_msg.data(), sse_msg.size())) {
                            LOG(ERROR) << "Failed to write SSE message; client disconnected" << std::endl;
                            return terminate();
                        }
                    }

                    return true; // continue streaming
                } catch (const std::exception& ex) {
                    LOG(ERROR) << "Exception in SSE content provider: " << ex.what() << std::endl;
                    return terminate();
                } catch (...) {
                    LOG(ERROR) << "Unknown exception in SSE content provider" << std::endl;
                    return terminate();
                }
            }
        );
    }

    void SSE::HandlePostMessage(const httplib::Request& req, httplib::Response& res) {
        SetCORSHeaders(res);

        // 如果客户端没有建立SSE长连接，则返回503服务不可用
        if (!client_connected_.load()) {
            res.status = 503;
            res.set_content("{\"error\":\"No SSE connection\"}", "application/json");
            return;
        }

        // 请求体，一般是规定好的json字符串
        std::string message = req.body;

        // 如果请求体为空，则返回400请求错误
        if (message.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"Empty message\"}", "application/json");
            return;
        }

        LOG(DEBUG) << "Received message via POST: " << message << std::endl; 
        {
            std::lock_guard<std::mutex> lock(incoming_mutex_);
            incoming_messages_.push(message);
            // 将请求体放入incoming_messages_队列，并唤醒一个等待的读线程读取
        }
        incoming_cv_.notify_one();

        // 返回200请求成功
        res.status = 200;
        res.set_content("{\"status\":\"received\"}", "application/json");
    }

    // 只是一个预检请求，浏览器会先发送一个OPTIONS请求来检查是否支持跨域请求
    void SSE::HandleOptionsRequest(const httplib::Request& req, httplib::Response& res) {
        SetCORSHeaders(res);
        res.status = 200;
    }

    void SSE::SetCORSHeaders(httplib::Response& res) {
        // 这些都是规范里规定的，不能乱写要按需配置，浏览器会检查这些头是否存在判断是否支持跨域请求
        // 所有的响应都应该返回这些头，否则client tcp层能收到，但浏览器会拒绝请求
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, x-api-key");
        res.set_header("Access-Control-Expose-Headers", "Content-Type, Authorization, x-api-key");
        res.set_header("Access-Control-Max-Age", "86400");
    }

}
