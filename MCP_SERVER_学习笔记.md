## 学习说明

这份文档按“学习博客”的方式整理我当前已经学过的 `mcp_server` 核心代码。本文先不展开 `main.cpp` 和插件实现，重点聚焦下面这些文件：

- `mcp_server/src/interface/ITransport.h`
- `mcp_server/src/server/Server.h`
- `mcp_server/src/server/Server.cpp`
- `mcp_server/src/transport/StdioTransport.h`
- `mcp_server/src/transport/StdioTransport.cpp`
- `mcp_server/src/transport/SseTransport.h`
- `mcp_server/src/utils/MCPBuilder.h`

这篇笔记的目标不是把每行代码翻译一遍，而是像 CSDN 学习博客那样，按文件和关键函数组织内容，讲清楚：

- 这个文件是干什么的
- 这个函数的核心逻辑是什么
- 代码里哪些地方值得重点注意
- 哪些点适合面试时讲

## `mcp_server` 重要文件导览

先把整个 `mcp_server` 里最重要的文件按学习顺序列出来，后面读代码时不容易乱。

| 文件 | 作用 | 当前建议掌握程度 |
| --- | --- | --- |
| `src/main.cpp` | 程序入口、transport 选择、插件加载、重写默认回调 | 下一阶段重点 |
| `src/server/Server.h` | `Server` 类定义，展示类职责和内部结构 | 必须掌握 |
| `src/server/Server.cpp` | 请求分发、同步/异步运行、通知、停机、初始化握手 | 必须掌握 |
| `src/interface/ITransport.h` | transport 抽象接口 | 必须掌握 |
| `src/transport/StdioTransport.h/.cpp` | 最简单的 transport 实现 | 必须掌握 |
| `src/transport/SseTransport.h/.cpp` | 较复杂的网络型 transport 实现 | 需要理解设计 |
| `src/utils/MCPBuilder.h` | 统一构造 MCP 响应和错误消息 | 建议掌握 |
| `src/loader/PluginsLoader.*` | 插件加载器 | 后续重点 |
| `src/interface/PluginAPI.h` | 插件接口定义 | 后续重点 |
| `plugins/*` | 具体插件实现 | 后续重点 |

我目前这篇文章主要整理前 7 个文件，尤其是 `Server` 和 `ITransport` 这一条主线。

## 一、先看 `ITransport.h`：为什么 server 一上来要抽象 transport

### 1. 这个文件在整个项目里的定位

`ITransport.h` 是整个 `mcp_server` 非常基础的一层。它不是在干业务，而是在定义一个问题：

“无论消息是通过 `stdio` 传，还是通过 `sse` 传，只要上层 `Server` 想处理 MCP 协议，它最少应该依赖哪些能力？”

这就是为什么这个文件里全是接口，没有实现。

### 2. 关键代码

```cpp
class ITransport {
public:
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() = 0;

    virtual std::pair<size_t, std::string> Read() = 0;
    virtual void Write(const std::string& json_data) = 0;

    virtual std::future<std::pair<size_t, std::string>> ReadAsync() = 0;
    virtual std::future<void> WriteAsync(const std::string& json_data) = 0;

    virtual std::string GetName() = 0;
    virtual std::string GetVersion() = 0;
    virtual int GetPort() = 0;
};
```

### 3. 这个接口到底规定了什么

从这段代码里可以直接看出，它要求所有 transport 都要支持三类能力。

第一类是生命周期管理：

- `Start()`
- `Stop()`
- `IsRunning()`

第二类是消息收发：

- 同步：`Read()`、`Write()`
- 异步：`ReadAsync()`、`WriteAsync()`

第三类是元信息：

- `GetName()`
- `GetVersion()`
- `GetPort()`

这意味着上层 `Server` 不关心底层究竟是读 `stdin`，还是监听 HTTP/SSE，它只关心“能不能读消息、写消息、启动、停止”。

### 4. 学这一段最该注意什么

这里有几个很容易混的知识点。

第一，`= 0` 表示纯虚函数，所以 `ITransport` 是抽象类。这不是“以后补实现”，而是“子类必须实现”。

