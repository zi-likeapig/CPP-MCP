# MCP 速成学习计划

## 1. 目标定位

这次学习的目标不是完整吃透整个大项目，也不是重写代码，而是：

- 只聚焦当前仓库里的 `MCP` 部分
- 以 `简历可写 + 面试能讲清楚` 为目标
- 优先掌握架构、主链路、重点设计，而不是逐行细读

一句话目标：

> 能比较完整地讲清这个项目中的 `MCP client/server`、`插件机制`、`STDIO/SSE 通信`、`工具调用流程`、`RAG 工具筛选`。

---

## 2. 你现在的优势和学习策略

你之前学过 `tinywebserver`，这意味着你已经有这些基础：

- `C++` 项目阅读能力
- 服务端基本架构理解
- `TCP/网络通信` 基础
- Linux 下编译、运行、排查问题的经验

因此这次真正的新内容主要是两块：

- `MCP` 协议和工具调用模式
- 项目里的 `AI/RAG` 是怎么接入的

最适合你的学习方式不是先系统学 AI，而是：

1. 先用你熟悉的服务端/网络编程视角，搞懂这个项目的 `client/server/plugin` 主链路
2. 再把 `RAG` 当成一个“智能工具筛选模块”来理解
3. 最后按面经整理成标准化回答

---

## 3. 必看的仓库内容

### 第一层：必须吃透

这部分决定你能不能把项目讲明白。

- `README.md`
- `docs/mcp-plugin-development.md`
- `mcp_server/src/main.cpp`
- `mcp_server/src/server/Server.cpp`
- `mcp_server/src/interface/PluginAPI.h`
- `mcp_client/src/mcp_client.cpp`
- `mcp_client/src/mcp_tool_manager.cpp`

### 第二层：重点理解，但不用深挖算法细节

- `docs/rag-mcp-guide.md`
- `mcp_client/src/mcp_agent_integration.cpp`
- `mcp_client/src/rag/tool_retriever.cpp`

### 第三层：知道作用即可，用于补面试

- `mcp_server/src/transport/StdioTransport.cpp`
- `mcp_server/src/transport/SseTransport.cpp`
- `mcp_server/src/loader/PluginsLoader.cpp`

### 不建议投入太多时间的内容

- 第三方大头文件
- 所有插件实现细节
- 整个大项目里的 `A2A`
- 泛化的大模型训练原理

---

## 4. 你必须弄懂的核心知识

### 4.1 项目整体定位

要能说清：

- 这个项目是一个基于 `C++` 的 `MCP` 完整实现
- 它包含 `MCP Server`、`MCP Client`、`Agent 集成层`、`RAG 工具检索层`
- 它的核心价值是给大模型提供标准化的工具调用能力

### 4.2 MCP 协议在这个项目里干了什么

至少要知道这些 MCP 方法：

- `initialize`
- `tools/list`
- `tools/call`
- `prompts/list`
- `prompts/get`
- `resources/list`
- `resources/read`

真正最高频的是：

- `tools/list`
- `tools/call`

### 4.3 client / server / plugin 的职责

- `MCP Server`：接收请求、路由请求、调用插件、返回结果
- `MCP Client`：连接 server、发送 JSON-RPC 请求、接收响应和通知
- `Plugin`：真正提供工具、资源、提示词能力
- `MCPToolManager`：缓存工具信息、执行工具、做参数校验
- `MCPAgentIntegration`：把 MCP 和 Agent / RAG 串起来

### 4.4 一次工具调用的完整流程

必须会口述：

1. 客户端发起 JSON-RPC 请求
2. 服务端解析 `method`
3. 如果是 `tools/call`，根据 `tool name` 在已加载插件中查找
4. 调用目标插件的 `HandleRequest`
5. 插件返回 JSON 结果
6. 服务端封装为 MCP response
7. 客户端拿到响应，再返回给 agent 或上层业务

### 4.5 插件机制

要能讲明白：

- 插件通过动态库形式加载
- 支持 `.dll`、`.so`、`.dylib`
- 通过统一 `PluginAPI` 暴露能力
- server 启动时扫描插件目录并加载
- `tools/list` 由 server 汇总插件注册的工具元信息
- `tools/call` 由 server 根据名字路由到具体插件

