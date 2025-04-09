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

#ifndef MEMORYBUFFER_H
#define MEMORYBUFFER_H

#include <Arduino.h>
#include <memory>

/**
 * @class MemoryBuffer
 * @brief A class that handles memory allocation and deallocation for a buffer.
 *
 * This class provides an RAII approach to manage a dynamically allocated buffer. It ensures that memory is
 * allocated during object creation and automatically freed when the object goes out of scope.
 *
 * @note It is recommended to use the `MemoryBuffer` class when dealing with dynamic memory allocation,
 *       to avoid memory leaks and ensure proper memory management.
 *
 * Example use:
 * ```cpp
 * {
 *     MemoryBuffer buffer(512);
 *     if (buffer.isAllocated()) { // Check if allocated!
 *         // Access buffer here...
 *     } else {
 *         // Handle error (e.g., log error, retry later)
 *     }
 * } // buffer automatically freed
 *
 * ```
 */
class MemoryBuffer
{
public:
    /**
     * @brief Constructs a `MemoryBuffer` object and allocates memory of the specified size.
     *
     * The constructor allocates memory of the specified size for the buffer. If allocation fails,
     * the buffer will not be valid.
     *
     * @param size The size of the buffer in bytes.
     *
     * @example
     * // Example usage of the constructor
     * MemoryBuffer buffer(512);  // Allocates a buffer of 512 bytes
     */
    explicit MemoryBuffer(size_t size);

    /**
     * @brief Returns a pointer to the allocated memory buffer.
     *
     * @return A pointer to the allocated memory, or `nullptr` if memory allocation failed.
     */
    uint8_t *get();

    /**
     * @brief Returns the size of the allocated buffer.
     *
     * @return The size of the allocated buffer in bytes.
     */
    size_t size() const;

    /**
     * @brief Checks whether memory allocation was successful.
     *
     * @return `true` if memory was successfully allocated, `false` if the buffer is `nullptr`.
     */
    bool isAllocated();

private:
    size_t size_;
    std::unique_ptr<uint8_t[]> buffer_;
};

#endif // MEMORYBUFFER_H
