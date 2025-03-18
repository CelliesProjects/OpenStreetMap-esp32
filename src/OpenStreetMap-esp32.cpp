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
    {
        log_e("PNGDraw called with null buffer or instance!");
        return;
    }
    log_v("Decoding line: %d", pDraw->y);
    uint16_t *destRow = currentInstance->currentTileBuffer + (pDraw->y * 256);
    currentInstance->png.getLineAsRGB565(pDraw, destRow, PNG_RGB565_BIG_ENDIAN, 0x0000);
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
            tileX = tileX % worldTileWidth;
            if (tileX < 0)
                tileX += worldTileWidth;

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

bool OpenStreetMap::resizeTilesCache(uint8_t cacheSize)
{
    if (cacheSize == 0)
    {
        log_e("Invalid cache size: %d", cacheSize);
        return false;
    }

    if (tilesCache.size() == cacheSize)
        return true;

    freeTilesCache();
    tilesCache.resize(cacheSize);

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

void OpenStreetMap::updateCache(tileList &requiredTiles, uint8_t zoom)
{
    for (const auto &[x, y] : requiredTiles)
    {
        if (!isTileCached(x, y, zoom))
        {
            CachedTile *tileToReplace = findUnusedTile(requiredTiles, zoom);
            String result;
            if (!downloadAndDecodeTile(*tileToReplace, x, y, zoom, result))
                log_e("%s", result.c_str());
        }
    }
}

bool OpenStreetMap::composeMap(LGFX_Sprite &mapSprite, tileList &requiredTiles, uint8_t zoom)
{
    if (mapSprite.width() != mapWidth || mapSprite.height() != mapHeight)
    {
        mapSprite.deleteSprite();
        mapSprite.setPsram(true);
        mapSprite.setColorDepth(lgfx::rgb565_2Byte);
        mapSprite.createSprite(mapWidth, mapHeight);
        if (!mapSprite.getBuffer())
        {
            log_e("could not allocate");
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
                                   return tile.valid && tile.x == tileX && tile.y == tileY && tile.z == zoom;
                               });

        if (it != tilesCache.end())
            mapSprite.pushImage(drawX, drawY, OSM_TILESIZE, OSM_TILESIZE, it->buffer);
        else
            log_w("Tile (%d, %d) not found in cache", tileX, tileY);

        tileIndex++;
    }

    const char *attribution = " Map data from OpenStreetMap.org ";
    mapSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    mapSprite.drawRightString(attribution, mapSprite.width(), mapSprite.height() - 10, &DejaVu9);

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
            log_e("Could not allocate cache");
            return false;
        }
    }

    // normalize the coordinates
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

