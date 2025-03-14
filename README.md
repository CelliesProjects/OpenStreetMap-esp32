# OpenStreetMap-esp32

This library provides a lightweight OpenStreetMap (OSM) map fetching and caching system for ESP32-based devices.

It fetches and caches OSM tiles, composes a map from these tiles and returns a LGFX sprite. The sprite can be pushed to the screen or used for further composing.

![scaledMap](https://github.com/user-attachments/assets/772ba198-4602-45fa-a67a-6eb802f22771)
### example code

```c++
#include <Arduino.h>
#include <WiFi.h>

#define LGFX_M5STACK_CORE2  // for supported devices see 
                            // https://github.com/lovyan03/LovyanGFX

#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>

#include <OpenStreetMap-esp32.h>

const char *ssid = "xxx";
const char *password = "xxx";

LGFX display;
OpenStreetMap osm(display);

double latitude = 52.52;
double longitude = 13.41;
int zoom = 14;

void setup()
{
    Serial.begin(115200);
    Serial.printf("WiFi connecting to %s\n", ssid);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(10);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected");

    display.begin();
    display.setRotation(1);
    display.setBrightness(110);

    // create a sprite to store the map, default returned size is 320px by 240px
    LGFX_Sprite map(&display); 

    const bool success = osm.fetchMap(map, longitude, latitude, zoom);

    if (success)
        map.pushSprite(0, 0);
    else
        Serial.println("Failed to fetch map.");
}

void loop()
{
    delay(1000);
}
```

### example code setting a map size and cache size on RGB panel devices

```c++
#include <Arduino.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>

#include "LGFX_ESP32_8048S050C.hpp" // replace with your panel config

#include <OpenStreetMap-esp32.h>

const char *ssid = "xxx";
const char *password = "xxx";

LGFX display;
OpenStreetMap osm(display);

double latitude = 52.52;
double longitude = 13.41;
int zoom = 14;

void setup()
{
    Serial.begin(115200);
    Serial.printf("WiFi connecting to %s\n", ssid);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(10);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected");

    display.begin();
    display.setRotation(1);
    display.setBrightness(110);

    osm.resizeTilesCache(16);
    osm.setResolution(480, 800);

    LGFX_Sprite map(&display);

    const bool success = osm.fetchMap(map, longitude, latitude, zoom);
    if (success)
    {
        // Draw a crosshair on the map
        map.drawLine(0, map.height() / 2, map.width(), map.height() / 2, 0);
        map.drawLine(map.width() / 2, 0, map.width() / 2, map.height(), 0);

        String location = " Map data from OpenStreetMap.org ";
        map.setTextColor(TFT_WHITE, TFT_BLACK);
        map.drawRightString(location.c_str(), map.width(), map.height() - 10);

        map.pushSprite(0, 0);
    }
}

void loop()
{
    delay(1000);
}
```
