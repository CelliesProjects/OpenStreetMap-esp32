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

#include "OpenStreetMap-esp32.hpp"

OpenStreetMap::~OpenStreetMap()
{
    if (jobQueue && tasksStarted)
    {
        constexpr TileJob poison = {0, 0, 255, nullptr};
        for (int i = 0; i < numberOfWorkers; ++i)
            if (xQueueSend(jobQueue, &poison, portMAX_DELAY) != pdPASS)
                log_e("Failed to send poison pill to tile worker %d", i);

        for (int i = 0; i < numberOfWorkers; ++i)
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ownerTask = nullptr;
        tasksStarted = false;
        numberOfWorkers = 0;

        vQueueDelete(jobQueue);
        jobQueue = nullptr;
    }

    freeTilesCache();

    if (pngCore0)
    {
        pngCore0->~PNG();
        heap_caps_free(pngCore0);
        pngCore0 = nullptr;
    }
    if (pngCore1)
    {
        pngCore1->~PNG();
        heap_caps_free(pngCore1);
        pngCore1 = nullptr;
    }
}

void OpenStreetMap::setSize(uint16_t w, uint16_t h)
{
    mapWidth = w;
    mapHeight = h;
}

double OpenStreetMap::lon2tile(double lon, uint8_t zoom)
{
    return (lon + 180.0) / 360.0 * (1 << zoom);
}

