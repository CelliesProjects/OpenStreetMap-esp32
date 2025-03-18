#ifndef OPENSTREETMAP_ESP32_H
#define OPENSTREETMAP_ESP32_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SD.h>
#include <vector>
#include <LovyanGFX.hpp>
#include <PNGdec.h>

#include "CachedTile.h"
#include "MemoryBuffer.h"

constexpr int16_t OSM_TILESIZE = 256;
constexpr uint8_t OSM_DEFAULT_CACHE_ITEMS = 10;

using tileList = std::vector<std::pair<uint32_t, uint32_t>>;

class OpenStreetMap
{
public:
    OpenStreetMap() = default;
    OpenStreetMap(const OpenStreetMap &) = delete;
    OpenStreetMap &operator=(const OpenStreetMap &) = delete;
    OpenStreetMap(OpenStreetMap &&other) = delete;
    OpenStreetMap &operator=(OpenStreetMap &&other) = delete;

    ~OpenStreetMap();

    void setResolution(uint16_t w, uint16_t h);
    bool resizeTilesCache(uint8_t numberOfTiles);
    void freeTilesCache();
    bool fetchMap(LGFX_Sprite &sprite, double longitude, double latitude, uint8_t zoom);
    bool saveMap(const char *filename, LGFX_Sprite &display, String &result, uint8_t sdPin = SS);

private:
    static OpenStreetMap *currentInstance;
    static void PNGDraw(PNGDRAW *pDraw);
    double lon2tile(double lon, uint8_t zoom);
    double lat2tile(double lat, uint8_t zoom);
    void computeRequiredTiles(double longitude, double latitude, uint8_t zoom, tileList &requiredTiles);
    void updateCache(tileList &requiredTiles, uint8_t zoom);
    bool isTileCached(uint32_t x, uint32_t y, uint8_t z);
    CachedTile *findUnusedTile(const tileList &requiredTiles, uint8_t zoom);
    bool downloadAndDecodeTile(CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result);
    bool composeMap(LGFX_Sprite &mapSprite, tileList &requiredTiles, uint8_t zoom);

    uint16_t mapWidth = 320;
    uint16_t mapHeight = 240;

    std::vector<CachedTile> tilesCache;
    uint16_t *currentTileBuffer = nullptr;
    PNG png;

    int16_t startOffsetX = 0;
    int16_t startOffsetY = 0;

    int32_t startTileIndexX = 0;
    int32_t startTileIndexY = 0;

    uint16_t numberOfColums = 0;    
};

#endif