### 4.6 通信方式

必须会区分：

- `STDIO`
  - 本地进程通信
  - client 启动 server 子进程
  - 通过标准输入输出交换 JSON-RPC 消息
- `SSE`
  - 适合远程 HTTP 场景
  - 客户端通过 `POST` 发送消息
  - 服务端通过 `Server-Sent Events` 推送响应/通知

### 4.7 项目里的 RAG 到底是什么

一定要记住：

> 这个项目里的 `RAG` 重点不是“文档问答”，而是“工具筛选”。

你需要理解：

- 用户问题会被向量化
- 工具描述和参数说明也会被向量化
- 通过向量相似度检索最相关工具
- 只把相关工具交给 LLM
- 这样可以减少 token 消耗、降低无关工具干扰、提升调用准确率

### 4.8 RAG 里的几个关键词

要能解释这些词在项目里的意义：

- `embedding`
- `vector index`
- `cosine similarity`
- `top-k`
- `similarity threshold`
- `cache`
- `fallback / degrade`

### 4.9 错误处理、超时、重试、降级

要有概念：

- 参数错误和 method 错误如何区分
- 工具不存在时如何返回错误
- 请求超时如何返回失败
- client 如何做重试
- RAG 不可用时如何退化到全量工具

---

## 5. 面经题目分层

### 第一类：必须重点准备

这些和当前仓库里的 `MCP` 强相关。

1. 项目为什么用 `JSON-RPC`，和传统 `RPC / gRPC` 有什么区别
2. MCP 工具调用时的不同错误类型如何区分
3. 超时重试机制如何设计
4. 服务端如何找到对应工具，完整调用流程是什么
5. QPS 高时如何优化性能
6. `SSE` 和 `WebSocket` 有什么区别
7. 如何通过 `RAG` 对 MCP 工具选择进行优化
8. 为什么工具调用会导致 token 消耗高
9. `RAG` 向量化的内容是什么
10. 用户问题如何向量化，效果如何比较
11. MCP client 是否嵌入到 Agent 中
12. MCP client 是基于开源实现还是自己实现
13. 你们如何实现 MCP 协议，协议包含哪些部分
14. MCP 协议在协议栈里大致属于哪一层
15. 做这个项目的目的是什么，解决了什么问题
16. MCP 插件热加载的实现原理
17. RPC 请求的超时设置、心跳/保活思路

### 第二类：可弱答

准备一个“能答但不展开”的版本即可。

1. Linux 平台 CPU 打满如何排查
2. RAG 的文本分块 `chunking` 如何设计

说明：

- `CPU 打满排查` 更偏通用服务端能力
- `chunking` 更像文档检索场景，这个仓库里主打的是工具检索，不要展开太深

### 第三类：当前阶段直接放弃

这些更偏 `A2A` 或其他项目背景，不值得现在花时间。

- `A2A` 相关问题
- 注册中心设计问题
- 锁设计追问
- `A2A` 数据格式是 `JSON` 还是 `Protobuf`
- 其他不属于当前 `MCP repo` 核心范围的问题

面试里如果被问到，可以明确说：

> 我这次重点准备的是仓库中的 `MCP` 子项目，`A2A` 部分没有作为主讲内容深入准备。

---

## 6. 速成学习计划

建议周期：

- `7 天`
- 每天 `4 小时`
- 总学习量约 `28 小时`

目标：

- 能写简历
- 能做 3 到 5 分钟项目介绍
- 能回答大多数 `MCP` 相关面经问题

### 第 1 天：建立全局认知

看：

- `README.md`
- `docs/mcp-plugin-development.md`
- `docs/rag-mcp-guide.md`

目标：

- 明白项目整体结构
- 明白 `MCP / plugin / RAG` 各自在做什么
- 写出一句话项目介绍

当天产出：

- 100 字项目简介
- 一张总架构图

### 第 2 天：服务端主线

看：

- `mcp_server/src/main.cpp`
- `mcp_server/src/server/Server.cpp`

目标：

- 明白服务端启动流程
- 明白请求怎么按 `method` 分发
- 明白 `initialize / tools/list / tools/call` 的处理方式

当天产出：