第二，`ReadAsync()` 返回的是 `std::future`。这表示“未来某个时间点会得到读取结果”，但不等于这份代码就已经是完全事件驱动异步。

第三，transport 是“消息怎么传”的问题，而 MCP 协议版本是“消息内容按哪版规范组织”的问题，这两个层次不能混。

### 5. 这一节的学习结论

`ITransport` 是整个 server 的解耦基础。没有它，`Server` 就会被迫绑定到某一种通信方式；有了它，`Server` 才能只专注于 MCP 协议处理。

## 二、看 `StdioTransport`：最适合入门的 transport

### 1. 为什么先学 `StdioTransport`

因为它最简单。

`stdio` 模式下没有 HTTP 服务器、没有网络连接、没有复杂路由，只有两件事：

- 从标准输入读 JSON 文本
- 往标准输出写 JSON 文本

所以如果 transport 一开始就去啃 `SSE`，很容易被网络和并发细节淹没。

### 2. 头文件里最值得看的内容

```cpp
class Stdio : public vx::ITransport {
public:
    std::pair<size_t, std::string> Read() override;
    void Write(const std::string& json_data) override;

    std::future<std::pair<size_t, std::string>> ReadAsync() override;
    std::future<void> WriteAsync(const std::string& json_data) override;

    std::string GetName() override { return "stdio"; }
    std::string GetVersion() override { return "0.2"; }
    int GetPort() override { return 0; }

    bool Start() override { return true; }
    void Stop() override {}
    bool IsRunning() override { return true; }
};
```

### 3. 这段代码说明了什么

这段头文件已经说明了 `stdio` 模式的本质：

- `GetName()` 返回 `stdio`
- 没有实际网络端口，所以 `GetPort()` 是 `0`
- `Start()` 基本是空操作，直接返回 `true`
- `Stop()` 也是空实现

这和 `SSE` 很不一样。`stdio` 本身不是一个需要“启动服务器”的通信方式，它只是依赖标准输入输出流，所以这几个函数在这里更像是为了满足统一接口而存在。

### 4. `Read()` 是真正值得重点学的地方

当前我们把 `Read()` 改成了更鲁棒的版本，关键代码如下：

```cpp
std::pair<size_t, std::string> Stdio::Read() {
    while (true) {
        std::string json_data;
        int c;

        while ((c = std::getc(stdin)) != EOF && c != '\n') {
            json_data += static_cast<char>(c);
        }

        if (c == EOF) {
            if (!json_data.empty()) {
                return {json_data.length(), json_data};
            }
            return {0, ""};
        }

        if (json_data.empty()) {
            continue;
        }

        return {json_data.length(), json_data};
    }
}
```

### 5. 这个函数干了什么

这个函数的逻辑可以按四种情况理解：

第一种，正常读到一整行 JSON，然后遇到换行符 `\n`。
这时候返回这整行字符串。

第二种，只读到空行 `\n`。
这时候不返回，而是 `continue`，继续等下一条消息。

第三种，读到一段文本后遇到 EOF。
这说明输入流结束了，但当前这条消息本身是有内容的，所以先把这条消息返回。

第四种，一开始就遇到 EOF。
这时说明底层输入流真正结束，返回 `{0, ""}` 给上层，让上层把它当成连接结束。

### 6. 这里最值得注意的点

这个函数其实帮我彻底理清了“暂时没消息”和“连接断开”的区别。

- 暂时没消息时，`getc(stdin)` 会阻塞等待
- 真正断开时，才会读到 EOF

所以不能把“空返回”简单理解成“当前没消息”，空返回在这里更接近“已经没法继续读了”。

另外，空行和 EOF 必须区分开，否则 server 会把空行误判成断开，这正是我们前面修掉的问题。

### 7. `ReadAsync()` 为什么看起来还是有点同步味

```cpp
std::future<std::pair<size_t, std::string>> Stdio::ReadAsync() {
    return std::async(std::launch::async, [this]() {
        return Read();
    });
}
```

