## OpenStreetMap-esp32

[![License](https://img.shields.io/github/license/CelliesProjects/OpenStreetMap-esp32)](https://github.com/CelliesProjects/OpenStreetMap-esp32/blob/main/LICENSE)
[![Release](https://img.shields.io/github/v/release/CelliesProjects/OpenStreetMap-esp32)](https://github.com/CelliesProjects/OpenStreetMap-esp32/releases/latest)
[![Issues](https://img.shields.io/github/issues/CelliesProjects/OpenStreetMap-esp32)](https://github.com/CelliesProjects/OpenStreetMap-esp32/issues)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-green?logo=platformio)](https://registry.platformio.org/libraries/celliesprojects/openstreetmap-esp32)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/0961fc2320cd495a9411eb391d5791ca)](https://app.codacy.com/gh/CelliesProjects/OpenStreetMap-esp32/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)

This PlatformIO library provides a [OpenStreetMap](https://www.openstreetmap.org/) (OSM) map fetching and tile caching system for ESP32-based devices.  
Under the hood it uses [LovyanGFX](https://github.com/lovyan03/LovyanGFX) and [PNGdec](https://github.com/bitbank2/PNGdec) to do the heavy lifting.

[![map](https://github.com/user-attachments/assets/39a7f287-c59d-4365-888a-d4c3f77a1dd1 "Click to visit OpenStreetMap.org")](https://www.openstreetmap.org/)

A map is composed from downloaded OSM tiles and returned as a LGFX sprite.  
Tile fetching and decoding is performed concurrently across both cores on dualcore ESP32 devices.  
A composed map can be pushed to the screen, saved to SD or used for further composing.  
Downloaded tiles are cached in psram for reuse.

This should work on any ESP32 type with psram and a LovyanGFX compatible display.  
OSM tiles are quite large at 128kB or insane large at 512kB per tile, so psram is required.

### Multiple tile formats and providers are supported

You can switch provider and tile format at runtime, or set up a different default tile provider if you want.  
This library can do it all and is very easy to configure and use.  

## How to use

This library is **PlatformIO only** due to use of modern C++ features. The Arduino IDE is **not** supported.  
Use [the latest Arduino ESP32 Core version](https://github.com/pioarduino/platform-espressif32/releases/latest) from [pioarduino](https://github.com/pioarduino/platform-espressif32) to compile this library.

See the example PIO settings and example code to get started.

### Example `platformio.ini` settings

These settings use `Arduino Release v3.2.0 based on ESP-IDF v5.4.1` from pioarduino.

```bash
[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.20/platform-espressif32.zip
framework = arduino

lib_deps =
    celliesprojects/OpenStreetMap-esp32@^1.2.0
    lovyan03/LovyanGFX@^1.2.7
    bitbank2/PNGdec@^1.1.3
```

## Functions

### Get the minimum zoom level

```c++
int getMinZoom()
```

### Get the maximum zoom level

```c++
int getMaxZoom()
```

### Set map size

```c++
void setSize(uint16_t w, uint16_t h)
```

- If no size is set a 320px by 240px map will be returned.  
- The tile cache might need resizing if the size is increased. 

### Get the number of tiles needed to cache a map

```c++
uint16_t tilesNeeded(uint16_t w, uint16_t h)
```

This returns the number of tiles required to cache the given map size.  

### Resize the tiles cache

```c++
bool resizeTilesCache(uint16_t numberOfTiles)
```

- The cache content is cleared before resizing.
- Each 256px tile allocates **128kB** psram.
- Each 512px tile allocates **512kB** psram.

**Don't over-allocate**  
When resizing the cache, keep in mind that the map sprite also uses psram.  
The PNG decoders -~50kB for each core- also live in psram.  
Use the above `tilesNeeded` function to calculate a safe and sane cache size if you change the map size.  

### Fetch a map

```c++
bool fetchMap(LGFX_Sprite &map, double longitude, double latitude, uint8_t zoom, unsigned long timeoutMS = 0)
```

- Overflowing `longitude` are wrapped and normalized to +-180°.
- Overflowing `latitude` are clamped to +-90°.
- Valid range for the `zoom` level is from `getMinZoom()` to `getMaxZoom()`.  
- `timeoutMS` can be used to throttle the amount of downloaded tiles per call.  
Setting it to anything other than `0` sets a timeout. Sane values start around ~100ms.  
**Note:** No more tiles will be downloaded after the timeout expires, but tiles that are downloading will be finished.  
**Note:** You might end up with missing map tiles. Or no map at all if you set the timeout too short.

### Free the psram memory used by the tile cache

```c++
void freeTilesCache()
```

- Does **not** free the PNG decoder(s).

### Switch to a different tile provider

```c++
bool setTileProvider(int index)
```

This function will switch to tile provider `index` defined in `src/TileProvider.hpp`.  

- Returns `true` and clears the cache on success.  
- Returns `false` -and the current tile provider is unchanged- if no provider at the index is defined.

### Get the number of defined providers

`OSM_TILEPROVIDERS` gives the number of defined providers.  

Example use:  

```c++
const int numberOfProviders = OSM_TILEPROVIDERS;
```

**Note:** In the default setup there is only one provider defined.

### Get the provider name

```c++
char *getProviderName()
```

## Adding tile providers

See `src/TileProvider.hpp` for example setups for [https://www.thunderforest.com/](https://www.thunderforest.com/) that only require you to register for a **free** API key and adjusting/uncommenting 2 lines in the config.  
Register for a ThunderForest free tier [here](https://manage.thunderforest.com/users/sign_up?price=hobby-project-usd) without needing a creditcard to sign up.

If you encounter a problem or want to request support for a new provider, please check the [issue tracker](../../issues) for existing reports or [open an issue](../../issues/new).

## Example code

### Example returning the default 320x240 map

```c++
#include <Arduino.h>
#include <WiFi.h>

#define LGFX_M5STACK_CORE2  // for supported devices see 
                            // https://github.com/lovyan03/LovyanGFX

#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>

#include <OpenStreetMap-esp32.hpp>

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

#include <OpenStreetMap-esp32.hpp>

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
    osm.setSize(mapWidth, mapHeight);

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

## License differences between this library and the map data

### This library has a MIT license

The `OpenstreetMap-esp32` library -this library- is licensed under the [MIT license](/LICENSE).

### The downloaded tile data has a ODbL license

OpenStreetMap® is open data, licensed under the [Open Data Commons Open Database License (ODbL)](https://opendatacommons.org/licenses/odbl/) by the OpenStreetMap Foundation (OSMF).

Use of any OSMF provided service is governed by the [OSMF Terms of Use](https://osmfoundation.org/wiki/Terms_of_Use).