- 服务端启动流程笔记
- 一版 `tools/call` 主链路图

### 第 3 天：插件机制

看：

- `mcp_server/src/interface/PluginAPI.h`
- `mcp_server/src/loader/PluginsLoader.cpp`
- 回看 `docs/mcp-plugin-development.md`

目标：

- 明白插件注册、加载、初始化、卸载
- 明白为什么插件化提升扩展性

当天产出：

- 一段“插件架构亮点”标准回答

### 第 4 天：客户端主线

看：

- `mcp_client/src/mcp_client.cpp`
- `mcp_client/src/mcp_tool_manager.cpp`

目标：

- 明白 client 如何连接 server
- 明白如何发送请求、接收响应、处理通知
- 明白工具缓存、参数校验、工具执行逻辑

当天产出：

- 一段“client 如何实现”的回答
- 一段“工具调用错误处理”的回答

### 第 5 天：传输方式

看：

- `mcp_server/src/transport/StdioTransport.cpp`
- `mcp_server/src/transport/SseTransport.cpp`
- 回看 `mcp_client/src/mcp_client.cpp` 中对应实现

目标：

- 搞懂 `STDIO` 和 `SSE` 的区别
- 准备 `SSE vs WebSocket` 的回答

当天产出：

- 一段“本地为什么适合 STDIO”的回答
- 一段“远程为什么适合 SSE”的回答

### 第 6 天：RAG 够用即可

看：

- `docs/rag-mcp-guide.md`
- `mcp_client/src/mcp_agent_integration.cpp`
- `mcp_client/src/rag/tool_retriever.cpp`

目标：

- 明白 `RAG` 在这个项目里如何做工具检索
- 明白 `embedding / top-k / threshold / cache / fallback`
- 会解释为什么能减少 token 消耗

当天产出：

- 一段“项目里的 RAG 是什么”标准回答
- 一段“为什么做工具筛选”标准回答

### 第 7 天：面经冲刺

做的事：

- 把高频题每题口头讲一遍
- 修正自己卡顿或表述不清的地方
- 只保留主线，不追求知识面过宽

重点演练：

- 项目介绍
- 工具调用主链路
- `STDIO vs SSE`
- `JSON-RPC vs gRPC`
- RAG 工具筛选
- 插件热加载

当天产出：

- 1 版简历项目描述
- 1 版 3 分钟项目介绍稿
- 10 个高频题答案

---

## 7. 学习时建议你反复输出的 4 个核心问题

每学完一部分，都强迫自己回答下面这几个问题。

### 问题 1：这个模块是干什么的

例如：

- `Server.cpp` 是负责请求分发和默认 MCP 方法处理
- `PluginAPI.h` 是定义插件统一接口
- `ToolRetriever` 是负责基于向量检索筛选工具

### 问题 2：它和上下游怎么交互

例如：

- client 如何把请求交给 server
- server 如何把请求交给 plugin
- RAG 如何把筛选结果交给 agent

### 问题 3：为什么要这样设计

例如：

- 为什么要插件化
- 为什么需要双传输模式
- 为什么在工具很多时要用 RAG 先筛选

### 问题 4：如果面试官追问，有什么可展开点

例如：

- 扩展性
- 性能
- 降级
- 错误处理
- 场景适配

---

## 8. 作者提供的资料里，你该看哪些

根据作者给的资料列表，你现在只需要保留和当前目标强相关的内容。

### 必看

#### 1. `mcp 介绍`

`https://zhuanlan.zhihu.com/p/29001189476`
作用：

- 快速建立对 `MCP` 的概念认知
- 知道 `tools / resources / prompts` 分别是什么
- 帮你在正式看仓库前有一个协议级框架

为什么看：

- 这是最快的入门材料

#### 2. `mcp 官网 getting started / intro`

`https://modelcontextprotocol.io/docs/getting-started/intro`

作用：

- 看官方对 `MCP` 的定义
- 理解标准能力、协议思想和基本术语

为什么看：

- 面试时你的表述会更“正统”
- 不容易把 `MCP` 讲偏成普通 RPC 框架

#### 3. `https://github.com/peppemas/mcp_server`

作用：

- 当前仓库里的 server 部分与它关联很强
- 帮你理解 server 的原始设计风格和结构来源

