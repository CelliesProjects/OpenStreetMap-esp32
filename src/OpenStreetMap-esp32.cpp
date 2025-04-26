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

#include "OpenStreetMap-esp32.h"

OpenStreetMap::~OpenStreetMap()
{
    freeTilesCache();
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
    const int16_t targetOffsetX = (exactTileX - targetTileX) * OSM_TILESIZE;
    const int16_t targetOffsetY = (exactTileY - targetTileY) * OSM_TILESIZE;

    // Compute the offset for tiles covering the map area to keep the location centered
    const int16_t tilesOffsetX = mapWidth / 2 - targetOffsetX;
    const int16_t tilesOffsetY = mapHeight / 2 - targetOffsetY;

    // Compute number of colums required
    const float colsLeft = 1.0 * tilesOffsetX / OSM_TILESIZE;
    const float colsRight = float(mapWidth - (tilesOffsetX + OSM_TILESIZE)) / OSM_TILESIZE;
    numberOfColums = ceil(colsLeft) + 1 + ceil(colsRight);

    startOffsetX = tilesOffsetX - (ceil(colsLeft) * OSM_TILESIZE);

    // Compute number of rows required
    const float rowsTop = 1.0 * tilesOffsetY / OSM_TILESIZE;
    const float rowsBottom = float(mapHeight - (tilesOffsetY + OSM_TILESIZE)) / OSM_TILESIZE;
    const uint32_t numberOfRows = ceil(rowsTop) + 1 + ceil(rowsBottom);

    startOffsetY = tilesOffsetY - (ceil(rowsTop) * OSM_TILESIZE);

    log_v(" Need %i * %i tiles. First tile offset is %d,%d",
          numberOfColums, numberOfRows, startOffsetX, startOffsetY);

    startTileIndexX = targetTileX - ceil(colsLeft);
    startTileIndexY = targetTileY - ceil(rowsTop);

    log_v("top left tile indices: %d, %d", startTileIndexX, startTileIndexY);

    requiredTiles.clear();

    const int32_t worldTileWidth = 1 << zoom;

    for (int32_t y = 0; y < numberOfRows; ++y)
    {
        for (int32_t x = 0; x < numberOfColums; ++x)
        {
            int32_t tileX = startTileIndexX + x;
            int32_t tileY = startTileIndexY + y;

            // Apply modulo wrapping for tileX
            // see https://godbolt.org/z/96e1x7j7r
            tileX = (tileX % worldTileWidth + worldTileWidth) % worldTileWidth;

            requiredTiles.emplace_back(tileX, tileY);
        }
    }
}

bool OpenStreetMap::isTileCached(uint32_t x, uint32_t y, uint8_t z)
{
    for (const auto &tile : tilesCache)
        if (tile.x == x && tile.y == y && tile.z == z && tile.valid)
            return true;

    return false;
}

CachedTile *OpenStreetMap::findUnusedTile(const tileList &requiredTiles, uint8_t zoom)
{
    for (auto &tile : tilesCache)
    {
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
            return &tile;
    }

    return &tilesCache[0]; // Placeholder: replace with better eviction logic
}

void OpenStreetMap::freeTilesCache()
{
    for (auto &tile : tilesCache)
        tile.free();

    tilesCache.clear();
}

bool OpenStreetMap::resizeTilesCache(uint16_t numberOfTiles)
{
    if (tilesCache.size() == numberOfTiles)
        return true;

    if (!numberOfTiles)
    {
        log_e("Invalid cache size: %d", numberOfTiles);
        return false;
    }

    freeTilesCache();
    tilesCache.resize(numberOfTiles);

    for (auto &tile : tilesCache)
    {
        if (!tile.allocate())
        {
            log_e("Tile cache allocation failed!");
            freeTilesCache();
            return false;
        }
    }
    return true;
}

