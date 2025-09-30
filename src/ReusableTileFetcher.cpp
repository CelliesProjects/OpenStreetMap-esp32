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

void ReusableTileFetcher::sendHttpRequest(const char *host, const char *path)
{
    Stream *s = currentIsTLS ? static_cast<Stream *>(&secureClient) : static_cast<Stream *>(&client);

    char buf[256];
    snprintf(buf, sizeof(buf), "GET %s HTTP/1.1\r\nHost: %s\r\n", path, host);
    s->print(buf);
    s->print("User-Agent: OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)\r\nConnection: keep-alive\r\n\r\n");
}

void ReusableTileFetcher::disconnect()
{
    if (currentIsTLS)
        secureClient.stop();
    else
        client.stop();
    currentHost[0] = 0;
    currentPort = 0;
    currentIsTLS = false;
}

MemoryBuffer ReusableTileFetcher::fetchToBuffer(const char *url, String &result, unsigned long timeoutMS)
{
    char host[OSM_MAX_HOST_LEN];
    char path[OSM_MAX_PATH_LEN];
    uint16_t port;
    bool useTLS;

    log_d("url: %s", url);

    [[maybe_unused]] const unsigned long startMS = millis();

    if (!parseUrl(url, host, path, port, useTLS))
    {
        result = "Invalid URL";
        return MemoryBuffer::empty();
    }

    if (!ensureConnection(host, port, useTLS, timeoutMS, result))
        return MemoryBuffer::empty();

    sendHttpRequest(host, path);

    size_t contentLength = 0;
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
        result = "Download buffer allocation failed";
        disconnect();
        return MemoryBuffer::empty();
    }

    if (!readBody(buffer, contentLength, timeoutMS, result))
    {
        disconnect();
        return MemoryBuffer::empty();
    }

    log_d("fetching %s took %lu ms", url, millis() - startMS);

    // Server requested connection close → drop it
    if (connClose)
        disconnect();

    return buffer;
}

bool ReusableTileFetcher::parseUrl(const char *url, char *host, char *path, uint16_t &port, bool &useTLS)
{
    if (!url)
        return false;

    if (strncmp(url, "https://", 8) == 0)
    {
        useTLS = true;
        port = 443;
    }
    else if (strncmp(url, "http://", 7) == 0)
    {
        useTLS = false;
        port = 80;
    }
    else
        return false;

    int idxHostStart = useTLS ? 8 : 7; // skip scheme
    const char *pathPtr = strchr(url + idxHostStart, '/');
    if (!pathPtr)
        return false; // no '/' → invalid

    int hostLen = pathPtr - (url + idxHostStart);
    if (hostLen <= 0 || hostLen >= OSM_MAX_HOST_LEN)
        return false; // too long for buffer

    snprintf(host, OSM_MAX_HOST_LEN, "%.*s", hostLen, url + idxHostStart);

    int pathLen = strnlen(pathPtr, OSM_MAX_PATH_LEN);
    if (pathLen == 0 || pathLen >= OSM_MAX_PATH_LEN)
        return false; // too long for buffer

    snprintf(path, OSM_MAX_PATH_LEN, "%s", pathPtr);

    return true;
}

bool ReusableTileFetcher::ensureConnection(const char *host, uint16_t port, bool useTLS, unsigned long timeoutMS, String &result)
{
    // If we already have a connection to exact host/port/scheme and it's connected, keep it.
    if ((useTLS == currentIsTLS) && !strcmp(host, currentHost) && (port == currentPort) &&
        ((useTLS && secureClient.connected()) || (!useTLS && client.connected())))
    {
        return true;
    }

    disconnect();

    uint32_t connectTimeout = timeoutMS > 0 ? timeoutMS : OSM_DEFAULT_TIMEOUT_MS;

    if (useTLS)
    {
        secureClient.setInsecure();
        if (!secureClient.connect(host, port, connectTimeout))
        {
            result = "TLS connect failed to ";
            result += host;
            return false;
        }
        currentIsTLS = true;
    }
    else
    {
        if (!client.connect(host, port, connectTimeout))
        {
            result = "TCP connect failed to ";
            result += host;
            return false;
        }
        currentIsTLS = false;
    }
    snprintf(currentHost, sizeof(currentHost), "%s", host);
    currentPort = port;
    log_i("(Re)connected on core %i to %s:%u (TLS=%d) (timeout=%lu ms)", xPortGetCoreID(), host, port, useTLS ? 1 : 0, connectTimeout);
    return true;
}

