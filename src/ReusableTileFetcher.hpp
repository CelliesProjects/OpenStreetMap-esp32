#pragma once

#include <WiFiClient.h>
#include <memory>
#include "MemoryBuffer.hpp" // your existing class

class ReusableTileFetcher {
public:
    ReusableTileFetcher();
    ~ReusableTileFetcher();

    // Not copyable/movable
    ReusableTileFetcher(const ReusableTileFetcher &) = delete;
    ReusableTileFetcher &operator=(const ReusableTileFetcher &) = delete;

    std::unique_ptr<MemoryBuffer> fetchToBuffer(const String &url, String &result);

private:
    WiFiClient client;
    String currentHost;
    uint16_t currentPort = 80;

    bool parseUrl(const String &url, String &host, String &path, uint16_t &port);
    bool ensureConnection(const String &host, uint16_t port, String &result);
    bool sendHttpRequest(const String &host, const String &path);
    bool readHttpHeaders(size_t &contentLength, String &result);
    bool readBody(MemoryBuffer &buffer, size_t contentLength, String &result);
};
