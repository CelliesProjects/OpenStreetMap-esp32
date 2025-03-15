#include "OpenStreetMap-esp32.h"

OpenStreetMap::~OpenStreetMap()
{
    freeTilesCache();
}

void OpenStreetMap::setResolution(int w, int h)
{
    mapWidth = w;
    mapHeight = h;
}

double OpenStreetMap::lon2tile(double lon, int zoom)
{
    return (lon + 180.0) / 360.0 * (1 << zoom);
}

double OpenStreetMap::lat2tile(double lat, int zoom)
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

void OpenStreetMap::computeRequiredTiles(double longitude, double latitude, int zoom, std::vector<std::pair<int, int>> &requiredTiles)
{
    // Compute exact tile coordinates
    const double exactTileX = lon2tile(longitude, zoom);
    const double exactTileY = lat2tile(latitude, zoom);

    // Determine the integer tile indices
    const int targetTileX = static_cast<int>(exactTileX);
    const int targetTileY = static_cast<int>(exactTileY);

    // Compute the offset inside the tile for the given coordinates
    const int targetOffsetX = (exactTileX - targetTileX) * TILE_SIZE;
    const int targetOffsetY = (exactTileY - targetTileY) * TILE_SIZE;

    // Compute the offset for tiles covering the map area to keep the location centered
    const int tilesOffsetX = mapWidth / 2 - targetOffsetX;
    const int tilesOffsetY = mapHeight / 2 - targetOffsetY;

    // Compute number of colums required
    const float colsLeft = 1.0 * tilesOffsetX / TILE_SIZE;
    const float colsRight = float(mapWidth - (tilesOffsetX + TILE_SIZE)) / TILE_SIZE;
    const int numberOfColums = ceil(colsLeft) + 1 + ceil(colsRight);

    startOffsetX = tilesOffsetX - (ceil(colsLeft) * TILE_SIZE);

    // Compute number of rows required
    const float rowsTop = 1.0 * tilesOffsetY / TILE_SIZE;
    const float rowsBottom = float(mapHeight - (tilesOffsetY + TILE_SIZE)) / TILE_SIZE;
    const int numberOfRows = ceil(rowsTop) + 1 + ceil(rowsBottom);

    startOffsetY = tilesOffsetY - (ceil(rowsTop) * TILE_SIZE);

    log_v(" Need %i * %i tiles. First tile offset is %i,%i",
          numberOfColums, numberOfRows, startOffsetX, startOffsetY);

    startTileIndexX = targetTileX - ceil(colsLeft);
    startTileIndexY = targetTileY - ceil(rowsTop);

    log_v("top left tile indices: %i, %i", startTileIndexX, startTileIndexY);

    requiredTiles.clear();
    for (int y = 0; y < numberOfRows; ++y)
    {
        for (int x = 0; x < numberOfColums; ++x)
            requiredTiles.emplace_back(startTileIndexX + x, startTileIndexY + y);
    }
}

bool OpenStreetMap::isTileCached(int x, int y, int z)
{
    for (const auto &tile : tilesCache)
        if (tile.valid && tile.x == x && tile.y == y && tile.z == z)
            return true;

    return false;
}