bool ReusableTileFetcher::readHttpHeaders(size_t &contentLength, unsigned long timeoutMS, String &result, bool &connectionClose)
{
    contentLength = 0;
    bool start = true;
    connectionClose = false;
    bool pngFound = false;

    uint32_t headerTimeout = timeoutMS > 0 ? timeoutMS : OSM_DEFAULT_TIMEOUT_MS;

    while ((currentIsTLS ? secureClient.connected() : client.connected()))
    {
        if (!readLineWithTimeout(headerTimeout))
        {
            result = "Header timeout";
            return false;
        }

        log_d("read header: %s", headerLine);

        if (start)
        {
            if (strncmp(headerLine, "HTTP/1.", 7) != 0)
            {
                result = "Bad HTTP response: ";
                result += headerLine;
                return false;
            }

            // parse status code
            int statusCode = 0;
            const char *reasonPhrase = "";
            const char *sp1 = strchr(headerLine, ' ');
            if (sp1)
            {
                const char *p = sp1 + 1;
                while (*p && isspace((unsigned char)*p))
                    p++;
                while (*p && isdigit((unsigned char)*p))
                {
                    statusCode = statusCode * 10 + (*p - '0');
                    p++;
                }
                if (*p == ' ')
                    reasonPhrase = p + 1;
            }

            if (statusCode != 200)
            {
                result = "HTTP error ";
                result += statusCode;
                if (*reasonPhrase)
                {
                    result += " (";
                    result += reasonPhrase;
                    result += ")";
                }
                return false;
            }

            start = false;
        }

        if (headerLine[0] == '\0') // empty line = end of headers
            break;

        // parse headers
        if (strncasecmp(headerLine, "content-length:", 15) == 0)
        {
            const char *val = headerLine + 15;
            while (*val == ' ' || *val == '\t')
                val++;
            contentLength = atoi(val);
        }
        else if (strncasecmp(headerLine, "connection:", 11) == 0)
        {
            const char *val = headerLine + 11;
            while (*val == ' ' || *val == '\t')
                val++;
            if (strcasecmp(val, "close") == 0)
                connectionClose = true;
        }
        else if (strncasecmp(headerLine, "content-type:", 13) == 0)
        {
            const char *val = headerLine + 13;
            while (*val == ' ' || *val == '\t')
                val++;
            if (strcasecmp(val, "image/png") == 0)
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
                result = "Timeout: body read stalled for ";
                result += maxStall;
                result += " ms";
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

bool ReusableTileFetcher::readLineWithTimeout(uint32_t timeoutMs)
{
    size_t len = 0;
    const uint32_t start = millis();
    bool skipping = false;

    while ((millis() - start) < timeoutMs)
    {
        int availableData = currentIsTLS ? secureClient.available() : client.available();
        if (availableData)
        {
            char c = currentIsTLS ? secureClient.read() : client.read();
            if (c == '\r')
                continue;

            if (c == '\n')
            {
                if (skipping)
                {
                    // We were discarding an oversized line → reset and keep going
                    len = 0;
                    skipping = false;
                    continue; // stay in loop, keep reading next line
                }

                headerLine[len] = '\0';
                return true; // got a usable line
            }

            if (!skipping)
            {
                if (len < sizeof(headerLine) - 1)
                {
                    headerLine[len++] = c;
                }
                else
                {
                    // buffer too small → switch to skipping mode
                    skipping = true;
                    len = 0; // clear partial junk
                }
            }
        }
        else
            taskYIELD();
    }

    return false; // timeout
}
