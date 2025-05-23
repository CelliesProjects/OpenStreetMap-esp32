/*
    Copyright (c) 2025 Cellie https://github.com/CelliesProjects/OpenStreetMap-esp32

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
    SPDX-License-Identifier: MIT
    */
#ifndef HTTPCLIENTRAII_HPP_
#define HTTPCLIENTRAII_HPP_

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

class HTTPClientRAII
{
public:
    HTTPClientRAII(const HTTPClientRAII &) = delete;
    HTTPClientRAII &operator=(const HTTPClientRAII &) = delete;
    HTTPClientRAII(HTTPClientRAII &&) = delete;
    HTTPClientRAII &operator=(HTTPClientRAII &&) = delete;

    HTTPClientRAII() noexcept : http(new HTTPClient()) {}

    ~HTTPClientRAII() noexcept
    {
        if (http)
            http->end();
    }

    bool begin(const String &url)
    {
        if (!http)
            return false;
        http->setUserAgent("OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)");
        return http->begin(url);
    }

    int GET() { return http ? http->GET() : -1; }
    size_t getSize() const { return http ? http->getSize() : 0; }
    WiFiClient *getStreamPtr() { return http ? http->getStreamPtr() : nullptr; }
    bool isInitialized() const { return static_cast<bool>(http); }

private:
    std::unique_ptr<HTTPClient> http;
};

#endif
