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

// 因为这个是server和plugin的边界接口
// 所以用extern "C"包裹，保证编译器按照C语言的规则处理，避免C++的命名修饰
// 这样做主要是为了兼容性，因为有些编译器对C++的命名修饰支持不好，可能会导致符号冲突

#ifndef MCP_SERVER_PLUGINAPI_H
#define MCP_SERVER_PLUGINAPI_H

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ClientNotificationCallback是一个函数指针
// 在这里指向回调函数，当plugin发送notification给server时，调用这个回调函数，并传入pluginName和notification
// 会在main里设置为ClientNotificationCallbackImpl函数
// 用typedef提前声明一下这个函数指针类型，后面好用
typedef void (*ClientNotificationCallback)(const char* pluginName, const char* notification);

typedef enum {
    PLUGIN_TYPE_TOOLS = 0,
    PLUGIN_TYPE_PROMPTS = 1,
    PLUGIN_TYPE_RESOURCES = 2
} PluginType;

typedef struct {
    const char* name;
    const char* description;
    const char* inputSchema;  // JSON schema as a string
} PluginTool;

typedef struct {
    const char* name;
    const char* description;
    const char* arguments;  // JSON arguments as a string
} PluginPrompt;

typedef struct {
    const char* name;
    const char* description;
    const char* uri;
    const char* mime;   // 文件类型，如text/plain, image/png, audio/mp3等
} PluginResource;

// 单独把通知系统抽象出来，可以不这样做，但方便之后扩展其他功能
typedef struct {
    ClientNotificationCallback SendToClient;    // you should not touch this
} NotificationSystem;

// 插件暴露给server的一张“功能清单表”，是主程序和插件之间的最小公共接口
// 只要有这些方法，server就能扫描到plugin并加载调用
typedef struct {
    const char* (*GetName)();
    const char* (*GetVersion)();
    PluginType (*GetType)();

    int (*Initialize)();
    void (*Shutdown)();

    // HandleRequest并不区分工具、prompt、resource，都统一调用这个方法，然后内部处理
    char* (*HandleRequest)(const char* request);

    int (*GetToolCount)();
    const PluginTool* (*GetTool)(int index);
    int (*GetPromptCount)();
    const PluginPrompt* (*GetPrompt)(int index);
    int (*GetResourceCount)();
    const PluginResource* (*GetResource)(int index);

    // 主程序反向注入通知能力，让插件可以发送notification给server
    NotificationSystem* notifications;
} PluginAPI;

// 这两个函数是暴露给server的
// server会在动态库里通过名字查找这两个函数，然后调用她们来创建和销毁插件
PLUGIN_API PluginAPI* CreatePlugin();
PLUGIN_API void DestroyPlugin(PluginAPI*);

// 如果用C++，就加这个，告诉编译器这是C++代码
#ifdef __cplusplus
}
#endif

#endif //MCP_SERVER_PLUGINAPI_H
