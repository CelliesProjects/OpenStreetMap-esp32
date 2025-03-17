#include "MemoryBuffer.h"
#include <Arduino.h>

MemoryBuffer::MemoryBuffer(size_t size) : size_(size), buffer_(nullptr)
{
    if (size_ > 0)
    {
        buffer_ = static_cast<uint8_t*>(malloc(size_));
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