它的本质是“把同步 `Read()` 包装进一个异步任务里”，而不是从底层上彻底变成非阻塞 I/O。

这也是后面理解 `ConnectAsync()` 时非常关键的一个点：当前这套异步，更像“线程异步”，而不是“底层 I/O 完全异步”。

## 三、看 `SseTransport.h`：为什么它明显比 `stdio` 复杂

### 1. 先不要逐行啃 `SSE`，先抓住角色变化

`SSE` 和 `stdio` 的最大不同，不在于“换了个通信方式”，而在于它内部自己维护了一个 HTTP 服务端。

也就是说：

- `stdio` 只是读写已有输入输出流
- `SSE` 自己还要负责监听、路由、长连接和消息队列

### 2. 头文件里的核心代码

```cpp
class SSE : public vx::ITransport {
public:
    explicit SSE(int port = 8080, std::string host = "127.0.0.1");
    ~SSE();

    std::pair<size_t, std::string> Read() override;
    void Write(const std::string& json_data) override;

    std::future<std::pair<size_t, std::string>> ReadAsync() override;
    std::future<void> WriteAsync(const std::string& json_data) override;

    bool Start() override;
    void Stop() override;
    bool IsRunning() override { return server_running_.load(); }
```

再往下看成员变量：

```cpp
std::unique_ptr<httplib::Server> server_;
std::thread server_thread_;
std::atomic<bool> server_running_ {false};
std::atomic<bool> client_connected_ {false};

std::queue<std::string> incoming_messages_;
std::queue<std::string> outgoing_messages_;
std::mutex incoming_mutex_;
std::mutex outgoing_mutex_;
std::condition_variable incoming_cv_;
std::condition_variable outgoing_cv_;
```

### 3. 这一段代码透露出的设计

这份设计说明 `SSE` 不只是“另一种读写方式”，它实际上包含了三个角色：

- HTTP 服务管理者
- SSE 长连接维护者
- 输入输出消息队列管理者

上层 `Server` 仍然只会调用 `Read()` 和 `Write()`，但在 `SSE` 内部，这两个动作已经被拆成了“入队/出队”的模型。

### 4. 配合 `SseTransport.cpp` 看运行模型

几个最重要的函数如下。

#### `Read()`

```cpp
std::pair<size_t, std::string> SSE::Read() {
    std::unique_lock<std::mutex> lock(incoming_mutex_);
    incoming_cv_.wait(lock, [this]() {
        return !incoming_messages_.empty() || !server_running_.load();
    });

    if (!server_running_.load() && incoming_messages_.empty()) {
        return {0, ""};
    }

    if (!incoming_messages_.empty()) {
        std::string message = incoming_messages_.front();
        incoming_messages_.pop();
        return { message.length(), message };
    } else {
        return {0, ""};
    }
}
```

这段的意思是：没有消息时不是忙等，而是睡眠等待；队列有消息就取出来；如果服务都停了而且队列也空了，就返回空结果。

#### `Write()`

```cpp
void SSE::Write(const std::string& json_data) {
    if (!client_connected_.load()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(outgoing_mutex_);
        outgoing_messages_.push(json_data);
    }

    outgoing_cv_.notify_one();
}
```

这段告诉我们：`Write()` 并不是立刻直接把内容发到网络上，而是先把消息放到输出队列，再通知等待发送的一侧。

#### `Start()`

```cpp
bool SSE::Start() {
    if (server_running_.load()) {
        return false;
    }

    server_running_.store(true);

    server_thread_ = std::thread([this]() {
        if (!server_->listen(host_.c_str(), port_)) {
            server_running_.store(false);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return server_running_.load();
}
```

这里能看出：`SSE` 真的会起一个后台 HTTP 服务线程，这就是为什么它的 `Start()` 和 `Stop()` 不可能像 `stdio` 一样简单。

### 5. 这一节的学习重点

学习 `SSE` 不用强求把每个路由实现都记住，但一定要理解：

- 为什么它需要自己的 `server_thread_`
- 为什么输入输出都要走队列
- 为什么它仍然能对上层暴露统一的 `Read()` / `Write()` 接口

