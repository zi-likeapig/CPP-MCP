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

#ifndef MCP_SERVER_SESSION_BUILDER_H
#define MCP_SERVER_SESSION_BUILDER_H

#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace vx::utils {

    struct SessionBuilder {
        // 生成唯一会话ID，用于标识一个SSE会话
        static std::string GenerateUniqueSessionID() {
            auto now = std::chrono::high_resolution_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<uint32_t> dis;

            std::stringstream ss;
            ss << std::hex << timestamp << "-" << dis(gen);
            // 使用字符串流将时间戳和随机数拼接成一个字符串，并返回
            // 类似于17f9ab23c1d4-8a72f1b3

            return ss.str();
        }
    };

}



#endif //MCP_SERVER_SESSION_BUILDER_H