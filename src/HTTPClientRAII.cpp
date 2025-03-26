#include "HTTPClientRAII.h"

HTTPClientRAII::HTTPClientRAII() : http(new HTTPClient()) {}

HTTPClientRAII::~HTTPClientRAII()
{
    if (http)
    {
        http->end();
        delete http;
        http = nullptr;
    }
}

bool HTTPClientRAII::begin(const String &url)
{
    if (!http)
        return false;

    http->setUserAgent(userAgent);
    return http->begin(url);
}

int HTTPClientRAII::GET()
{
    if (!http)
        return -1;

    return http->GET();
}

size_t HTTPClientRAII::getSize() const
{
    if (!http)
        return 0;

    return http->getSize();
}

WiFiClient *HTTPClientRAII::getStreamPtr()
{
    if (!http)
        return nullptr;

    return http->getStreamPtr();
}

bool HTTPClientRAII::isInitialized() const
{
    return http != nullptr;
}