这就是 transport 抽象真正发挥价值的地方。

## 四、看 `Server.h`：先把类的结构图搭起来

### 1. 为什么头文件值得认真读

很多时候头文件比实现更适合入门，因为它会先告诉你这个类“打算做什么”。`Server.h` 就非常适合拿来搭结构图。

### 2. 类的核心公开接口

```cpp
bool Connect(const std::shared_ptr<ITransport>& transport);
bool ConnectAsync(const std::shared_ptr<ITransport> &transport);

void Stop();
void StopAsync();

inline bool IsValid() { return transport_ != nullptr; }
inline void VerboseLevel(int level) { verboseLevel_ = level; }
inline void Name(const std::string& name) { name_ = name; }

bool OverrideCallback(const std::string &method, std::function<json(const json&)> function);
void SendNotification(const std::string& pluginName, const char* notification);
```

### 3. 这一组接口说明了什么

从功能角度可以把它们分成四组：

第一组，生命周期：

- `Connect`
- `ConnectAsync`
- `Stop`
- `StopAsync`

第二组，简单配置：

- `IsValid`
- `VerboseLevel`
- `Name`

第三组，协议扩展：

- `OverrideCallback`

第四组，主动通知：

- `SendNotification`

这说明 `Server` 不是单纯的 JSON 处理类，而是一个完整的“协议运行核心”。

### 4. 内部成员最值得看的地方

```cpp
std::unordered_map<std::string, std::function<json(const json&)>> functionMap;

bool isStopping_ = false;
int verboseLevel_ = 0;
int parserErrors_ = 0;
std::string name_ = "mcp-server";

std::shared_ptr<ITransport> transport_;
std::queue<std::string> notification_queue_;
std::mutex output_mutex_;
std::condition_variable queue_cv_;
std::thread writer_thread_;
std::atomic<bool> writer_running_{false};
std::thread reader_thread_;
std::atomic<bool> reader_running_ = false;
```

### 5. 这里该怎么理解

这一段其实已经把整个 `Server` 的运行模型暴露出来了：

- `functionMap` 负责 method 路由
- `transport_` 是底层通信抽象
- `notification_queue_` 负责承接待发送通知
- `writer_thread_` 是通知写线程
- `reader_thread_` 是异步模式下的读线程

读到这里时我最大的感受是：`Server` 不是一个轻量工具类，而是一个带并发和生命周期管理的核心组件。

### 6. 这里需要特别注意的点

`Capabilities` 这个枚举当前写成了：

```cpp
enum Capabilities {
    RESOURCES = 0 << 1,
    TOOLS = 0 << 2,
    PROMPTS = 0 << 3,
};
```

这个定义看起来很可疑，因为 `0 << anything` 结果都是 `0`。我当前的结论是：这里更像作者原本想表达能力位标志，但这段定义本身并不合理，至少目前不适合死背。

## 五、看 `Server.cpp`：真正的骨架都在这里

这部分是当前学习的重点，也是最容易被“默认壳子函数”干扰的地方。正确的看法不是把整份 `Server.cpp` 平铺看完，而是先抓最核心的骨架函数。

### 1. `Server::Server()`：先把路由表建立起来

关键代码：

```cpp
Server::Server() {
    functionMap = {
        {"initialize", [this](const json& req) { return this->InitializeCmd(req); }},
        {"ping", [this](const json& req) { return this->PingCmd(req); }},
        {"resources/list", [this](const json& req) { return this->ResourcesListCmd(req); }},
        {"resources/read", [this](const json& req) { return this->ResourcesReadCmd(req); }},
        {"tools/list", [this](const json& req) { return this->ToolsListCmd(req); }},
        {"tools/call", [this](const json& req) { return this->ToolsCallCmd(req); }},
        {"prompts/list", [this](const json& req) { return this->PromptsListCmd(req); }},
        {"prompts/get", [this](const json& req) { return this->PromptsGetCmd(req); }},
        ...
    };
}
```

### 2. 这个函数到底在干什么

它就是在做一件事：把 MCP 的方法名和对应处理函数绑起来。

