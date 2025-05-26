/*
    Copyright (c) 2025 Cellie https://github.com/CelliesProjects/OpenStreetMap-esp32

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
    SPDX-License-Identifier: MIT
    */

#ifndef OPENSTREETMAP_ESP32_HPP_
#define OPENSTREETMAP_ESP32_HPP_

#include <Arduino.h>
#include <WiFiClient.h>
#include <SD.h>
#include <vector>
#include <optional>
#include <atomic>
#include <LovyanGFX.hpp>
#include <PNGdec.h>

#include "CachedTile.hpp"
#include "TileJob.hpp"
#include "MemoryBuffer.hpp"
#include "HTTPClientRAII.hpp"

constexpr uint16_t OSM_TILESIZE = 256;
constexpr uint16_t OSM_TILE_TIMEOUT_MS = 2500;
constexpr uint16_t OSM_DEFAULT_CACHE_ITEMS = 10;
constexpr uint16_t OSM_MAX_ZOOM = 18;
constexpr UBaseType_t OSM_TASK_PRIORITY = 10;
constexpr uint32_t OSM_TASK_STACKSIZE = 5120;
constexpr uint32_t OSM_JOB_QUEUE_SIZE = 50;
constexpr bool OSM_FORCE_SINGLECORE = false;

using tileList = std::vector<std::pair<uint32_t, int32_t>>;

namespace
{
    PNG *pngCore0 = nullptr;
    PNG *pngCore1 = nullptr;

    PNG *getPNGForCore()
    {
        auto &ptr = (xPortGetCoreID() == 0) ? pngCore0 : pngCore1;
        if (!ptr)
        {
            void *mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM);
            if (!mem)
                return nullptr;
            ptr = new (mem) PNG();
        }
        return ptr;
    }
}

class OpenStreetMap
{
public:
    OpenStreetMap() = default;
    OpenStreetMap(const OpenStreetMap &) = delete;
    OpenStreetMap &operator=(const OpenStreetMap &) = delete;
    OpenStreetMap(OpenStreetMap &&other) = delete;
    OpenStreetMap &operator=(OpenStreetMap &&other) = delete;

    ~OpenStreetMap();

    void setSize(uint16_t w, uint16_t h);
    bool resizeTilesCache(uint16_t numberOfTiles);
    void freeTilesCache();
    bool fetchMap(LGFX_Sprite &sprite, double longitude, double latitude, uint8_t zoom);

private:
    std::vector<CachedTile> tilesCache;
    static inline thread_local OpenStreetMap *currentInstance = nullptr;
    static inline thread_local uint16_t *currentTileBuffer = nullptr;
    static void PNGDraw(PNGDRAW *pDraw);
    double lon2tile(double lon, uint8_t zoom);
    double lat2tile(double lat, uint8_t zoom);
    void computeRequiredTiles(double longitude, double latitude, uint8_t zoom, tileList &requiredTiles);
    void updateCache(const tileList &requiredTiles, uint8_t zoom);
    void makeJobList(const tileList &requiredTiles, std::vector<TileJob> &jobs, uint8_t zoom);
    void runJobs(const std::vector<TileJob> &jobs);
    CachedTile *findUnusedTile(const tileList &requiredTiles, uint8_t zoom);
    bool isTileCachedOrBusy(uint32_t x, uint32_t y, uint8_t z);
    bool fetchTile(CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result);
    std::optional<std::unique_ptr<MemoryBuffer>> urlToBuffer(const char *url, String &result);
    bool fillBuffer(WiFiClient *stream, MemoryBuffer &buffer, size_t contentSize, String &result);
    bool composeMap(LGFX_Sprite &mapSprite, const tileList &requiredTiles, uint8_t zoom);
    static void tileFetcherTask(void *param);
    bool startTileWorkerTasks();

// cppcheck-suppress unusedPrivateMember
    const int numberOfWorkers = OSM_FORCE_SINGLECORE ? 1 : ESP.getChipCores();
    TaskHandle_t ownerTask = nullptr;
    QueueHandle_t jobQueue = nullptr;
    std::atomic<int> pendingJobs = 0;
    bool tasksStarted = false;

    uint16_t mapWidth = 320;
    uint16_t mapHeight = 240;

    int16_t startOffsetX = 0;
    int16_t startOffsetY = 0;

    int32_t startTileIndexX = 0;
    int32_t startTileIndexY = 0;

    uint16_t numberOfColums = 0;
};

#endif