void OpenStreetMap::updateCache(const tileList &requiredTiles, uint8_t zoom)
{
    int jobsSubmitted = 0;

    for (const auto &[x, y] : requiredTiles)
    {
        if (isTileCached(x, y, zoom) || y < 0 || y >= (1 << zoom))
            continue;

        CachedTile *tileToReplace = findUnusedTile(requiredTiles, zoom);
        if (!tileToReplace)
            continue; // Should never happen if cache sizing is correct

        TileJob job = {x, static_cast<uint32_t>(y), zoom, tileToReplace};
        if (xQueueSend(jobQueue, &job, portMAX_DELAY) == pdPASS)
            jobsSubmitted++;
        else
            log_e("Failed to enqueue TileJob");
    }

    if (jobsSubmitted > 0)
    {
        // Set wait group counter
        pendingJobs.store(jobsSubmitted); // Assume pendingJobs is an std::atomic<int>

        // Wait for all jobs to finish
        while (pendingJobs.load() > 0)
            delay(1); // Sleep briefly, avoid tight busy loop
    }
}

bool OpenStreetMap::composeMap(LGFX_Sprite &mapSprite, const tileList &requiredTiles, uint8_t zoom)
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

    int tileIndex = 0;
    for (const auto &[tileX, tileY] : requiredTiles)
    {
        if (tileY < 0 || tileY >= (1 << zoom))
        {
            tileIndex++;
            continue;
        }

        int drawX = startOffsetX + (tileIndex % numberOfColums) * OSM_TILESIZE;
        int drawY = startOffsetY + (tileIndex / numberOfColums) * OSM_TILESIZE;

        auto it = std::find_if(tilesCache.begin(), tilesCache.end(),
                               [&](const CachedTile &tile)
                               {
                                   return tile.x == tileX && tile.y == tileY && tile.z == zoom && tile.valid;
                               });

        if (it != tilesCache.end())
            mapSprite.pushImage(drawX, drawY, OSM_TILESIZE, OSM_TILESIZE, it->buffer);
        else
            log_w("Tile (z=%d, x=%d, y=%d) not found in cache", zoom, tileX, tileY);

        tileIndex++;
    }

    constexpr uint32_t LESS_INTRUSIVE_MS = 15 * 60 * 1000;
    static unsigned long initTime = millis();
    if (millis() - initTime < LESS_INTRUSIVE_MS)
        mapSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    else
        mapSprite.setTextColor(TFT_BLACK);
    mapSprite.drawRightString(" Map data from OpenStreetMap.org ",
                              mapSprite.width(), mapSprite.height() - 10, &DejaVu9);
    mapSprite.setTextColor(TFT_WHITE, TFT_BLACK);

    return true;
}

bool OpenStreetMap::fetchMap(LGFX_Sprite &mapSprite, double longitude, double latitude, uint8_t zoom)
{
    startTileWorkersIfNeeded();

    if (!zoom || zoom > OSM_MAX_ZOOM)
    {
        log_e("Invalid zoom level: %d", zoom);
        return false;
    }

    if (!mapWidth || !mapHeight)
    {
        log_e("Invalid map dimension");
        return false;
    }

    if (!tilesCache.capacity())
    {
        log_w("Cache not initialized, setting up a default cache...");
        if (!resizeTilesCache(OSM_DEFAULT_CACHE_ITEMS))
        {
            log_e("Could not allocate tile cache");
            return false;
        }
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

    updateCache(requiredTiles, zoom);

    if (!composeMap(mapSprite, requiredTiles, zoom))
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
            vTaskDelay(pdMS_TO_TICKS(1));
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
            vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}

std::optional<std::unique_ptr<MemoryBuffer>> OpenStreetMap::urlToBuffer(const char *url, String &result)
{
    HTTPClientRAII http;
    if (!http.begin(url))
    {
        result = "Failed to initialize HTTP client";
        return std::nullopt;
    }

    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        result = "HTTP Error: " + String(httpCode);
        return std::nullopt;
    }

    const size_t contentSize = http.getSize();
    if (contentSize < 1)
    {
        result = "Empty or chunked response";
        return std::nullopt;
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream)
    {
        result = "Failed to get HTTP stream";
        return std::nullopt;
    }

    auto buffer = std::make_unique<MemoryBuffer>(contentSize);
    if (!buffer->isAllocated())
    {
        result = "Failed to allocate buffer";
        return std::nullopt;
    }

    if (!fillBuffer(stream, *buffer, contentSize, result))
        return std::nullopt;

    return buffer;
}