以后只要 `HandleRequest()` 拿到一个 `method`，比如：

- `initialize`
- `tools/list`
- `prompts/get`

就可以去 `functionMap` 里找到对应处理函数。

### 3. 这里值得注意的知识点

这一段里有两个现代 C++ 关键语法点：

第一，`std::function<json(const json&)>` 用来统一包装“可调用对象”。

第二，这里存进去的不是普通函数指针，而是 lambda：

```cpp
[this](const json& req) { return this->InitializeCmd(req); }
```

也就是说，路由表里存的不是方法名字符串，而是“真正可以被调用的处理逻辑”。

### 4. 学完这一节应该知道什么

`Server` 的本质不是 if-else 大杂烩，而是一个**基于路由表的请求分发器**。

### 5. `HandleRequest()`：整个 server 的分发中枢

关键代码：

```cpp
json Server::HandleRequest(const json &request) {
    if (!request.contains("method")) {
        return MCPBuilder::Error(MCPBuilder::InvalidRequest, request["id"], "Missing method");
    }

    std::string methodName = request["method"];
    auto it = functionMap.find(methodName);
    if (it != functionMap.end()) {
        json response = it->second(request);
        return response;
    }

    int id = request["id"];
    return MCPBuilder::Error(MCPBuilder::MethodNotFound, std::to_string(id), "Method not found");
}
```

### 6. 这个函数做了什么

可以直接按 4 步记：

1. 检查请求里是否有 `method`
2. 读取 `methodName`
3. 去 `functionMap` 里查找
4. 找到就执行，找不到就返回 `MethodNotFound`

### 7. 为什么这里用 `find()` 而不是 `count() + []`

这是一个很典型的 C++ 容器习惯用法。

- `find()` 一次查找就拿到迭代器
- `count() + operator[]` 可能需要两次查找
- `operator[]` 在 key 不存在时还有插入默认值的副作用

所以这里的写法更标准，也更安全。

### 8. 学这一节最该掌握的点

`HandleRequest()` 不关心某个 tool 具体怎么执行，它只负责“路由”和“错误兜底”。

## 六、看 `Connect()`：同步版主循环到底怎么跑

### 1. 关键代码

```cpp
bool Server::Connect(const std::shared_ptr<ITransport> &transport) {
    transport_ = transport;
    isStopping_ = false;

    writer_running_ = true;
    writer_thread_ = std::thread(&Server::WriterLoop, this);

    if (!transport_->Start()) {
        return false;
    }

    while (!isStopping_) {
        auto [length, json_string] = transport->Read();
        if (isStopping_) break;

        if (length == 0 && json_string.empty()) {
            isStopping_ = true;
            break;
        }

        try {
            if (json_string.empty()) continue;
            json request = json::parse(json_string);
            parserErrors_ = 0;
            json response = HandleRequest(request);
            if (response != nullptr) {
                std::lock_guard<std::mutex> lock(output_mutex_);
                transport_->Write(response.dump());
            }
        } catch (json::parse_error &e) {
            if (++parserErrors_ > MAX_PARSER_ERRORS) return false;
        }
    }

    Stop();
    return true;
}
```

### 2. 这个函数干了什么

同步版 `Connect()` 可以理解成一个完整的 server 主循环：

第一步，保存 `transport_` 并重置 `isStopping_`。

第二步，先起一个 `writer_thread_`，它不负责读请求，而是负责发送 notification。

第三步，启动 transport。对于 `SSE` 来说会真正起 HTTP 服务，对于 `stdio` 来说就是一个空操作。

第四步，主线程开始阻塞读请求：

- `Read()`
- `json::parse`
- `HandleRequest()`
- `Write()`

最后，当循环退出时，统一调用 `Stop()`。

### 3. 这里最值得注意的几个点

第一个点，同步版没有独立 reader 线程。主线程自己负责读取请求。

第二个点，`Read()` 返回 `{0, ""}` 会被视为连接结束信号。

第三个点，写响应前要加 `output_mutex_`，因为此时还有一个通知写线程也可能写 transport。

