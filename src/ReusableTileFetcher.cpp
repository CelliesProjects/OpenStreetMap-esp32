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

ReusableTileFetcher::ReusableTileFetcher() {}
ReusableTileFetcher::~ReusableTileFetcher() { disconnect(); }

void ReusableTileFetcher::sendHttpRequest(const String &host, const String &path)
{
    Stream *s = currentIsTLS ? static_cast<Stream *>(&secureClient) : static_cast<Stream *>(&client);

    s->print(String("GET ") + path + " HTTP/1.1\r\n");
    s->print(String("Host: ") + host + "\r\n");
    s->print("User-Agent: OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)\r\n");
    s->print("Connection: keep-alive\r\n");
    s->print("\r\n");
}

void ReusableTileFetcher::disconnect()
{
    if (currentIsTLS)
        secureClient.stop();
    else
        client.stop();
    currentHost = "";
    currentPort = 0;
    currentIsTLS = false;
}

MemoryBuffer ReusableTileFetcher::fetchToBuffer(const String &url, String &result, unsigned long timeoutMS)
{
    String host, path;
    uint16_t port;
    bool useTLS;

    log_d("url: %s", url.c_str());

    if (!parseUrl(url, host, path, port, useTLS))
    {
        result = "Invalid URL";
        return MemoryBuffer::empty();
    }

    if (!ensureConnection(host, port, useTLS, timeoutMS, result))
        return MemoryBuffer::empty();

    sendHttpRequest(host, path);

    size_t contentLength = 0;
    String location;
    bool connClose = false;

    if (!readHttpHeaders(contentLength, timeoutMS, result, connClose))
    {
        disconnect();
        return MemoryBuffer::empty();
    }

    if (contentLength == 0)
    {
        result = "Empty response (Content-Length=0)";
        disconnect();
        return MemoryBuffer::empty();
    }

    auto buffer = MemoryBuffer(contentLength);
    if (!buffer.isAllocated())
    {
        result = "Buffer allocation failed";
        disconnect();
        return MemoryBuffer::empty();
    }

    if (!readBody(buffer, contentLength, timeoutMS, result))
    {
        disconnect();
        return MemoryBuffer::empty();
    }

    // Server requested connection close → drop it
    if (connClose)
        disconnect();

    return buffer;
}

bool ReusableTileFetcher::parseUrl(const String &url, String &host, String &path, uint16_t &port, bool &useTLS)
{
    useTLS = false;
    port = 80;

    if (url.startsWith("https://"))
    {
        useTLS = true;
        port = 443;
    }
    else if (url.startsWith("http://"))
    {
        useTLS = false;
        port = 80;
    }
    else
    {
        return false;
    }

    int idxHostStart = useTLS ? 8 : 7; // length of "https://" : "http://"
    int idxPath = url.indexOf('/', idxHostStart);
    if (idxPath == -1)
    {
        // allow bare host (no path) by setting path to "/"
        host = url.substring(idxHostStart);
        path = "/";
        return true;
    }

    host = url.substring(idxHostStart, idxPath);
    path = url.substring(idxPath);
    return true;
}

bool ReusableTileFetcher::ensureConnection(const String &host, uint16_t port, bool useTLS, unsigned long timeoutMS, String &result)
{
    // If we already have a connection to exact host/port/scheme and it's connected, keep it.
    if ((useTLS == currentIsTLS) && (host == currentHost) && (port == currentPort) &&
        ((useTLS && secureClient.connected()) || (!useTLS && client.connected())))
    {
        return true;
    }

    // Not connected or different target: close previous
    if (currentIsTLS)
        secureClient.stop();
    else
        client.stop();

    currentHost = "";
    currentPort = 0;
    currentIsTLS = false;

    uint32_t connectTimeout = timeoutMS > 0 ? timeoutMS : OSM_DEFAULT_TIMEOUT_MS;

    if (useTLS)
    {
        secureClient.setInsecure();
        if (!secureClient.connect(host.c_str(), port, connectTimeout))
        {
            result = "TLS connect failed to " + host;
            return false;
        }
        currentIsTLS = true;
    }
    else
    {
        if (!client.connect(host.c_str(), port, connectTimeout))
        {
            result = "TCP connect failed to " + host;
            return false;
        }
        currentIsTLS = false;
    }
    currentHost = host;
    currentPort = port;
    log_i("(Re)connected on core %i to %s:%u (TLS=%d) (timeout=%lu ms)", xPortGetCoreID(), host.c_str(), port, useTLS ? 1 : 0, connectTimeout);
    return true;
}

