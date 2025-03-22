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

void OpenStreetMap::setResolution(uint16_t w, uint16_t h)
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

OpenStreetMap *OpenStreetMap::currentInstance = nullptr;

void OpenStreetMap::PNGDraw(PNGDRAW *pDraw)
{
    if (!currentInstance || !currentInstance->currentTileBuffer)
        return;

    uint16_t *destRow = currentInstance->currentTileBuffer + (pDraw->y * 256);
    currentInstance->png.getLineAsRGB565(pDraw, destRow, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
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

bool OpenStreetMap::resizeTilesCache(uint8_t numberOfTiles)
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
    for (const auto &[x, y] : requiredTiles)
    {
        if (!isTileCached(x, y, zoom))
        {
            CachedTile *tileToReplace = findUnusedTile(requiredTiles, zoom);
            String result;
            if (!downloadAndDecodeTile(*tileToReplace, x, y, zoom, result))
                log_e("%s", result.c_str());
            else
                log_i("%s", result.c_str());
        }
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

    mapSprite.drawRightString(" Map data from OpenStreetMap.org ",
                              mapSprite.width(), mapSprite.height() - 10, &DejaVu9);

    return true;
}

bool OpenStreetMap::fetchMap(LGFX_Sprite &mapSprite, double longitude, double latitude, uint8_t zoom)
{
    if (!zoom || zoom > 18)
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

    // normalize the coordinates
    longitude = fmod(longitude + 180.0, 360.0) - 180.0;
    latitude = std::clamp(latitude, -90.0, 90.0);

    tileList requiredTiles;
    computeRequiredTiles(longitude, latitude, zoom, requiredTiles);

#define SHOW_REQUIRED_TILES false
#if defined(SHOW_REQUIRED_TILES) && (SHOW_REQUIRED_TILES == true)
    log_i("Required Tiles:");
    for (size_t i = 0; i < requiredTiles.size(); ++i)
    {
        log_i("    Tile [%zu]: X=%d, Y=%d", i, requiredTiles[i].first, requiredTiles[i].second);
    }
#endif

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

bool OpenStreetMap::readTileDataToBuffer(WiFiClient *stream, MemoryBuffer &buffer, size_t contentSize, String &result)
{
    size_t readSize = 0;
    unsigned long lastReadTime = millis();
    while (readSize < contentSize)
    {
        int availableData = stream->available();
        if (availableData > 0)
        {
            int bytesRead = stream->readBytes(buffer.get() + readSize, availableData);
            readSize += bytesRead;
            lastReadTime = millis();
        }
        else if (millis() - lastReadTime >= OSM_TILE_TIMEOUT_MS)
        {
            result = "Timeout: No data received within " + String(OSM_TILE_TIMEOUT_MS) + " ms";
            return false;
        }
    }
    return true;
}

bool OpenStreetMap::downloadAndDecodeTile(CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result)
{
    const uint32_t worldTileWidth = 1 << zoom;
    if (x >= worldTileWidth || y >= worldTileWidth)
    {
        result = "Out of range tile coordinates";
        return false;
    }

    const String url = "https://tile.openstreetmap.org/" + String(zoom) + "/" + String(x) + "/" + String(y) + ".png";

    HTTPClient http;
    http.setUserAgent("OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)");
    if (!http.begin(url))
    {
        result = "Failed to initialize HTTP client";
        return false;
    }

    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        http.end();

        if (httpCode == HTTP_CODE_NOT_FOUND)
        {
            result = "HTTP Error 404 - not found tile " + String(x) + "," + String(y) + "," + String(zoom);
            return false;
        }

        result = "HTTP Error: " + String(httpCode);
        return false;
    }

    const size_t contentSize = http.getSize();
    if (contentSize < 1)
    {
        http.end();
        result = "Empty or chunked response";
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream)
    {
        http.end();
        result = "Failed to get HTTP stream";
        return false;
    }

    MemoryBuffer buffer(contentSize);
    if (!buffer.isAllocated())
    {
        http.end();
        result = "Failed to allocate buffer";
        return false;
    }

    if (!readTileDataToBuffer(stream, buffer, contentSize, result))
    {
        http.end();
        log_e("%s", result.c_str());
        return false;
    }

    http.end();

    const int16_t rc = png.openRAM(buffer.get(), contentSize, PNGDraw);
    if (rc != PNG_SUCCESS)
    {
        result = "PNG Decoder Error: " + String(rc);
        return false;
    }

    currentInstance = this;
    currentTileBuffer = tile.buffer;
    const int decodeResult = png.decode(0, PNG_FAST_PALETTE);
    currentTileBuffer = nullptr;
    currentInstance = nullptr;

    if (decodeResult != PNG_SUCCESS)
    {
        result = "Decoding " + url + " failed with code: " + String(decodeResult);
        tile.valid = false;
        return false;
    }

    tile.x = x;
    tile.y = y;
    tile.z = zoom;
    tile.valid = true;

    result = "Added: " + url;
    return true;
}

bool OpenStreetMap::writeHeader(const LGFX_Sprite &map, File &file)
{
    // BMP Header (54 bytes)
    uint16_t bfType = 0x4D42;                              // "BM"
    uint32_t biSizeImage = map.width() * map.height() * 3; // 3 bytes per pixel (RGB888)
    uint32_t bfSize = 54 + biSizeImage;                    // Total file size
    uint32_t bfOffBits = 54;                               // Offset to pixel data

    uint32_t biSize = 40; // Info header size
    int32_t biWidth = map.width();
    int32_t biHeight = -map.height(); // Negative to store in top-down order
    uint16_t biPlanes = 1;
    uint16_t biBitCount = 24; // RGB888 format
    uint32_t biCompression = 0;
    int32_t biXPelsPerMeter = 0;
    int32_t biYPelsPerMeter = 0;
    uint32_t biClrUsed = 0;
    uint32_t biClrImportant = 0;

    auto writeLE = [&](uint32_t value, uint8_t size) -> bool
    {
        for (uint8_t i = 0; i < size; i++)
        {
            if (file.write(static_cast<uint8_t>(value >> (8 * i))) != 1)
            {
                return false;
            }
        }
        return true;
    };

    bool success = true;

    if (!(success &= writeLE(bfType, 2)))
        return false;
    if (!(success &= writeLE(bfSize, 4)))
        return false;
    if (!(success &= writeLE(0, 2)))
        return false; // bfReserved
    if (!(success &= writeLE(0, 2)))
        return false;
    if (!(success &= writeLE(bfOffBits, 4)))
        return false;
    if (!(success &= writeLE(biSize, 4)))
        return false;
    if (!(success &= writeLE(biWidth, 4)))
        return false;
    if (!(success &= writeLE(biHeight, 4)))
        return false;
    if (!(success &= writeLE(biPlanes, 2)))
        return false;
    if (!(success &= writeLE(biBitCount, 2)))
        return false;
    if (!(success &= writeLE(biCompression, 4)))
        return false;
    if (!(success &= writeLE(biSizeImage, 4)))
        return false;
    if (!(success &= writeLE(biXPelsPerMeter, 4)))
        return false;
    if (!(success &= writeLE(biYPelsPerMeter, 4)))
        return false;
    if (!(success &= writeLE(biClrUsed, 4)))
        return false;
    if (!(success &= writeLE(biClrImportant, 4)))
        return false;

    return success;
}

bool OpenStreetMap::writeMap(LGFX_Sprite &map, File &file, MemoryBuffer &buffer)
{
    uint8_t *buf = buffer.get();
    const size_t size = buffer.size();
    for (uint16_t y = 0; y < map.height(); y++)
    {
        for (uint16_t x = 0; x < map.width(); x++)
        {
            uint16_t rgb565Color = map.readPixel(x, y);
            uint8_t red8 = ((rgb565Color >> 11) & 0x1F) * 255 / 31;
            uint8_t green8 = ((rgb565Color >> 5) & 0x3F) * 255 / 63;
            uint8_t blue8 = (rgb565Color & 0x1F) * 255 / 31;

            buf[x * 3] = blue8;
            buf[x * 3 + 1] = green8;
            buf[x * 3 + 2] = red8;
        }
        if (file.write(buf, size) != size)
            return false;
    }
    return true;
}

bool OpenStreetMap::saveMap(const char *filename, LGFX_Sprite &map, String &result, uint8_t sdPin, uint32_t frequency)
{
    log_i("Saving map as %s", filename);

    if (!map.getBuffer())
    {
        result = "No data in map";
        return false;
    }

    MemoryBuffer rowBuffer(map.width() * 3);
    if (!rowBuffer.isAllocated())
    {
        result = "Row buffer allocation failed";
        return false;
    }

    if (!SD.begin(sdPin, SPI, frequency))
    {
        result = "SD Card mount failed";
        return false;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file)
    {
        result = "Failed to open file";
        SD.end();
        return false;
    }

    if (!writeHeader(map, file))
    {
        result = "Failed to write bmp header";
        file.close();
        SD.end();
        return false;
    }

    if (!writeMap(map, file, rowBuffer))
    {
        result = "Failed to write map data";
        file.close();
        SD.end();
        return false;
    }

    file.close();
    SD.end();
    result = "Map saved as " + String(filename);
    return true;
}
