#include "ReusableTileFetcher.hpp"

ReusableTileFetcher::ReusableTileFetcher() {}
ReusableTileFetcher::~ReusableTileFetcher() { client.stop(); }

std::unique_ptr<MemoryBuffer> ReusableTileFetcher::fetchToBuffer(const String &url, String &result)
{
    String host, path;
    uint16_t port;
    if (!parseUrl(url, host, path, port))
    {
        result = "Invalid URL";
        return nullptr;
    }

    if (!ensureConnection(host, port, result))
        return nullptr;

    if (!sendHttpRequest(host, path))
    {
        result = "Failed to send HTTP GET request";
        return nullptr;
    }

    size_t contentLength = 0;
    if (!readHttpHeaders(contentLength, result))
        return nullptr;

    auto buffer = std::make_unique<MemoryBuffer>(contentLength);
    if (!buffer->isAllocated())
    {
        result = "Buffer allocation failed";
        return nullptr;
    }

    if (!readBody(*buffer, contentLength, result))
        return nullptr;

    return buffer;
}

bool ReusableTileFetcher::parseUrl(const String &url, String &host, String &path, uint16_t &port)
{
    port = 80;

    if (url.startsWith("https://"))
    {
        // Not supported in this fetcher (requires WiFiClientSecure)
        return false;
    }

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
        client.stop(); // Close old connection if mismatched
        if (!client.connect(host.c_str(), port))
        {
            result = "Connection failed to " + host;
            return false;
        }
        currentHost = host;
        currentPort = port;
    }
    return true;
}

bool ReusableTileFetcher::sendHttpRequest(const String &host, const String &path)
{
    client.print(String("GET ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print("User-Agent: OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)\r\n");
    client.print("Connection: keep-alive\r\n");
    client.print("\r\n");
    return true;
}

bool ReusableTileFetcher::readHttpHeaders(size_t &contentLength, String &result)
{
    String line;
    contentLength = 0;
    while (client.connected())
    {
        line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
            break; // End of headers

        if (line.startsWith("Content-Length:"))
        {
            contentLength = line.substring(15).toInt();
        }
        else if (line.startsWith("HTTP/1.1"))
        {
            if (!line.startsWith("HTTP/1.1 200"))
            {
                result = "HTTP error: " + line;
                return false;
            }
        }
    }

    if (contentLength == 0)
    {
        result = "Missing or invalid Content-Length";
        return false;
    }

    return true;
}

bool ReusableTileFetcher::readBody(MemoryBuffer &buffer, size_t contentLength, String &result)
{
    uint8_t *dest = buffer.get();
    size_t remaining = contentLength;
    size_t offset = 0;

    unsigned long start = millis();
    while (remaining > 0 && millis() - start < 3000)
    {
        int len = client.read(dest + offset, remaining);
        if (len > 0)
        {
            remaining -= len;
            offset += len;
        }
        else if (len < 0)
        {
            result = "Read error";
            return false;
        }
        else
        {
            delay(1);
        }
    }

    if (remaining > 0)
    {
        result = "Incomplete read";
        return false;
    }

    return true;
}
