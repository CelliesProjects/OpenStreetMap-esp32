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