第四个点，`json::parse_error` 超过阈值时目前直接 `return false`，这会绕过函数尾部的 `Stop()`。这条错误路径从设计上还有优化空间。

### 4. 这一节学习后的结论

同步版 `Connect()` 才是当前主程序真正使用的主流程，也是整个 server 最核心的运行入口。

## 七、看 `WriterLoop()` 和 `SendNotification()`：为什么通知要单独走一条线

### 1. `SendNotification()`：先入队，再唤醒写线程

关键代码：

```cpp
void Server::SendNotification(const std::string& pluginName, const char* notification) {
    if (isStopping_) {
        LOG(WARNING) << pluginName << " attempted to send notification while server stopping." << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        notification_queue_.emplace(notification);
    }
    queue_cv_.notify_one();
}
```

### 2. 这个函数干了什么

它不是直接写 transport，而是先把通知压进队列，再通过 `notify_one()` 把写线程叫醒。

这里日志里带 `pluginName` 不是因为一个 plugin 对应一个 server，而是为了排查“停机阶段是谁还在尝试发通知”。

### 3. `WriterLoop()`：真正负责把通知写出去

关键代码：

```cpp
void Server::WriterLoop() {
    while (writer_running_.load()) {
        std::string notification_to_send;
        {
            std::unique_lock<std::mutex> lock(output_mutex_);
            queue_cv_.wait(lock, [this] { return !notification_queue_.empty() || !writer_running_.load(); });

            if (!writer_running_.load() && notification_queue_.empty()) {
                break;
            }

            if (!notification_queue_.empty()) {
                notification_to_send = std::move(notification_queue_.front());
                notification_queue_.pop();
            }
        }

        if (!notification_to_send.empty() && transport_) {
            std::lock_guard<std::mutex> write_lock(output_mutex_);
            if (transport_) {
                transport_->Write(notification_to_send);
            }
        }
    }
}
```

### 4. 为什么这里要单独开线程

因为通知不是“收到请求后立刻回应”的那种模式，它可能在任意时刻由插件主动触发。

如果不单独抽线程，你就要么：

- 到处直接 `transport_->Write()`
- 要么把通知发送硬塞进主循环

现在这种设计的好处是：

- 普通响应仍然由主线程写
- 通知统一由 `WriterLoop()` 写
- 真正写 transport 时再用同一把 `output_mutex_` 做互斥

这就避免了多个地方无序地同时写 transport。

### 5. 这段代码里最值得注意的并发知识

第一个知识点是 `condition_variable::wait(lock, predicate)`。它本质上已经等价于“带 while 的等待模式”，所以不需要你手工再套一层 `while`。

第二个知识点是 `unique_lock`。这里不能用 `lock_guard`，因为 `wait()` 需要在内部自动解锁和重新加锁。

第三个知识点是那层额外的 `{}`。它不是多余，而是为了让锁作用域更小，在真正写 transport 之前先释放队列相关锁。

## 八、看 `Stop()`：server 是怎么优雅停机的

### 1. 关键代码

```cpp
void Server::Stop() {
    isStopping_ = true;

    if (transport_) {
        transport_->Stop();
        transport_.reset();
    }

    writer_running_ = false;
    queue_cv_.notify_one();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    reader_running_ = false;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
}
```

### 2. 这个函数干了什么

停机过程可以分成四步：

第一步，设置全局停止标志 `isStopping_ = true`。

第二步，停止 transport。对于 `SSE` 来说是真正停 HTTP 服务；对于 `stdio` 来说基本是 no-op。

第三步，通知 writer 线程退出，并 `join()` 它。

第四步，如果存在 reader 线程，也把它回收掉。

### 3. 为什么 `Stop()` 后还要 `transport_.reset()`

因为 `Stop()` 只是“让底层不再运行”，`reset()` 则表示“当前 server 不再持有这个 transport 对象”，属于资源释放层面的收口。

### 4. `joinable()` 和 `join()` 到底是什么意思

- `joinable()`：这个 `std::thread` 对象当前是否还关联着一个可以 `join` 的真实线程
- `join()`：当前线程在这里等待，直到那个线程真正结束并回收线程资源

