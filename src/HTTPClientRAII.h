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
   
#ifndef HTTPCLIENTRAII_H
#define HTTPCLIENTRAII_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

class HTTPClientRAII
{
public:
    HTTPClientRAII();
    // This class manages an HTTPClient and should not be copied.
    HTTPClientRAII(const HTTPClientRAII &) = delete;
    HTTPClientRAII &operator=(const HTTPClientRAII &) = delete;
    HTTPClientRAII(HTTPClientRAII &&) = delete;
    HTTPClientRAII &operator=(HTTPClientRAII &&) = delete;

    ~HTTPClientRAII();

    bool begin(const String &url);
    int GET();
    size_t getSize() const;
    WiFiClient *getStreamPtr();
    bool isInitialized() const;

private:
    HTTPClient *http;
    const char *userAgent = "OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)";
};

#endif // HTTPCLIENTRAII_H