为什么看：

- 如果你发现当前仓库里的 server 代码说明不足，可以把它当补充材料

### 选看

#### 4. `rpc` 相关文章

包括：

- `grpc` 源码链接
- 腾讯云的 `grpc/rpc` 相关文章

作用：

- 用来补“`JSON-RPC` 和 `gRPC / RPC` 有什么区别”

为什么是选看：

- 你这次不是学 `gRPC` 项目本身
- 只需要补概念对比，不需要深入源码

建议：

- 看一篇讲清 `RPC / gRPC` 基本概念的资料就够了
- 不要去啃 `grpc` 仓库源码

### 可以跳过

#### 5. `提示词` 相关资料

包括：

- `django-mcp`
- `system prompts`
- `AutoPrompt`
- 相关论文

原因：

- 这些对当前“速成讲清 MCP 项目”不是核心
- 面试里最多作为补充，不会是主线

#### 6. `grpc.zip`

原因：

- 体量太大
- 当前任务不是学习 gRPC 源码
- 只要懂 `gRPC` 和 `JSON-RPC` 的差异即可

---

## 9. 作者资料的推荐阅读顺序

建议顺序如下：

1. `mcp 介绍`
2. `mcp 官网 intro`
3. 当前仓库的 `README.md`
4. `docs/mcp-plugin-development.md`
5. `mcp_server/src/main.cpp`
6. `mcp_server/src/server/Server.cpp`
7. `PluginAPI.h`
8. `mcp_client.cpp`
9. `mcp_tool_manager.cpp`
10. `docs/rag-mcp-guide.md`
11. `mcp_agent_integration.cpp`
12. `tool_retriever.cpp`
13. 如果还需要补背景，再看 `RPC / gRPC` 的概念文章

---

## 10. 你最后要达到的效果

学完后，至少要做到下面这些：

- 能用 1 分钟介绍项目
- 能用 3 到 5 分钟讲清架构
- 能讲清 `tools/call` 的完整链路
- 能解释 `STDIO` 和 `SSE`
- 能解释插件机制
- 能解释本项目里的 `RAG`
- 能回答大多数 `MCP` 相关面经问题

如果时间再紧，可以记住最小闭环：

1. 项目做什么
2. client / server / plugin 怎么分工
3. `tools/call` 怎么走
4. `STDIO / SSE` 区别
5. `RAG` 为什么能优化工具选择

---

## 11. 面经驱动式看代码

如果你现在主要目标是应付面试，不想按文件从头硬啃，那么最合适的方式就是：

- 先按面经问题分组
- 再反推要看哪些文件
- 每个问题只抓“足够回答”的代码逻辑
- 先放弃 `A2A` 和上游大项目基础设施问题

一句话策略：

> 不是“先把代码看完再答题”，而是“先看高频题，再反推代码主链路”。

### 11.1 第一优先级：必须拿下

这些题和当前 `mcp` 子项目强相关，面试里最值得准备。

#### 1. 项目为什么用 `JSON-RPC`，它和标准 `RPC / gRPC` 有什么区别

看：

- `README.md`
- `mcp_server/src/server/Server.cpp`
- `mcp_client/src/mcp_client.cpp`

重点抓：

- 请求/响应里的 `jsonrpc`、`method`、`params`、`id`
- `HandleRequest()` 如何按 `method` 分发
- client 如何构造请求、解析响应
- 本项目为什么更贴近 `MCP + JSON-RPC + STDIO/SSE`

#### 2. 对 MCP 工具调用时的不同错误类型，你的设计有什么区别

看：

- `mcp_server/src/server/Server.cpp`
- `mcp_server/src/main.cpp`
- `mcp_client/src/mcp_tool_manager.cpp`
- `docs/mcp-plugin-development.md`

重点抓：

- `HandleRequest()` 的 method 检查与错误返回
- `tools/call` 找不到工具时的返回
- 工具参数校验逻辑
- 插件文档中的 JSON-RPC 错误码

#### 3. 怎么封装 `rpc` 超时重试机制

看：

- `mcp_client/src/mcp_agent_integration.cpp`
- `mcp_client/src/mcp_client.cpp`

重点抓：