CachedTile *OpenStreetMap::findUnusedTile(const std::vector<std::pair<int, int>> &requiredTiles, int zoom)
{
    for (auto &tile : tilesCache)
    {
        // If a tile is valid but not required in the current frame, we can replace it
        bool needed = false;
        for (const auto &[x, y] : requiredTiles)
        {
            if (tile.valid && tile.x == x && tile.y == y && tile.z == zoom)
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

bool OpenStreetMap::resizeTilesCache(int cacheSize)
{
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

bool OpenStreetMap::fetchMap(LGFX_Sprite &mapSprite, double longitude, double latitude, int zoom)
{
    if (!tilesCache.capacity())
    {
        log_w("Cache not initialized, setting up a default cache...");
        resizeTilesCache(10);
    }

    std::vector<std::pair<int, int>> requiredTiles;
    computeRequiredTiles(longitude, latitude, zoom, requiredTiles);

    if (tilesCache.capacity() < requiredTiles.size())
    {
        log_e("Caching error: Need %i cache slots, but only %i are provided", requiredTiles.size(), tilesCache.capacity());
        return false;
    }

    for (const auto &[x, y] : requiredTiles)
    {
        if (!isTileCached(x, y, zoom))
        {
            CachedTile *tileToReplace = findUnusedTile(requiredTiles, zoom);
            String result;
            const bool success = downloadAndDecodeTile(*tileToReplace, x, y, zoom, result);
            if (!success)
                log_e("%s", result.c_str());

            log_i("%s", result.c_str());
        }
    }

    if (mapSprite.width() != mapWidth || mapSprite.height() != mapHeight)
    {
        mapSprite.deleteSprite();
        mapSprite.setPsram(true);
        mapSprite.setColorDepth(lgfx::rgb565_2Byte);
        mapSprite.createSprite(mapWidth, mapHeight);
    }

    if (mapSprite.getBuffer() == nullptr)
    {
        log_e("could not allocate");
        return false;
    }

    for (const auto &[tileX, tileY] : requiredTiles)
    {
        int drawX = startOffsetX + (tileX - startTileIndexX) * TILE_SIZE;
        int drawY = startOffsetY + (tileY - startTileIndexY) * TILE_SIZE;

        auto it = std::find_if(tilesCache.begin(), tilesCache.end(),
                               [&](const CachedTile &tile)
                               {
                                   return tile.valid && tile.x == tileX && tile.y == tileY && tile.z == zoom;
                               });

        if (it != tilesCache.end())
            mapSprite.pushImage(drawX, drawY, TILE_SIZE, TILE_SIZE, it->buffer);

        else
            log_w("Tile (%d, %d) not found in cache", tileX, tileY);
    }

    const char *attribution = " Map data from OpenStreetMap.org ";
    mapSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    mapSprite.drawRightString(attribution, mapSprite.width(), mapSprite.height() - 10, &DejaVu9);    

    return true;
}

bool OpenStreetMap::downloadAndDecodeTile(CachedTile &tile, int x, int y, int zoom, String &result)
{
    String url = "https://tile.openstreetmap.org/" + String(zoom) + "/" + String(x) + "/" + String(y) + ".png";

    HTTPClient http;
    http.setUserAgent("OpenStreetMap-esp32/1.0 (+https://github.com/CelliesProjects/OpenStreetMap-esp32)");
    if (!http.begin(url))
    {
        result = "Failed to initialize HTTP client";
        return false;
    }

    int httpCode = http.GET();
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

    constexpr unsigned long TIMEOUT_MS = 500;
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

bool OpenStreetMap::saveMap(const char *filename, LGFX_Sprite &sprite, String &result)
{
    log_i("Saving map, this may take a while...");

    if (!sprite.getBuffer())
    {
        result = "No data in map!";
        return false;
    }

    if (!SD.begin(SS))
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
    uint16_t bfType = 0x4D42;                                      // "BM"
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
    file.write((uint8_t *)&bfType, 2);
    file.write((uint8_t *)&bfSize, 4);
    file.write((uint8_t *)&bfReserved, 2);
    file.write((uint8_t *)&bfReserved, 2);
    file.write((uint8_t *)&bfOffBits, 4);

    file.write((uint8_t *)&biSize, 4);
    file.write((uint8_t *)&biWidth, 4);
    file.write((uint8_t *)&biHeight, 4);
    file.write((uint8_t *)&biPlanes, 2);
    file.write((uint8_t *)&biBitCount, 2);
    file.write((uint8_t *)&biCompression, 4);
    file.write((uint8_t *)&biSizeImage, 4);
    file.write((uint8_t *)&biXPelsPerMeter, 4);
    file.write((uint8_t *)&biYPelsPerMeter, 4);
    file.write((uint8_t *)&biClrUsed, 4);
    file.write((uint8_t *)&biClrImportant, 4);

    for (int y = 0; y < sprite.height(); y++)
    {
        for (int x = 0; x < sprite.width(); x++)
        {
            uint16_t color = sprite.readPixel(x, y); // Read pixel color (RGB565 format)
            uint8_t r = (color >> 11) & 0x1F;
            uint8_t g = (color >> 5) & 0x3F;
            uint8_t b = color & 0x1F;

            // Convert RGB565 to RGB888
            r = (r * 255) / 31;
            g = (g * 255) / 63;
            b = (b * 255) / 31;

            file.write(b);
            file.write(g);
            file.write(r);
        }
    }

    file.close();
    SD.end();
    result = "Screenshot saved";
    return true;
}