注意，`join()` 不是“把线程杀掉”。真正让线程退出的是：

- 改运行标志
- 唤醒条件变量
- 线程自己跳出循环

`join()` 只是等待它优雅结束。

## 九、看 `ConnectAsync()`：为什么它看起来有点“异步但又不完全异步”

### 1. 关键代码

```cpp
reader_running_ = true;
reader_thread_ = std::thread([this]() {
    while (reader_running_ && !isStopping_) {
        try {
            auto future = transport_->ReadAsync();
            auto [length, json_string] = future.get();

            if (isStopping_ || (length == 0 && json_string.empty())) {
                break;
            }

            if (!json_string.empty()) {
                json request = json::parse(json_string);
                parserErrors_ = 0;
                json response = HandleRequest(request);
                if (response != nullptr) {
                    std::lock_guard<std::mutex> lock(output_mutex_);
                    transport_->Write(response.dump());
                }
            }
        } catch (...) {
            ...
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
});
```

### 2. 为什么它让我觉得“有点奇怪”

因为它虽然叫 `ConnectAsync()`，也确实起了一个 `reader_thread_`，但 reader 线程内部又是：

- `ReadAsync()`
- 紧接着 `future.get()`

这意味着对这个 reader 线程来说，它仍然是在等待结果。

所以更准确的说法是：

- 它实现了“对调用者异步”
- 但没有做到“底层 I/O 完全事件驱动异步”

### 3. 当前对它的学习结论

我当前把这部分先理解为：

- 它在思路上是“把读请求这件事搬到后台线程”
- 但实现上还不够打磨
- 当前项目主程序也没有使用它，所以不需要把它当成主线深挖

它更适合作为“可讨论的扩展设计”和“值得质疑的实现点”来记。

## 十、看 `InitializeCmd()`：MCP 握手到底在干什么

### 1. 关键代码

```cpp
json Server::InitializeCmd(const json &request) {
    if (request.contains("params")) {
        json params = request["params"];
        if (params.contains("rootUri")) { ... }
        if (params.contains("rootPath")) { ... }
        if (params.contains("initializationOptions")) { ... }
        if (params.contains("capabilities")) { ... }
        if (params.contains("trace")) { ... }
        if (params.contains("workspaceFolders")) { ... }
    }

    nlohmann::ordered_json response = {};
    response["jsonrpc"] = "2.0";
    response["id"] = request["id"];
    response["result"]["protocolVersion"] = request["params"]["protocolVersion"];
    response["result"]["capabilities"]["tools"] = json::object();
    response["result"]["capabilities"]["prompts"] = json::object();
    response["result"]["capabilities"]["resources"]["subscribe"] = true;
    response["result"]["capabilities"]["logging"] = json::object();
    response["result"]["serverInfo"]["name"] = name_;
    response["result"]["serverInfo"]["version"] = PROJECT_VERSION;
    return response;
}
```

### 2. 这个函数到底是干什么的

它处理的不是工具调用，而是 MCP 初始化握手。

更直白一点说，客户端在刚连接上 server 时，会先发一个 `initialize` 请求。这个函数就是在回答两个问题：

- 客户端给了我哪些初始化信息
- 我这个 server 支持哪些能力、叫什么名字、用什么协议版本

### 3. 最值得记住的几个字段

`protocolVersion`

- 表示 MCP 协议版本
- 不是 `stdio` 或 `sse` 的版本
- 当前仓库测试里常见的例子是 `2024-11-05`

`rootUri`

- 表示工作区根 URI
- 不是普通 path
- 更适合跨平台和统一资源表示

`capabilities`

- 表示客户端能力声明
- 当前代码主要是读取并打印日志

### 4. 为什么这里 `tools`、`prompts` 返回的是 `json::object()`

因为这里不是在返回具体列表，而是在声明“我支持这类能力”。

比如：

```cpp
response["result"]["capabilities"]["tools"] = json::object();
```

它的含义不是“我这里有一堆 tool”，而是：

“我支持 tools 这一类能力，当前先用一个空对象作为能力节点。”

