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

#include "MemoryBuffer.h"
#include <Arduino.h>

MemoryBuffer::MemoryBuffer(size_t size) : size_(size), buffer_(nullptr)
{
    if (size_ > 0)
    {
        buffer_ = static_cast<uint8_t *>(malloc(size_));
        if (buffer_ == nullptr)
        {
            log_e("Memory allocation failed!");
        }
    }
}

MemoryBuffer::~MemoryBuffer()
{
    if (buffer_ != nullptr)
    {
        free(buffer_);
        buffer_ = nullptr;
    }
}

uint8_t *MemoryBuffer::get()
{
    return buffer_;
}

size_t MemoryBuffer::size() const
{
    return size_;
}

bool MemoryBuffer::isAllocated()
{
    return buffer_ != nullptr;
}
