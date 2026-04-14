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

#include "PluginsLoader.h"

namespace vx::mcp {

    PluginsLoader::PluginsLoader() = default;

    PluginsLoader::~PluginsLoader() {
        UnloadPlugins();
    }

    // 扫描指定目录，寻找所有插件文件，并加载它们
    bool PluginsLoader::LoadPlugins(const std::string& directory) {
        try {
            // filesystem库提供了一个递归遍历目录的迭代器，可以遍历目录下的所有文件和子目录
            // 这里用recursive_directory_iterator遍历目录下的所有文件和子目录，包括子目录下的子目录
            // 这样就可以找到所有插件文件，并加载
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
                if (entry.is_regular_file()) {  // 如果是普通文件，则继续处理，看是不是动态库文件
                    std::string extension = entry.path().extension().string(); // 获取文件扩展名.dll/.so/.dylib

                    // 检查文件是否是共享库，如果是，则加载
#ifdef _WIN32
                    if (extension == ".dll")
#else
    #ifdef __APPLE__
                    if (extension == ".dylib" || extension == ".so")
    #else
                    if (extension == ".so") // 通常对应linux或unix系统下的共享库文件
    #endif
#endif
                    {
                        LoadPlugin(entry.path().string()); // 如果是符合拓展名的动态库文件，则加载插件
                    }
                }
            }
            return true;
        } catch (const std::exception& ex) {
            LOG(ERROR) << "Error loading plugins: " << ex.what() << std::endl;
            return false;
        }
    }

    // 加载单个插件
    bool PluginsLoader::LoadPlugin(const std::string& path) {  
        PluginEntry entry;
        entry.path = path;

        // Load the shared library
#ifdef _WIN32
        entry.handle = LoadLibraryA(path.c_str());  // 加载一个dll文件，返回句柄
        if (!entry.handle) {
            DWORD error = GetLastError();  // 获取错误码
            char errorMsg[256] = {0};
            FormatMessageA(
                    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr,
                    error,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    errorMsg,
                    sizeof(errorMsg),
                    nullptr
            );  // 将错误码转换为错误信息
            LOG(ERROR) << "Failed to load plugin: " << path
                       << " - Error " << error << ": " << errorMsg << std::endl;
            return false;
        }
        // Get function pointers
        entry.createFunc = (PluginAPI * (*)())GetProcAddress(entry.handle, "CreatePlugin");
        entry.destroyFunc = (void (*)(PluginAPI *))GetProcAddress(entry.handle, "DestroyPlugin");
#else
        entry.handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!entry.handle) {
            LOG(ERROR) << "Failed to load plugin: " << path << " - " << dlerror() << std::endl;
            return false;
        }

        // Get function pointers
        entry.createFunc = (PluginAPI * (*)())dlsym(entry.handle, "CreatePlugin");
        entry.destroyFunc = (void (*)(PluginAPI *))dlsym(entry.handle, "DestroyPlugin");
#endif

        // Check if required functions were found
        if (!entry.createFunc || !entry.destroyFunc) {
            LOG(ERROR) << "Plugin does not export required functions: " << path << std::endl;

#ifdef _WIN32
            FreeLibrary(entry.handle);
#else
            dlclose(entry.handle);
#endif

            return false;
        }

        // Create plugin instance
        entry.instance = entry.createFunc();

        // Initialize the plugin
        if (!entry.instance->Initialize()) {
            LOG(ERROR) << "Plugin initialization failed: " << path << std::endl;
            entry.destroyFunc(entry.instance);

#ifdef _WIN32
            FreeLibrary(entry.handle);
#else
            dlclose(entry.handle);
#endif

            return false;
        }

        // Add to a plugin list
        m_plugins.push_back(entry);
        LOG(INFO) << "Loaded plugin: " << entry.instance->GetName()
                  << " v" << entry.instance->GetVersion() << std::endl;

        return true;
    }

    void PluginsLoader::UnloadPlugins() {
        for (auto& entry : m_plugins) {
            UnloadPlugin(entry);
        }
        m_plugins.clear();
    }

    void PluginsLoader::UnloadPlugin(PluginEntry& entry) {
        if (entry.instance) {
            // Shutdown the plugin
            entry.instance->Shutdown();

            // Destroy the plugin instance
            entry.destroyFunc(entry.instance);
            entry.instance = nullptr;
        }

        // Unload the library
        if (entry.handle) {
#ifdef _WIN32
            FreeLibrary(entry.handle);
#else
            dlclose(entry.handle);
#endif
            entry.handle = nullptr;
        }
    }

    const std::vector<PluginEntry>& PluginsLoader::GetPlugins() const {
        return m_plugins;
    }

}