- `callTool()` 中的重试循环
- `receiveResponse()` 的等待超时
- `retry_count`、`retry_delay_ms`

#### 4. 客户端发起一个请求时，服务端怎么找到对应的工具

看：

- `mcp_server/src/main.cpp`
- `mcp_server/src/server/Server.cpp`
- `mcp_server/src/interface/PluginAPI.h`
- `mcp_server/src/loader/PluginsLoader.cpp`

重点抓：

- `functionMap`
- `OverrideCallback("tools/call", ...)`
- 遍历插件、遍历 tool 列表、按名称匹配
- `plugin.instance->HandleRequest(...)`

这是最高频主线题，必须讲顺。

#### 5. 假设请求 QPS 很高，高并发场景下怎么优化性能

看：

- `mcp_server/src/server/Server.cpp`
- `mcp_client/src/rag/tool_retriever.cpp`
- `docs/rag-mcp-guide.md`

重点抓：

- 读写循环与通知线程分离
- RAG 的 `cache`
- `top-k` 和 `threshold`
- 索引持久化与减少重复向量化

#### 6. 讲一下 `SSE` 通信模式，它和 `WebSocket` 有什么区别

看：

- `mcp_server/src/transport/SseTransport.cpp`
- `mcp_client/src/mcp_client.cpp`
- `README.md`

重点抓：

- 服务端 `GET /sse`
- 客户端 `POST /message(s)`
- 长连接事件流
- keep-alive / ping

#### 7. 怎么通过 `RAG` 对 MCP 工具选择进行优化

看：

- `docs/rag-mcp-guide.md`
- `mcp_client/src/mcp_agent_integration.cpp`
- `mcp_client/src/rag/tool_retriever.cpp`

重点抓：

- `indexTools()`
- `retrieve()`
- `buildToolText()`
- `getRelevantTools()`

#### 8. 工具调用时 token 消耗过高，是什么时候呢

看：

- `docs/rag-mcp-guide.md`
- `README.md`
- `mcp_client/src/mcp_agent_integration.cpp`

重点抓：

- 不做筛选时，全量工具会发给 LLM
- 做了 RAG 后只返回相关工具

#### 9. `RAG` 向量化的内容是什么

看：

- `mcp_client/src/rag/tool_retriever.cpp`
- `docs/rag-mcp-guide.md`

重点抓：

- `buildToolText()`
- 工具名
- 描述
- 参数 schema 中的字段说明

#### 10. 怎么把用户问题、prompt 去向量化，有对比过效果吗

看：

- `mcp_client/src/rag/tool_retriever.cpp`
- `mcp_client/src/rag/embedding_service.cpp`
- `docs/rag-mcp-guide.md`

重点抓：

- query 直接做 embedding
- 工具文本也做 embedding
- 相似度搜索

#### 11. 你开发的 MCP client 是嵌入到 Agent 里面的吗

看：

- `mcp_client/src/mcp_agent_integration.cpp`
- `README.md`

重点抓：

- `MCPAgentIntegration`
- `initialize()`
- `connectToMCPServer()`
- `getRelevantToolsAsJson()`

#### 12. MCP client 是直接复用开源实现，还是基于 MCP 协议自己开发的

看：

- `README.md`
- `mcp_client/src/mcp_client.cpp`

重点抓：

- 当前 repo 自己实现了 `MCPClient`
- server 部分和 `peppemas/mcp_server` 有来源关系
- client 与 RAG 集成层是当前仓库实现重点

#### 13. 你们如何实现 MCP 协议，协议包含哪些部分

看：

- `README.md`
- `mcp_server/src/server/Server.cpp`
- `mcp_server/src/main.cpp`
- `mcp_server/src/interface/PluginAPI.h`

重点抓：

- `initialize`
- `tools/list`
- `tools/call`
- `prompts/list/get`
- `resources/list/read`

#### 14. `TCP/IP` 协议栈里，MCP 对应哪一层

看：

- `README.md`
- `mcp_server/src/transport/StdioTransport.cpp`
- `mcp_server/src/transport/SseTransport.cpp`

重点抓：

- `STDIO` 不走网络
- `SSE/HTTP Stream` 建立在 `HTTP/TCP` 之上
- `MCP` 本身属于应用层协议

