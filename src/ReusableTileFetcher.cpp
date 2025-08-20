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

#include "ReusableTileFetcher.hpp"

ReusableTileFetcher::ReusableTileFetcher() { renderMode = RenderMode::ACCURATE; }
ReusableTileFetcher::~ReusableTileFetcher() { disconnect(); }

void ReusableTileFetcher::sendHttpRequest(const String &host, const String &path)
{
    client.print(String("GET ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print("User-Agent: OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)\r\n");
    client.print("Connection: keep-alive\r\n");
    client.print("\r\n");
}

void ReusableTileFetcher::disconnect()
{
    client.stop();
    currentHost = "";
    currentPort = 80;
}

MemoryBuffer ReusableTileFetcher::fetchToBuffer(const String &url, String &result, RenderMode mode)
{
    renderMode = mode;
    String host, path;
    uint16_t port;
    if (!parseUrl(url, host, path, port))
    {
        result = "Invalid URL";
        return MemoryBuffer::empty();
    }

    if (!ensureConnection(host, port, result))
        return MemoryBuffer::empty();

    sendHttpRequest(host, path);
    size_t contentLength = 0;
    if (!readHttpHeaders(contentLength, result))
        return MemoryBuffer::empty();

    auto buffer = MemoryBuffer(contentLength);
    if (!buffer.isAllocated())
    {
        result = "Buffer allocation failed";
        return MemoryBuffer::empty();
    }

    if (!readBody(buffer, contentLength, result))
        return MemoryBuffer::empty();

    return buffer;
}

bool ReusableTileFetcher::parseUrl(const String &url, String &host, String &path, uint16_t &port)
{
    port = 80;
    if (url.startsWith("https://"))
        return false;

    if (!url.startsWith("http://"))
        return false;

    int idxHostStart = 7; // length of "http://"
    int idxPath = url.indexOf('/', idxHostStart);
    if (idxPath == -1)
        return false;

    host = url.substring(idxHostStart, idxPath);
    path = url.substring(idxPath);
    return true;
}

bool ReusableTileFetcher::ensureConnection(const String &host, uint16_t port, String &result)
{
    if (!client.connected() || host != currentHost || port != currentPort)
    {
        disconnect();
        client.setConnectionTimeout(renderMode == RenderMode::FAST ? 100 : 5000);
        if (!client.connect(host.c_str(), port, renderMode == RenderMode::FAST ? 100 : 5000))
        {
            result = "Connection failed to " + host;
            return false;
        }
        currentHost = host;
        currentPort = port;
        log_i("(Re)connected on core %i", xPortGetCoreID());
    }
    return true;
}

bool ReusableTileFetcher::readHttpHeaders(size_t &contentLength, String &result)
{
    String line;
    line.reserve(OSM_MAX_HEADERLENGTH);
    contentLength = 0;
    bool start = true;
    while (client.connected())
    {
        if (!readLineWithTimeout(line, renderMode == RenderMode::FAST ? 300 : 5000))
        {
            result = "Header timeout";
            disconnect();
            return false;
        }

        line.trim();
        if (start)
        {
            if (!line.startsWith("HTTP/1.1"))
            {
                result = "Bad HTTP response: " + line;
                disconnect();
                return false;
            }
            start = false;
        }

        if (line.length() == 0)
            break; // End of headers

        if (line.startsWith("Content-Length:"))
        {
            String val = line.substring(15);
            val.trim();
            contentLength = val.toInt();
        }
    }

    if (contentLength == 0)
    {
        result = "Missing or invalid Content-Length";
        disconnect();
        return false;
    }

    return true;
}

bool ReusableTileFetcher::readBody(MemoryBuffer &buffer, size_t contentLength, String &result)
{
    uint8_t *dest = buffer.get();
    size_t readSize = 0;
    unsigned long lastReadTime = millis();
    const unsigned long timeoutMs = (renderMode == RenderMode::FAST) ? 300 : 5000;

    while (readSize < contentLength)
    {
        size_t availableData = client.available();
        if (availableData == 0)
        {
            if (millis() - lastReadTime >= timeoutMs)
            {
                result = "Timeout: " + String(timeoutMs) + " ms";
                disconnect();
                return false;
            }
            taskYIELD();
            continue;
        }

        size_t remaining = contentLength - readSize;
        size_t toRead = std::min(availableData, remaining);

        int bytesRead = client.readBytes(dest + readSize, toRead);
        if (bytesRead > 0)
        {
            readSize += bytesRead;
            lastReadTime = millis();
        }
        else
            taskYIELD();
    }
    return true;
}

bool ReusableTileFetcher::readLineWithTimeout(String &line, uint32_t timeoutMs)
{
    line = "";
    const uint32_t start = millis();

    while ((millis() - start) < timeoutMs)
    {
        if (client.available())
        {
            // cppcheck-suppress uncheckedRead
            const char c = client.read();

            if (c == '\r')
                continue;

            if (c == '\n')
                return true;

            if (line.length() >= OSM_MAX_HEADERLENGTH - 1)
                return false;

            line += c;
        }
        else
            taskYIELD();
    }
    return false; // Timed out
}
