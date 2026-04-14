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

#include <iostream>
#include "StdioTransport.h"
#include "aixlog.hpp"

namespace vx::transport {

    // 修改过的更鲁棒的Read函数，处理了EOF和空行的情况
    std::pair<size_t, std::string> Stdio::Read() {
        while (true) {
            std::string json_data;
            int c;

            while ((c = std::getc(stdin)) != EOF && c != '\n') {
                json_data += static_cast<char>(c);
            }

            // 如果遇到了EOF说明可能客户端已经断开连接了
            if (c == EOF) {
                // 如果还有数据，则返回数据
                if (!json_data.empty()) {
                    return {json_data.length(), json_data};
                }
                // 如果没数据，则返回空字符串和0，表示客户端已经断开连接
                return {0, ""};
            }

            // 如果读到了空的\n，则跳过继续等待下一个数据
            // 防止因为返回空导致服务器退出
            if (json_data.empty()) {
                continue;
            }

            // 如果没遇到EOF且读到了数据，则正常返回数据
            return {json_data.length(), json_data};
        }
    }

    std::future<std::pair<size_t, std::string>> Stdio::ReadAsync() {
        return std::async(std::launch::async, [this]() {
            return Read();
        });
    }

    void Stdio::Write(const std::string& json_data) {
        std::cout << json_data << std::endl << std::flush;
    }

    std::future<void> Stdio::WriteAsync(const std::string& json_data) {
        return std::async(std::launch::async, [json_data]() {
            std::cout << json_data << std::endl << std::flush;
        });
    }

}