bool OpenStreetMap::downloadAndDecodeTile(CachedTile &tile, uint32_t x, uint32_t y, uint8_t zoom, String &result)
{
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

    log_v("Content size: %u bytes", contentSize);

    WiFiClient *stream = http.getStreamPtr();
    if (!stream)
    {
        http.end();
        result = "Failed to get HTTP stream.";
        return false;
    }

    MemoryBuffer buffer(contentSize);
    if (!buffer.isAllocated())
    {
        http.end();
        result = "Failed to allocate buffer.";
        return false;
    }

    constexpr unsigned long TIMEOUT_MS = 900;
    size_t readSize = 0;
    unsigned long lastReadTime = millis();

    while (readSize < contentSize)
    {
        int availableData = stream->available();
        if (availableData > 0)
        {
            int bytesRead = stream->readBytes(buffer.get() + readSize, availableData);
            readSize += bytesRead;
            log_d("Read %d bytes, total %d bytes", bytesRead, readSize);
            lastReadTime = millis();
        }
        else if (millis() - lastReadTime >= TIMEOUT_MS)
        {
            http.end();
            result = "Timeout: No data received within " + String(TIMEOUT_MS) + " ms";
            return false;
        }
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
    const int decodeResult = png.decode(0, 0);
    currentTileBuffer = nullptr;
    currentInstance = nullptr;

    if (decodeResult != PNG_SUCCESS)
    {
        result = "Decode failed with code: " + String(decodeResult);
        tile.valid = false;
        return false;
    }

    tile.x = x;
    tile.y = y;
    tile.z = zoom;
    tile.valid = true;

    result = "Downloaded and decoded: " + url;
    return true;
}

bool OpenStreetMap::saveMap(const char *filename, LGFX_Sprite &sprite, String &result, uint8_t sdPin)
{
    log_i("Saving map, this may take a while...");

    if (!sprite.getBuffer())
    {
        result = "No data in map!";
        return false;
    }

    if (!SD.begin(sdPin))
    {
        result = "SD Card mount failed!";
        return false;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file)
    {
        result = "Failed to open file!";
        SD.end();
        return false;
    }

    // BMP header (54 bytes)
    uint16_t bfType = 0x4D42;                                    // "BM"
    uint32_t bfSize = 54 + sprite.width() * sprite.height() * 3; // Header + pixel data (3 bytes per pixel for RGB888)
    uint16_t bfReserved = 0;
    uint32_t bfOffBits = 54; // Offset to pixel data

    uint32_t biSize = 40; // Info header size
    int32_t biWidth = sprite.width();
    int32_t biHeight = -sprite.height(); // Negative to flip vertically
    uint16_t biPlanes = 1;
    uint16_t biBitCount = 24; // RGB888 format
    uint32_t biCompression = 0;
    uint32_t biSizeImage = sprite.width() * sprite.height() * 3; // 3 bytes per pixel
    int32_t biXPelsPerMeter = 0;
    int32_t biYPelsPerMeter = 0;
    uint32_t biClrUsed = 0;
    uint32_t biClrImportant = 0;

    // Write BMP header
    file.write(reinterpret_cast<const uint8_t *>(&bfType), sizeof(bfType));
    file.write(reinterpret_cast<const uint8_t *>(&bfSize), sizeof(bfSize));
    file.write(reinterpret_cast<const uint8_t *>(&bfReserved), sizeof(bfReserved));
    file.write(reinterpret_cast<const uint8_t *>(&bfOffBits), sizeof(bfOffBits));

    file.write(reinterpret_cast<const uint8_t *>(&biSize), sizeof(biSize));
    file.write(reinterpret_cast<const uint8_t *>(&biWidth), sizeof(biWidth));
    file.write(reinterpret_cast<const uint8_t *>(&biHeight), sizeof(biHeight));
    file.write(reinterpret_cast<const uint8_t *>(&biPlanes), sizeof(biPlanes));
    file.write(reinterpret_cast<const uint8_t *>(&biBitCount), sizeof(biBitCount));
    file.write(reinterpret_cast<const uint8_t *>(&biCompression), sizeof(biCompression));
    file.write(reinterpret_cast<const uint8_t *>(&biSizeImage), sizeof(biSizeImage));
    file.write(reinterpret_cast<const uint8_t *>(&biXPelsPerMeter), sizeof(biXPelsPerMeter));
    file.write(reinterpret_cast<const uint8_t *>(&biYPelsPerMeter), sizeof(biYPelsPerMeter));
    file.write(reinterpret_cast<const uint8_t *>(&biClrUsed), sizeof(biClrUsed));
    file.write(reinterpret_cast<const uint8_t *>(&biClrImportant), sizeof(biClrImportant));

    for (int y = 0; y < sprite.height(); y++)
    {
        for (int x = 0; x < sprite.width(); x++)
        {
            uint16_t rgb565Color = sprite.readPixel(x, y); // Read pixel color (RGB565 format)
            uint8_t red5 = (rgb565Color >> 11) & 0x1F;
            uint8_t green6 = (rgb565Color >> 5) & 0x3F;
            uint8_t blue5 = rgb565Color & 0x1F;

            // Convert RGB565 to RGB888
            uint8_t red8 = (red5 * 255) / 31;
            uint8_t green8 = (green6 * 255) / 63;
            uint8_t blue8 = (blue5 * 255) / 31;

            file.write(blue8);
            file.write(green8);
            file.write(red8);
        }
    }

    file.close();
    SD.end();
    result = "Screenshot saved";
    return true;
}