如果想拿到具体有哪些 tool，应该再发 `tools/list` 请求。

## 十一、看 `MCPBuilder.h`：它为什么很适合拿来做辅助理解

### 1. 关键代码

```cpp
static json Response(json request) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = request["id"];
    response["result"] = json::object();
    return response;
}

static json Error(ErrorCode code, const std::string& id, const std::string &message) {
    return {
        {"jsonrpc", "2.0"},
        {"error", {{"code", code}, {"message", message}}},
        {"id", id}
    };
}
```

### 2. 这个工具类有什么价值

它把很多重复的 JSON 拼装逻辑统一了。

比如：

- 生成标准响应骨架
- 生成标准错误响应
- 生成文本、图片、音频内容块
- 生成日志通知和进度通知

所以当后面继续学 `main.cpp` 和插件时，只要看到 `MCPBuilder::Response()`，就能很快知道它是在构造标准 MCP 响应。

## 十二、当前阶段最容易混淆的几个知识点

### 1. `protocolVersion` 不是 transport 版本

它是 MCP 协议版本，比如：

- `2024-11-05`

不是 `stdio`/`sse` 的版本号。

### 2. `URI`、`URL`、`path` 的区别

- `path`：文件系统路径，例如 `D:\xy\work\mcp`
- `URI`：统一资源标识，例如 `file:///D:/xy/work/mcp`
- `URL`：一种可定位资源的 URI，例如 `https://example.com/api`

### 3. `json::object()` 和 `json::array()` 不是一个意思

- `json::object()` -> `{}`
- `json::array()` -> `[]`

所以：

- `capabilities.tools = {}` 是能力声明
- `result.tools = []` 是准备装工具列表

### 4. `ordered_json` 是什么

`nlohmann::ordered_json` 也是 JSON 类型，但更强调保持字段插入顺序。构造协议响应时更容易输出得整齐稳定。

### 5. `std::move`、`std::function`、lambda`

这是当前 `Server.cpp` 里最常见的现代 C++ 语法组合：

- `std::function`：统一包装可调用对象
- lambda：匿名函数对象，常用于回调和线程入口
- `std::move(x)`：把 `x` 当成可移动源对象，让资源尽量转移而不是拷贝

### 6. `atomic`、`lock_guard`、`unique_lock`、`condition_variable`

这组知识点主要出现在 `WriterLoop()` 里：

- `atomic<bool>` 保存简单线程状态
- `lock_guard` 适合简单作用域加锁
- `unique_lock` 适合配合条件变量
- `condition_variable` 让线程在没消息时休眠等待

## 十三、当前阶段对 `Server.cpp` 的整体判断

我现在对 `Server.cpp` 的看法是：

前半部分是核心，必须认真看：

- 路由表
- `HandleRequest()`
- `Connect()`
- `WriterLoop()`
- `Stop()`
- `InitializeCmd()`

后半部分有不少 `xxxCmd()` 在当前项目里更像默认壳子，尤其是：

- `ToolsListCmd()`
- `ToolsCallCmd()`
- `PromptsListCmd()`
- `ResourcesListCmd()`

因为当前项目真正运行时，这些逻辑主要会在下一阶段的 `main.cpp` 里被 `OverrideCallback()` 接管。

所以不能简单说“整个 `Server.cpp` 都是壳子”，但可以说：

“真正值得精读的是前半部分骨架，后半部分很多默认命令实现只需要知道定位即可。”

## 十四、后续学习预留

### 1. `main.cpp` 预留

后面准备补这些内容：

- 程序入口整体启动流程
- transport 选择和日志初始化
- `OverrideCallback()` 为什么写在 `main.cpp`
- `tools/list`、`tools/call` 等真实业务逻辑如何接入

### 2. 插件系统预留

后面准备补这些内容：

- `PluginAPI`
- `PluginsLoader`
- 代表性插件的完整结构
- tool / prompt / resource 是怎么从插件暴露出来的

### 3. 面试表达预留

后面准备单独补一版：

- 1 分钟项目介绍
- 3 分钟架构讲解
- 高频追问和答法

