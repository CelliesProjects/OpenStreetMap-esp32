#ifndef CACHEDTILE
#define CACHEDTILE

#include <Arduino.h>

struct CachedTile
{
    uint32_t x;
    uint32_t y;
    uint8_t z;
    bool valid;
    uint16_t *buffer;

    CachedTile() : valid(false), buffer(nullptr) {}

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