double OpenStreetMap::lat2tile(double lat, uint8_t zoom)
{
    double latRad = lat * M_PI / 180.0;
    return (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * (1 << zoom);
}

void OpenStreetMap::computeRequiredTiles(double longitude, double latitude, uint8_t zoom, tileList &requiredTiles)
{
    // Compute exact tile coordinates
    const double exactTileX = lon2tile(longitude, zoom);
    const double exactTileY = lat2tile(latitude, zoom);

    // Determine the integer tile indices
    const int32_t targetTileX = static_cast<int32_t>(exactTileX);
    const int32_t targetTileY = static_cast<int32_t>(exactTileY);

    // Compute the offset inside the tile for the given coordinates
    const int16_t targetOffsetX = (exactTileX - targetTileX) * currentProvider->tileSize;
    const int16_t targetOffsetY = (exactTileY - targetTileY) * currentProvider->tileSize;

    // Compute the offset for tiles covering the map area to keep the location centered
    const int16_t tilesOffsetX = mapWidth / 2 - targetOffsetX;
    const int16_t tilesOffsetY = mapHeight / 2 - targetOffsetY;

    // Compute number of colums required
    const float colsLeft = 1.0 * tilesOffsetX / currentProvider->tileSize;
    const float colsRight = float(mapWidth - (tilesOffsetX + currentProvider->tileSize)) / currentProvider->tileSize;
    numberOfColums = ceil(colsLeft) + 1 + ceil(colsRight);

    startOffsetX = tilesOffsetX - (ceil(colsLeft) * currentProvider->tileSize);

    // Compute number of rows required
    const float rowsTop = 1.0 * tilesOffsetY / currentProvider->tileSize;
    const float rowsBottom = float(mapHeight - (tilesOffsetY + currentProvider->tileSize)) / currentProvider->tileSize;
    const uint32_t numberOfRows = ceil(rowsTop) + 1 + ceil(rowsBottom);

    startOffsetY = tilesOffsetY - (ceil(rowsTop) * currentProvider->tileSize);

    log_v(" Need %i * %i tiles. First tile offset is %d,%d",
          numberOfColums, numberOfRows, startOffsetX, startOffsetY);

    startTileIndexX = targetTileX - ceil(colsLeft);
    startTileIndexY = targetTileY - ceil(rowsTop);

    log_v("top left tile indices: %d, %d", startTileIndexX, startTileIndexY);

    const int32_t worldTileWidth = 1 << zoom;
    for (int32_t y = 0; y < numberOfRows; ++y)
    {
        for (int32_t x = 0; x < numberOfColums; ++x)
        {
            int32_t tileX = startTileIndexX + x;
            const int32_t tileY = startTileIndexY + y;

            // Apply modulo wrapping for tileX
            // see https://godbolt.org/z/96e1x7j7r
            tileX = (tileX % worldTileWidth + worldTileWidth) % worldTileWidth;
            requiredTiles.emplace_back(tileX, tileY);
        }
    }
}

CachedTile *OpenStreetMap::findUnusedTile(const tileList &requiredTiles, uint8_t zoom)
{
    for (auto &tile : tilesCache)
    {
        if (tile.busy)
            continue;

        // If a tile is valid but not required in the current frame, we can replace it
        bool needed = false;
        for (const auto &[x, y] : requiredTiles)
        {
            if (tile.x == x && tile.y == y && tile.z == zoom && tile.valid)
            {
                needed = true;
                break;
            }
        }
        if (!needed)
        {
            tile.busy = true;
            return &tile;
        }
    }

    return nullptr; // no unused tile found
}

CachedTile *OpenStreetMap::isTileCached(uint32_t x, uint32_t y, uint8_t z)
{
    for (auto &tile : tilesCache)
    {
        if (tile.x == x && tile.y == y && tile.z == z && tile.valid)
            return &tile;
    }
    return nullptr;
}

void OpenStreetMap::freeTilesCache()
{
    std::vector<CachedTile>().swap(tilesCache);
}

bool OpenStreetMap::resizeTilesCache(uint16_t numberOfTiles)
{
    if (!numberOfTiles)
    {
        log_e("Invalid cache size: %d", numberOfTiles);
        return false;
    }

    freeTilesCache();
    tilesCache.resize(numberOfTiles);

    for (auto &tile : tilesCache)
    {
        if (!tile.allocate(currentProvider->tileSize))
        {
            log_e("Tile cache allocation failed!");
            freeTilesCache();
            return false;
        }
    }
    return true;
}

void OpenStreetMap::updateCache(const tileList &requiredTiles, uint8_t zoom, TileBufferList &tilePointers)
{
    [[maybe_unused]] const unsigned long startMS = millis();
    std::vector<TileJob> jobs;
    makeJobList(requiredTiles, jobs, zoom, tilePointers);
    if (!jobs.empty())
    {
        runJobs(jobs);
        log_i("Finished %i jobs in %lu ms - %i ms/job", jobs.size(), millis() - startMS, (millis() - startMS) / jobs.size());
    }
}

void OpenStreetMap::makeJobList(const tileList &requiredTiles, std::vector<TileJob> &jobs, uint8_t zoom, TileBufferList &tilePointers)
{
    for (const auto &[x, y] : requiredTiles)
    {
        if (y < 0 || y >= (1 << zoom))
        {
            tilePointers.emplace_back(nullptr); // keep alignment
            continue;
        }

        const CachedTile *cachedTile = isTileCached(x, y, zoom);
        if (cachedTile)
        {
            tilePointers.emplace_back(const_cast<CachedTile *>(cachedTile));
            continue;
        }

        const auto job = std::find_if(jobs.begin(), jobs.end(), [&](const TileJob &job)
                                      { return job.x == x && job.y == static_cast<uint32_t>(y) && job.z == zoom; });
        if (job != jobs.end())
        {
            tilePointers.emplace_back(const_cast<CachedTile *>(cachedTile));
            continue;
        }

        CachedTile *tileToReplace = findUnusedTile(requiredTiles, zoom);
        if (!tileToReplace)
        {
            log_e("Cache error, no unused tile found, could not store tile %lu, %i, %u", x, y, zoom);
            tilePointers.emplace_back(nullptr); // keep alignment
            continue;
        }

        // store buffer and current validity for rendering
        tilePointers.emplace_back(tileToReplace);
        jobs.push_back({x, static_cast<uint32_t>(y), zoom, tileToReplace});
    }
}

void OpenStreetMap::runJobs(const std::vector<TileJob> &jobs)
{
    log_d("submitting %i jobs", (int)jobs.size());

    pendingJobs.store(jobs.size());
    startJobsMS = millis();
    for (const TileJob &job : jobs)
        if (xQueueSend(jobQueue, &job, portMAX_DELAY) != pdPASS)
        {
            log_e("Failed to enqueue TileJob");
            --pendingJobs;
        }

    while (pendingJobs.load() > 0)
        vTaskDelay(pdMS_TO_TICKS(1));
}

bool OpenStreetMap::composeMap(LGFX_Sprite &mapSprite, TileBufferList &tilePointers)
{
    if (mapSprite.width() != mapWidth || mapSprite.height() != mapHeight)
    {
        mapSprite.deleteSprite();
        mapSprite.setPsram(true);
        mapSprite.setColorDepth(lgfx::rgb565_2Byte);
        mapSprite.createSprite(mapWidth, mapHeight);
        if (!mapSprite.getBuffer())
        {
            log_e("could not allocate map");
            return false;
        }
    }

    for (size_t tileIndex = 0; tileIndex < tilePointers.size(); ++tileIndex)
    {
        const int drawX = startOffsetX + (tileIndex % numberOfColums) * currentProvider->tileSize;
        const int drawY = startOffsetY + (tileIndex / numberOfColums) * currentProvider->tileSize;
        const TileBuffer &tb = tilePointers[tileIndex];
        CachedTile *ct = tb.cached;

        // draw background if no tile or tile not valid yet
        if (!ct || !ct->valid || !ct->buffer)
        {
            mapSprite.fillRect(drawX, drawY, currentProvider->tileSize, currentProvider->tileSize, OSM_BGCOLOR);
            continue;
        }

        // draw from live cached buffer
        mapSprite.pushImage(drawX, drawY, currentProvider->tileSize, currentProvider->tileSize, ct->buffer);
    }

    mapSprite.setTextColor(TFT_WHITE, OSM_BGCOLOR);
    mapSprite.drawRightString(currentProvider->attribution, mapSprite.width(), mapSprite.height() - 10, &DejaVu9Modded);
    mapSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    return true;
}

bool OpenStreetMap::fetchMap(LGFX_Sprite &mapSprite, double longitude, double latitude, uint8_t zoom, unsigned long timeoutMS)
{
    if (!tasksStarted && !startTileWorkerTasks())
    {
        log_e("Failed to start tile worker(s)");
        return false;
    }

    if (zoom < currentProvider->minZoom || zoom > currentProvider->maxZoom)
    {
        log_e("Invalid zoom level: %d", zoom);
        return false;
    }

    if (!mapWidth || !mapHeight)
    {
        log_e("Invalid map dimension");
        return false;
    }

    if (!tilesCache.capacity() && !resizeTilesCache(tilesNeeded(mapWidth, mapHeight)))
    {
        log_e("Could not allocate tile cache");
        return false;
    }

    // Web Mercator projection only supports latitudes up to ~85.0511°.
    // See https://en.wikipedia.org/wiki/Web_Mercator_projection#Formulas
    // We use 85.0° as a safe and simple boundary.
    constexpr double MAX_MERCATOR_LAT = 85.0;

    longitude = fmod(longitude + 180.0, 360.0) - 180.0;
    latitude = std::clamp(latitude, -MAX_MERCATOR_LAT, MAX_MERCATOR_LAT);

    tileList requiredTiles;
    computeRequiredTiles(longitude, latitude, zoom, requiredTiles);
    if (tilesCache.capacity() < requiredTiles.size())
    {
        log_e("Caching error: Need %i cache slots, but only %i are provided", requiredTiles.size(), tilesCache.capacity());
        return false;
    }

    mapTimeoutMS = timeoutMS;
    TileBufferList tilePointers;
    updateCache(requiredTiles, zoom, tilePointers);
    if (!composeMap(mapSprite, tilePointers))
    {
        log_e("Failed to compose map");
        return false;
    }
    return true;
}

void OpenStreetMap::PNGDraw(PNGDRAW *pDraw)
{
    uint16_t *destRow = currentInstance->currentTileBuffer + (pDraw->y * currentInstance->currentProvider->tileSize);
    getPNGCurrentCore()->getLineAsRGB565(pDraw, destRow, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
}

bool OpenStreetMap::fetchTile(ReusableTileFetcher &fetcher, CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result, unsigned long timeout)
{
    char url[256];
    if (currentProvider->requiresApiKey)
    {
        snprintf(url, sizeof(url),
                 currentProvider->urlTemplate,
                 zoom, x, y, currentProvider->apiKey);
    }
    else
    {
        snprintf(url, sizeof(url),
                 currentProvider->urlTemplate,
                 zoom, x, y);
    }

    MemoryBuffer buffer = fetcher.fetchToBuffer(url, result, timeout);
    if (!buffer.isAllocated())
        return false;

    [[maybe_unused]] const unsigned long startMS = millis();

    PNG *png = getPNGCurrentCore();
    const int16_t rc = png->openRAM(buffer.get(), buffer.size(), PNGDraw);
    if (rc != PNG_SUCCESS)
    {
        result = "PNG Decoder Error: " + String(rc);
        return false;
    }

    if (png->getWidth() != currentProvider->tileSize || png->getHeight() != currentProvider->tileSize)
    {
        result = "Unexpected tile size: w=" + String(png->getWidth()) + " h=" + String(png->getHeight());
        return false;
    }

    currentInstance = this;
    currentTileBuffer = tile.buffer;
    const int decodeResult = png->decode(0, PNG_FAST_PALETTE);
    if (decodeResult != PNG_SUCCESS)
    {
        result = "Decoding " + String(url) + " failed with code: " + String(decodeResult);
        return false;
    }

    log_d("decoding %s took %lu ms on core %i", url, millis() - startMS, xPortGetCoreID());

    tile.x = x;
    tile.y = y;
    tile.z = zoom;
    return true;
}

void OpenStreetMap::tileFetcherTask(void *param)
{
    ReusableTileFetcher fetcher;
    OpenStreetMap *osm = static_cast<OpenStreetMap *>(param);
    while (true)
    {
        TileJob job;
        xQueueReceive(osm->jobQueue, &job, portMAX_DELAY);
        [[maybe_unused]] const unsigned long startMS = millis();

        if (job.z == 255)
            break;

        const uint32_t elapsedMS = millis() - osm->startJobsMS;
        if (osm->mapTimeoutMS && elapsedMS >= osm->mapTimeoutMS)
        {
            log_w("Map timeout (%lu ms) exceeded after %lu ms, dropping job",
                  osm->mapTimeoutMS, elapsedMS);

            osm->invalidateTile(job.tile);
            --osm->pendingJobs;
            continue;
        }

        uint32_t remainingMS = 0;
        if (osm->mapTimeoutMS > 0)
        {
            remainingMS = osm->mapTimeoutMS - elapsedMS;
            if (remainingMS == 0)
            {
                log_w("No budget left for job, dropping");
                osm->invalidateTile(job.tile);
                --osm->pendingJobs;
                continue;
            }
        }

        String result;
        if (!osm->fetchTile(fetcher, *job.tile, job.x, job.y, job.z, result, remainingMS))
        {
            log_e("Tile fetch failed: %s", result.c_str());
            osm->invalidateTile(job.tile);
        }
        else
        {
            job.tile->valid = true;
            log_d("core %i fetched tile z=%u x=%lu, y=%lu in %lu ms",
                  xPortGetCoreID(), job.z, job.x, job.y, millis() - startMS);
        }
        job.tile->busy = false;
        --osm->pendingJobs;
    }
    log_d("task on core %i exiting", xPortGetCoreID());
    xTaskNotifyGive(osm->ownerTask);
    vTaskDelete(nullptr);
}

bool OpenStreetMap::startTileWorkerTasks()
{
    if (tasksStarted)
        return true;

    if (!jobQueue)
    {
        jobQueue = xQueueCreate(OSM_JOB_QUEUE_SIZE, sizeof(TileJob));
        if (!jobQueue)
        {
            log_e("Failed to create job queue!");
            return false;
        }
    }

    numberOfWorkers = OSM_FORCE_SINGLECORE ? 1 : ESP.getChipCores();
    for (int core = 0; core < numberOfWorkers; ++core)
    {
        if (!getPNGForCore(core))
        {
            log_e("Failed to initialize PNG decoder on core %d", core);
            return false;
        }
    }

    ownerTask = xTaskGetCurrentTaskHandle();
    for (int core = 0; core < numberOfWorkers; ++core)
    {
        if (!xTaskCreatePinnedToCore(tileFetcherTask,
                                     nullptr,
                                     OSM_TASK_STACKSIZE,
                                     this,
                                     OSM_TASK_PRIORITY,
                                     nullptr,
                                     OSM_FORCE_SINGLECORE ? OSM_SINGLECORE_NUMBER : core))
        {
            log_e("Failed to create tile fetcher task on core %d", core);
            return false;
        }
    }

    tasksStarted = true;

    log_i("Started %d tile worker task(s)", numberOfWorkers);
    return true;
}

uint16_t OpenStreetMap::tilesNeeded(uint16_t mapWidth, uint16_t mapHeight)
{
    const int tileSize = currentProvider->tileSize;
    int tilesX = (mapWidth + tileSize - 1) / tileSize + 1;
    int tilesY = (mapHeight + tileSize - 1) / tileSize + 1;
    return tilesX * tilesY;
}

bool OpenStreetMap::setTileProvider(int index)
{
    if (index < 0 || index >= OSM_TILEPROVIDERS)
    {
        log_e("invalid provider index");
        return false;
    }

    currentProvider = &tileProviders[index];
    freeTilesCache();
    log_i("provider changed to '%s'", currentProvider->name);
    return true;
}

void OpenStreetMap::invalidateTile(CachedTile *tile)
{
    if (!tile)
        return;
    tile->valid = false;
    tile->busy = false;
}
