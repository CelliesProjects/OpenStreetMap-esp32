#ifndef _OPENSTREETMAP_ESP32_H_
#define _OPENSTREETMAP_ESP32_H_

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <vector>
#include <LovyanGFX.hpp>
#include <PNGdec.h>

#include "CachedTile.h"
#include "MemoryBuffer.h"

constexpr int TILE_SIZE = 256;

class OpenStreetMap
{
    int mapWidth = 320;
    int mapHeight = 240;

    std::vector<CachedTile> tilesCache;
    LGFX_Device &target;
    uint16_t *currentTileBuffer = nullptr;
    PNG png;

    int startOffsetX = 0;
    int startOffsetY = 0;

    int startTileIndexX = 0;
    int startTileIndexY = 0;

public:
    explicit OpenStreetMap(LGFX_Device &gfx)
        : target(gfx) {}

    ~OpenStreetMap();

    void setResolution(int w, int h);
    bool resizeTilesCache(int cacheSize);
    bool fetchMap(LGFX_Sprite &sprite, double longitude, double latitude, int zoom);

private:
    static OpenStreetMap *currentInstance;
    static void PNGDraw(PNGDRAW *pDraw);
    double lon2tile(double lon, int zoom);
    double lat2tile(double lat, int zoom);
    void computeRequiredTiles(double longitude, double latitude, int zoom, std::vector<std::pair<int, int>> &requiredTiles);
    CachedTile *findUnusedTile(const std::vector<std::pair<int, int>> &requiredTiles, int zoom);
    bool isTileCached(int x, int y, int z);
    void freeTilesCache();
    bool downloadAndDecodeTile(CachedTile &tile, int x, int y, int zoom, String &result);
};

#endif
