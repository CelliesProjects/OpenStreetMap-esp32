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
        log_i("Updated %i tiles in %lu ms - %i ms/tile", jobs.size(), millis() - startMS, (millis() - startMS) / jobs.size());
    }
}

void OpenStreetMap::makeJobList(const tileList &requiredTiles, std::vector<TileJob> &jobs, uint8_t zoom, TileBufferList &tilePointers)
{
    for (const auto &[x, y] : requiredTiles)
    {
        if (y < 0 || y >= (1 << zoom))
        {
            tilePointers.push_back(nullptr); // we need to keep 1:1 grid alignment with requiredTiles for composeMap
            continue;
        }

        const CachedTile *cachedTile = isTileCached(x, y, zoom);
        if (cachedTile)
        {
            tilePointers.push_back(cachedTile->buffer);
            continue;
        }

        // Check if this tile is already in the job list
        const auto job = std::find_if(jobs.begin(), jobs.end(), [&](const TileJob &job)
                                      { return job.x == x && job.y == static_cast<uint32_t>(y) && job.z == zoom; });
        if (job != jobs.end())
        {
            tilePointers.push_back(job->tile->buffer); // reuse buffer from already queued job
            continue;
        }

        CachedTile *tileToReplace = findUnusedTile(requiredTiles, zoom);
        if (!tileToReplace)
        {
            log_e("Cache error, no unused tile found, could not store tile %lu, %i, %u", x, y, zoom);
            tilePointers.push_back(nullptr); // again, keep 1:1 aligned
            continue;
        }

        tilePointers.push_back(tileToReplace->buffer);                      // store buffer for rendering
        jobs.push_back({x, static_cast<uint32_t>(y), zoom, tileToReplace}); // queue job
    }
}

void OpenStreetMap::runJobs(const std::vector<TileJob> &jobs)
{
    log_d("submitting %i jobs", (int)jobs.size());

    pendingJobs.store(jobs.size());
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
        const uint16_t *tile = tilePointers[tileIndex];
        if (!tile)
        {
            mapSprite.fillRect(drawX, drawY, currentProvider->tileSize, currentProvider->tileSize, OSM_BGCOLOR);
            continue;
        }
        mapSprite.pushImage(drawX, drawY, currentProvider->tileSize, currentProvider->tileSize, tile);
    }

    mapSprite.setTextColor(TFT_WHITE, OSM_BGCOLOR);
    mapSprite.drawRightString(currentProvider->attribution, mapSprite.width(), mapSprite.height() - 10, &DejaVu9Modded);
    mapSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    return true;
}

bool OpenStreetMap::fetchMap(LGFX_Sprite &mapSprite, double longitude, double latitude, uint8_t zoom)
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

    longitude = fmod(longitude + 180.0, 360.0) - 180.0;
    latitude = std::clamp(latitude, -90.0, 90.0);

    tileList requiredTiles;
    computeRequiredTiles(longitude, latitude, zoom, requiredTiles);
    if (tilesCache.capacity() < requiredTiles.size())
    {
        log_e("Caching error: Need %i cache slots, but only %i are provided", requiredTiles.size(), tilesCache.capacity());
        return false;
    }

    TileBufferList tilePointers;
    updateCache(requiredTiles, zoom, tilePointers);
    if (!composeMap(mapSprite, tilePointers))
    {
        log_e("Failed to compose map");
        return false;
    }
    return true;
}

bool OpenStreetMap::fillBuffer(WiFiClient *stream, MemoryBuffer &buffer, size_t contentSize, String &result)
{
    size_t readSize = 0;
    unsigned long lastReadTime = millis();
    while (readSize < contentSize)
    {
        const size_t availableData = stream->available();
        if (!availableData)
        {
            if (millis() - lastReadTime >= OSM_TILE_TIMEOUT_MS)
            {
                result = "Timeout: " + String(OSM_TILE_TIMEOUT_MS) + " ms";
                return false;
            }
            taskYIELD();
            continue;
        }

        const size_t remaining = contentSize - readSize;
        const size_t toRead = std::min(availableData, remaining);
        if (toRead == 0)
            continue;

        const int bytesRead = stream->readBytes(buffer.get() + readSize, toRead);
        if (bytesRead > 0)
        {
            readSize += bytesRead;
            lastReadTime = millis();
        }
        else
            taskYIELD();
    }
    return true;
}

std::unique_ptr<MemoryBuffer> OpenStreetMap::urlToBuffer(const char *url, String &result)
{
    HTTPClientRAII http;
    if (!http.begin(url))
    {
        result = "Failed to initialize HTTP client";
        return nullptr;
    }

    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        result = "HTTP Error: " + String(httpCode);
        return nullptr;
    }

    const size_t contentSize = http.getSize();
    if (contentSize < 1)
    {
        result = "Empty or chunked response";
        return nullptr;
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream)
    {
        result = "Failed to get HTTP stream";
        return nullptr;
    }

    auto buffer = std::make_unique<MemoryBuffer>(contentSize);
    if (!buffer->isAllocated())
    {
        result = "Failed to allocate buffer";
        return nullptr;
    }

    if (!fillBuffer(stream, *buffer, contentSize, result))
        return nullptr;

    return buffer;
}

void OpenStreetMap::PNGDraw(PNGDRAW *pDraw)
{
    uint16_t *destRow = currentInstance->currentTileBuffer + (pDraw->y * currentInstance->currentProvider->tileSize);
    getPNGCurrentCore()->getLineAsRGB565(pDraw, destRow, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
}

bool OpenStreetMap::fetchTile(ReusableTileFetcher &fetcher, CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result)
{

    String url = currentProvider->urlTemplate;
    url.replace("{x}", String(x));
    url.replace("{y}", String(y));
    url.replace("{z}", String(zoom));
    if (currentProvider->requiresApiKey && strstr(url.c_str(), "{apiKey}"))
        url.replace("{apiKey}", currentProvider->apiKey);

    const std::unique_ptr<MemoryBuffer> buffer = fetcher.fetchToBuffer(url, result);
    if (!buffer)
        return false;

    PNG *png = getPNGCurrentCore();
    const int16_t rc = png->openRAM(buffer->get(), buffer->size(), PNGDraw);
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
        result = "Decoding " + url + " failed with code: " + String(decodeResult);
        return false;
    }

    tile.x = x;
    tile.y = y;
    tile.z = zoom;
    return true;
}

void OpenStreetMap::tileFetcherTask(void *param)
{
    OpenStreetMap *osm = static_cast<OpenStreetMap *>(param);
    ReusableTileFetcher fetcher;
    while (true)
    {
        TileJob job;
        xQueueReceive(osm->jobQueue, &job, portMAX_DELAY);
        [[maybe_unused]] const unsigned long startMS = millis();

        if (job.z == 255)
            break;

        String result;
        if (!osm->fetchTile(fetcher, *job.tile, job.x, job.y, job.z, result))
        {
            const size_t tileByteCount = osm->currentProvider->tileSize * osm->currentProvider->tileSize * 2;
            memset(job.tile->buffer, 0, tileByteCount);
            log_e("Tile fetch failed: %s", result.c_str());
            job.tile->valid = false;
        }
        else
        {
            log_d("core %i fetched tile z=%u x=%lu, y=%lu in %lu ms", xPortGetCoreID(), job.z, job.x, job.y, millis() - startMS);
            job.tile->valid = true;
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
