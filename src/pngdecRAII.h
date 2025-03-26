#ifndef PNGDEC_RAII_H
#define PNGDEC_RAII_H

#include <PNGdec.h>

class PNGDecoderRAII
{
public:
    explicit PNGDecoderRAII(PNG_DRAW_CALLBACK *drawCallback)
        : callback(drawCallback), isOpen(false) {}

    PNGDecoderRAII(const PNGDecoderRAII &) = delete;
    PNGDecoderRAII &operator=(const PNGDecoderRAII &) = delete;

    ~PNGDecoderRAII()
    {
        close();
    }

    bool open(uint8_t *pngData, size_t dataSize)
    {
        if (isOpen)
            close();

        int result = png.openRAM(pngData, dataSize, callback);
        isOpen = (result == PNG_SUCCESS);
        return isOpen;
    }

    int decode(void *pPriv = NULL, uint8_t options = 0)
    {
        if (!isOpen)
            return PNG_INVALID_PARAMETER;

        return png.decode(pPriv, options);
    }

    int getWidth() const { return png.getWidth(); }
    int getHeight() const { return png.getHeight(); }

    void close()
    {
        if (isOpen)
        {
            png.close();
            isOpen = false;
        }
    }

private:
    PNGdec png;
    PNG_DRAW_CALLBACK *callback;
    bool isOpen;
};

#endif // PNGDEC_RAII_H
