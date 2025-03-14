# OpenStreetMap-esp32

This library provides a OpenStreetMap (OSM) map fetching and caching system for ESP32-based devices.<br>
Under the hood it uses [LovyanGFX](https://github.com/lovyan03/LovyanGFX) and [PNGdec](https://github.com/bitbank2/PNGdec) to do the heavy lifting.

It fetches, decodes and caches OSM tiles, composes a map from these tiles and returns the map as a LGFX sprite.<br>The sprite can be pushed to the screen or used for further composing.<br>Downloaded tiles are cached in psram.

The library should work on any ESP32 type with a bit of psram and a LovyanGFX compatible display.

##### Screenshot of a 480x800 map sprite on a esp32-8048s050 RGB panel
![scaledMap](https://github.com/user-attachments/assets/3c30ae46-e499-4d50-af0f-da4156fe5374)

### Example code returning a default 320x240 map

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

    // create a sprite to store the map
    LGFX_Sprite map(&display); 

    const bool success = osm.fetchMap(map, longitude, latitude, zoom);
    // returned map is 320px by 240px by default

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

### Example code setting map resolution and cache size on RGB panel devices

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

int mapWidth = 480;
int mapHeight = 800;
int cacheSize = 20; // cache size in tiles where each osm tile is 128kB
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

    osm.resizeTilesCache(cacheSize);
    osm.setResolution(mapWidth, mapHeight);

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
    else
        Serial.println("Failed to fetch map.");
}

void loop()
{
    delay(1000);
}
```

### PlatformIO setup
```bash
lib_deps =
    https://github.com/CelliesProjects/OpenStreetMap-esp32
    lovyan03/LovyanGFX@^1.2.0
    bitbank2/PNGdec@^1.0.3    
```
