#ifndef HTTPCLIENTRAII_H
#define HTTPCLIENTRAII_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

class HTTPClientRAII
{
public:
    HTTPClientRAII();
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
