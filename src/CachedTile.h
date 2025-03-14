#ifndef CACHEDTILE
#define CACHEDTILE

#include <Arduino.h>

struct CachedTile
{
    uint16_t *buffer;
    int z, x, y;
    bool valid;

    CachedTile() : buffer(nullptr), valid(false) {}

    bool allocate()
    {
        buffer = (uint16_t *)heap_caps_malloc(256 * 256 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        return buffer != nullptr;
    }

    void free()
    {
        if (buffer)
        {
            heap_caps_free(buffer);
            buffer = nullptr;
        }
    }
};

#endif