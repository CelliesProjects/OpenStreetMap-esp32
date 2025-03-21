## OpenStreetMap-esp32

[![Codacy Badge](https://app.codacy.com/project/badge/Grade/0961fc2320cd495a9411eb391d5791ca)](https://app.codacy.com/gh/CelliesProjects/OpenStreetMap-esp32/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)

### What is this

This library provides a [OpenStreetMap](https://www.openstreetmap.org/) (OSM) map fetching and tile caching system for ESP32-based devices.
Under the hood it uses [LovyanGFX](https://github.com/lovyan03/LovyanGFX) and [PNGdec](https://github.com/bitbank2/PNGdec) to do the heavy lifting.

![map](https://github.com/user-attachments/assets/bc0534c1-b2e6-4f6e-804f-95b7db00c850)

A map is composed from downloaded OSM tiles and returned as a LGFX sprite.
The sprite can be pushed to the screen or used for further composing.
Downloaded tiles are cached in psram for reuse.

The library should work on any ESP32 type with psram and a LovyanGFX compatible display.

The downloaded tile cache gets large very quickly -128kB per tile- so a ESP32 with psram is required.

### Functions

#### Set map resolution

```c++
void setResolution(uint16_t w, uint16_t h);
```

- If no resolution is set, a 320 by 240 map will be returned by `fetchMap`.

#### Resize cache 

```c++
bool resizeTilesCache(uint8_t numberOfTiles); 
```

- The cache is cleared before resizing.
- Each tile is 128 kB.

#### Free the memory used by the tile cache

```c++
void freeTilesCache();
```

#### Fetch a map

```c++
bool fetchMap(LGFX_Sprite &map, double longitude, double latitude, uint8_t zoom);
```

#### Save a map to SD card

```c++
bool saveMap(const char *filename, LGFX_Sprite &map, String &result, 
             uint8_t sdPin = SS, uint32_t frequency = 4000000)
```

- `filename` should start with `/` for example `/map.bmp` or `/images/map.bmp` 
- `result` returns something like `SD Card mount failed` or `Screenshot saved`.
- `sdPin` is optional and used to set a `SS/CS` pin for the SD slot.
- `frequency` is optional and used to set the SD speed.

## License differences between this library and the map data

### This library has a MIT license

The `OpenstreetMap-esp32` library -this library- is licensed under the [MIT license](/LICENSE).  
This project is not endorsed by or affiliated with the OpenStreetMap Foundation.

### The downloaded tile data has a Open Data Commons Open Database License (ODbL)

OpenStreetMap® is open data, licensed under the [Open Data Commons Open Database License (ODbL)](https://opendatacommons.org/licenses/odbl/) by the OpenStreetMap Foundation (OSMF).

Use of any OSMF provided service is governed by the [OSMF Terms of Use](https://osmfoundation.org/wiki/Terms_of_Use).

## Example code

### Example returning the default 320x240 map

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
OpenStreetMap osm;

double longitude = 5.9;
double latitude = 51.5;
int zoom = 5;

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

    // returned map is 320px by 240px by default
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

### Example setting map resolution and cache size on RGB panel devices

```c++
#include <Arduino.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>

#include "LGFX_ESP32_8048S050C.hpp" // replace with your panel config

#include <OpenStreetMap-esp32.h>

const char *ssid = "xxx";
const char *password = "xxx";

LGFX display;
OpenStreetMap osm;

int mapWidth = 480;
int mapHeight = 800;
int cacheSize = 20; // cache size in tiles where each osm tile is 128kB
double longitude = 5.9;
double latitude = 51.5;
int zoom = 5;

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

### Screenshot of a 480x800 map from a esp32-8048s050

![map](https://github.com/user-attachments/assets/9a92bbff-e96d-444d-8b34-29801744fa80)

### PlatformIO setup

```bash
lib_deps =
    https://github.com/CelliesProjects/OpenStreetMap-esp32
    lovyan03/LovyanGFX@^1.2.0
    bitbank2/PNGdec@^1.0.3  
```
