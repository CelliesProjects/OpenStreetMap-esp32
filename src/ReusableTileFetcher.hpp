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

#pragma once

#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <memory>
#include "MemoryBuffer.hpp"

constexpr int OSM_MAX_HEADERLENGTH = 256;
constexpr int OSM_DEFAULT_TIMEOUT_MS = 5000;

class ReusableTileFetcher
{
public:
    ReusableTileFetcher();
    ~ReusableTileFetcher();

    ReusableTileFetcher(const ReusableTileFetcher &) = delete;
    ReusableTileFetcher &operator=(const ReusableTileFetcher &) = delete;

    MemoryBuffer fetchToBuffer(const String &url, String &result, unsigned long timeoutMS);
    void disconnect();

private:
    WiFiClient client;
    WiFiClientSecure secureClient;
    bool currentIsTLS = false;       // true if secureClient is the active connection
    String currentHost;
    uint16_t currentPort = 80;

    bool parseUrl(const String &url, String &host, String &path, uint16_t &port, bool &useTLS);
    bool ensureConnection(const String &host, uint16_t port, bool useTLS, unsigned long timeoutMS, String &result);
    void sendHttpRequest(const String &host, const String &path);
    bool readHttpHeaders(size_t &contentLength, unsigned long timeoutMS, String &result, int &statusCode, String &outLocation, bool &outConnectionClose);
    bool readBody(MemoryBuffer &buffer, size_t contentLength, unsigned long timeoutMS, String &result);
    bool readLineWithTimeout(String &line, uint32_t timeoutMs);
};