thread_local OpenStreetMap *OpenStreetMap::currentInstance = nullptr;

void OpenStreetMap::PNGDraw(PNGDRAW *pDraw)
{
    if (!currentInstance || !currentInstance->currentTileBuffer)
        return;

    uint16_t *destRow = currentInstance->currentTileBuffer + (pDraw->y * OSM_TILESIZE);
    currentInstance->png.getLineAsRGB565(pDraw, destRow, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
}

bool OpenStreetMap::fetchTile(CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result)
{
    const uint32_t worldTileWidth = 1 << zoom;
    if (x >= worldTileWidth || y >= worldTileWidth)
    {
        result = "Out of range tile coordinates";
        return false;
    }

    static char url[64];
    snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%u/%u/%u.png",
             static_cast<unsigned int>(zoom),
             static_cast<unsigned int>(x),
             static_cast<unsigned int>(y));

    int decodeResult;
    {
        auto buffer = urlToBuffer(url, result);
        if (!buffer)
            return false;

        const int16_t rc = png.openRAM(buffer.value()->get(), buffer.value()->size(), PNGDraw);
        if (rc != PNG_SUCCESS)
        {
            result = "PNG Decoder Error: " + String(rc);
            return false;
        }

        if (png.getWidth() != OSM_TILESIZE || png.getHeight() != OSM_TILESIZE)
        {
            result = "Unexpected tile size: w=" + String(png.getWidth()) + " h=" + String(png.getHeight());
            return false;
        }

        currentInstance = this;
        currentTileBuffer = tile.buffer;
        decodeResult = png.decode(0, PNG_FAST_PALETTE);
        currentTileBuffer = nullptr;
        currentInstance = nullptr;
    }

    if (decodeResult != PNG_SUCCESS)
    {
        result = "Decoding " + String(url) + " failed with code: " + String(decodeResult);
        tile.valid = false;
        return false;
    }

    tile.x = x;
    tile.y = y;
    tile.z = zoom;
    tile.valid = true;
    return true;
}

void OpenStreetMap::decrementActiveJobs()
{
    if (--pendingJobs == 0)
    {
        // Maybe notify main thread that fetching is done
        xSemaphoreGive(doneSemaphore);
    }
}

void OpenStreetMap::tileFetcherTask(void *param)
{
    OpenStreetMap *osm = static_cast<OpenStreetMap *>(param);

    while (true)
    {
        TileJob job;
        if (xQueueReceive(osm->jobQueue, &job, portMAX_DELAY) == pdTRUE)
        {
            // Validate input, double check in case of race
            if (!job.tile)
                continue;

            {
                ScopedMutex lock(job.tile->mutex); // Lock the tile we're working on

                // Check again that the tile is still needed
                if (job.tile->valid &&
                    job.tile->x == job.x &&
                    job.tile->y == job.y &&
                    job.tile->z == job.z)
                {
                    // Already done, no work needed
                    continue;
                }

                // Fetch and decode the tile
                String result;
                osm->fetchTile(*job.tile, job.x, job.y, job.z, result);
                log_v("Tile fetch result: %s", result.c_str());
            }

            // Decrement the active jobs counter
            osm->decrementActiveJobs();
        }
    }
}

void OpenStreetMap::startTileWorkersIfNeeded()
{
    if (tasksStarted)
        return;

    tasksStarted = true;

    // Create worker task(s)
    xTaskCreatePinnedToCore(tileFetcherTask, "TileWorker0", 4096, this, 1, nullptr, 0);
    xTaskCreatePinnedToCore(tileFetcherTask, "TileWorker1", 4096, this, 1, nullptr, 1);
}