#### 15. 你做这个 MCP 项目的目的是什么，可以解决什么问题

看：

- `README.md`
- `docs/rag-mcp-guide.md`

重点抓：

- 标准化工具调用
- 统一 tools/resources/prompts 能力
- 本地和远程两种接入方式
- 工具很多时通过 RAG 做智能筛选

#### 16. 问 MCP 插件热加载的实现原理，并询问是否对比过其他热加载方案

看：

- `mcp_server/src/loader/PluginsLoader.cpp`
- `mcp_server/src/interface/PluginAPI.h`
- `docs/mcp-plugin-development.md`

重点抓：

- 扫描插件目录
- `LoadLibraryA` / `dlopen`
- `CreatePlugin()` / `DestroyPlugin()`
- `Initialize()` / `Shutdown()`

#### 17. 针对 RPC 请求或系统交互，超时时间与保活怎么设计

看：

- `mcp_client/src/mcp_client.cpp`
- `mcp_server/src/transport/SseTransport.cpp`
- `docs/rag-mcp-guide.md`

重点抓：

- request timeout
- SSE keep-alive ping
- validation timeout

### 11.2 第二优先级：可弱答

这些题可以准备一个“懂方向但不深答”的版本。

#### 18. 假设 MCP 服务端在 Linux 平台运行时 CPU 突然占满，如何排查

看：

- `mcp_server/src/server/Server.cpp`
- `mcp_server/src/transport/SseTransport.cpp`

重点抓：

- 循环读写
- 线程
- wait / sleep
- 是否有忙等

#### 19. 提问 RAG 在实际业务中的文本分块 `Chunking` 策略

看：

- `docs/rag-mcp-guide.md`
- `mcp_client/src/rag/tool_retriever.cpp`

这题在当前 repo 里不用深讲，因为这里主要不是文档分块检索，而是工具描述检索。可以答：

- 当前实现重点不是 chunking 文档库
- 而是把工具名、描述、参数说明组织成向量化文本

### 11.3 第三优先级：当前阶段直接放弃

这些题更偏上游大项目，不适合你现在投入时间。

- `A2A` 协议
- 能力发现机制
- 注册中心数据结构
- 锁设计追问
- `A2A` 数据格式是 `Protobuf` 还是 `JSON`
- 分布式云存储、`FastDFS`

如果被问到，可以直接收边界：

> 我这次重点准备的是拆分后的 `MCP` 子项目，主要覆盖 `MCP client/server`、插件机制、`STDIO/SSE` 通信和 `RAG` 工具筛选；`A2A` 和上游分布式基础设施部分不是这次主讲内容。

### 11.4 最短刷题顺序

如果你时间很紧，按这个顺序刷：

1. 先刷 `4`：服务端怎么找到工具，完整调用流程
2. 再刷 `13`：你们如何实现 MCP 协议
3. 再刷 `6`：`SSE` 和 `WebSocket` 区别
4. 再刷 `7`：RAG 如何优化工具选择
5. 再刷 `1`：`JSON-RPC` 与 `gRPC/RPC` 区别
6. 再刷 `11/12`：MCP client 与 Agent 的关系、是否自研
7. 再刷 `16`：插件加载机制
8. 最后补 `2/3/8/9/10/14/15/17`

### 11.5 题目反推代码的使用方法

以后看代码时，不要问“这个文件我要不要全读完”，而是问：

- 这个文件能帮我回答哪几个面试题？
- 这个函数在主链路里是什么角色？
- 我需要知道它的职责，还是需要知道它的实现细节？

推荐执行方式：

1. 先选一个问题
2. 只看这个问题对应的 2 到 4 个文件
3. 写出 3 句自己的回答
4. 再切到下一个问题

---

## 12. 下一步建议

建议你接下来按这个顺序推进：

1. 先看作者资料里的 `mcp 介绍` 和 `mcp 官网 intro`
2. 再按本文档的 7 天计划看仓库代码
3. 然后切到本节，用“面经题 -> 对应文件”的方式刷主线问题
4. 学完后，把高频题答案整理成自己的表达

如果需要，下一步可以继续补两份内容：

- `MCP 高频面试题标准答案`
- `适合简历的项目描述模板`