bool ReusableTileFetcher::readHttpHeaders(size_t &contentLength, unsigned long timeoutMS, String &result, bool &connectionClose)
{
    String line;
    line.reserve(OSM_MAX_HEADERLENGTH);
    contentLength = 0;
    bool start = true;
    connectionClose = false;
    bool pngFound = false;

    uint32_t headerTimeout = timeoutMS > 0 ? timeoutMS : OSM_DEFAULT_TIMEOUT_MS;

    while ((currentIsTLS ? secureClient.connected() : client.connected()))
    {
        if (!readLineWithTimeout(line, headerTimeout))
        {
            result = "Header timeout";
            return false;
        }

        line.trim();
        log_d("read header: %s", line.c_str());

        if (start)
        {
            if (!line.startsWith("HTTP/1."))
            {
                result = "Bad HTTP response: " + line;
                return false;
            }
            // Extract status code
            int statusCode;
            int sp1 = line.indexOf(' ');
            if (sp1 >= 0)
            {
                int sp2 = line.indexOf(' ', sp1 + 1);
                String codeStr = (sp2 > sp1) ? line.substring(sp1 + 1, sp2)
                                             : line.substring(sp1 + 1);
                statusCode = codeStr.toInt();
            }
            if (statusCode != 200)
            {
                result = "HTTP error " + String(statusCode);
                return false;
            }
            start = false;
        }

        if (line.length() == 0)
            break; // End of headers

        line.toLowerCase();

        static const char *CONTENT_LENGTH = "content-length:";
        static const char *CONTENT_TYPE = "content-type:";
        static const char *CONNECTION = "connection:";

        if (line.startsWith(CONTENT_LENGTH))
        {
            String val = line.substring(strlen(CONTENT_LENGTH));
            val.trim();
            contentLength = val.toInt();
        }
        else if (line.startsWith(CONNECTION))
        {
            String val = line.substring(strlen(CONNECTION));
            val.trim();
            if (val.equals("close"))
                connectionClose = true;
        }
        else if (line.startsWith(CONTENT_TYPE))
        {
            String val = line.substring(strlen(CONTENT_TYPE));
            val.trim();
            if (val.equals("image/png"))
                pngFound = true;
        }
    }

    if (!pngFound)
    {
        result = "Content-Type not PNG";
        return false;
    }

    return true;
}

bool ReusableTileFetcher::readBody(MemoryBuffer &buffer, size_t contentLength, unsigned long timeoutMS, String &result)
{
    uint8_t *dest = buffer.get();
    size_t readSize = 0;
    unsigned long lastReadTime = millis();

    const unsigned long maxStall = timeoutMS > 0 ? timeoutMS : OSM_DEFAULT_TIMEOUT_MS;

    if (currentIsTLS)
        secureClient.setTimeout(maxStall);
    else
        client.setTimeout(maxStall);

    while (readSize < contentLength)
    {
        size_t availableData = currentIsTLS ? secureClient.available() : client.available();
        if (availableData == 0)
        {
            if (millis() - lastReadTime >= maxStall)
            {
                result = "Timeout: body read stalled for " + String(maxStall) + " ms";
                disconnect();
                return false;
            }
            taskYIELD();
            continue;
        }

        size_t remaining = contentLength - readSize;
        size_t toRead = std::min(availableData, remaining);

        int bytesRead = currentIsTLS
                            ? secureClient.readBytes(dest + readSize, toRead)
                            : client.readBytes(dest + readSize, toRead);

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
        int availableData = (currentIsTLS ? secureClient.available() : client.available());
        if (availableData)
        {
            const char c = (currentIsTLS ? secureClient.read() : client.read());
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
