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
#include <atomic>
#include <LovyanGFX.hpp>
#include <PNGdec.h>

#include "TileProvider.hpp"
#include "CachedTile.hpp"
#include "TileJob.hpp"
#include "MemoryBuffer.hpp"
#include "ReusableTileFetcher.hpp"
#include "fonts/DejaVu9-modded.h"

constexpr uint16_t OSM_BGCOLOR = lgfx::color565(32, 32, 128);
constexpr UBaseType_t OSM_TASK_PRIORITY = 1;
constexpr uint32_t OSM_TASK_STACKSIZE = 6144;
constexpr uint32_t OSM_JOB_QUEUE_SIZE = 50;
constexpr bool OSM_FORCE_SINGLECORE = false;
constexpr int OSM_SINGLECORE_NUMBER = 1;

static_assert(OSM_SINGLECORE_NUMBER < 2, "OSM_SINGLECORE_NUMBER must be 0 or 1 (ESP32 has only 2 cores)");

using tileList = std::vector<std::pair<uint32_t, int32_t>>;
using TileBufferList = std::vector<uint16_t *>;

namespace
{
    PNG *pngCore0 = nullptr;
    PNG *pngCore1 = nullptr;

    PNG *getPNGForCore(int coreID)
    {
        PNG *&ptr = (coreID == 0) ? pngCore0 : pngCore1;
        if (!ptr)
        {
            void *mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM);
            if (!mem)
                return nullptr;
            ptr = new (mem) PNG();
        }
        return ptr;
    }

    PNG *getPNGCurrentCore()
    {
        return getPNGForCore(xPortGetCoreID());
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
    uint16_t tilesNeeded(uint16_t mapWidth, uint16_t mapHeight);
    bool resizeTilesCache(uint16_t numberOfTiles);
    bool fetchMap(LGFX_Sprite &sprite, double longitude, double latitude, uint8_t zoom, unsigned long timeoutMS = 0);
    inline void freeTilesCache();

    bool setTileProvider(int index);
    const char *getProviderName() { return currentProvider->name; };
    int getMinZoom() const { return currentProvider->minZoom; };
    int getMaxZoom() const { return currentProvider->maxZoom; };

private:
    double lon2tile(double lon, uint8_t zoom);
    double lat2tile(double lat, uint8_t zoom);
    void computeRequiredTiles(double longitude, double latitude, uint8_t zoom, tileList &requiredTiles);
    void updateCache(const tileList &requiredTiles, uint8_t zoom, TileBufferList &tilePointers);
    bool startTileWorkerTasks();
    void makeJobList(const tileList &requiredTiles, std::vector<TileJob> &jobs, uint8_t zoom, TileBufferList &tilePointers);
    void runJobs(const std::vector<TileJob> &jobs);
    CachedTile *findUnusedTile(const tileList &requiredTiles, uint8_t zoom);
    CachedTile *isTileCached(uint32_t x, uint32_t y, uint8_t z);
    bool fetchTile(ReusableTileFetcher &fetcher, CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result, unsigned long timeoutMS);
    bool composeMap(LGFX_Sprite &mapSprite, TileBufferList &tilePointers);
    static void tileFetcherTask(void *param);
    static void PNGDraw(PNGDRAW *pDraw);
    void invalidateTile(CachedTile *tile);

    static inline thread_local OpenStreetMap *currentInstance = nullptr;
    static inline thread_local uint16_t *currentTileBuffer = nullptr;
    const TileProvider *currentProvider = &tileProviders[0];
    std::vector<CachedTile> tilesCache;

    TaskHandle_t ownerTask = nullptr;
    int numberOfWorkers = 0;
    QueueHandle_t jobQueue = nullptr;
    std::atomic<int> pendingJobs = 0;
    bool tasksStarted = false;

    unsigned long mapTimeoutMS = 0; // 0 means no timeout
    unsigned long startJobsMS = 0;

    uint16_t mapWidth = 320;
    uint16_t mapHeight = 240;

    int16_t startOffsetX = 0;
    int16_t startOffsetY = 0;

    int32_t startTileIndexX = 0;
    int32_t startTileIndexY = 0;

    uint16_t numberOfColums = 0;
};

#endif
