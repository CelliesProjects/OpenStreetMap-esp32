/*
    MIT License
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
#ifndef SCOPEDMUTEX_HPP_
#define SCOPEDMUTEX_HPP_

#include <Arduino.h>
#include <freertos/semphr.h>

class ScopedMutex
{
private:
    SemaphoreHandle_t &mutex;
    bool locked;

public:
    explicit ScopedMutex(SemaphoreHandle_t &m, TickType_t timeout = portMAX_DELAY)
        : mutex(m), locked(xSemaphoreTake(mutex, timeout)) {}

    ScopedMutex(const ScopedMutex &) = delete;
    ScopedMutex &operator=(const ScopedMutex &) = delete;

    ~ScopedMutex()
    {
        if (locked)
            xSemaphoreGive(mutex);
    }

    bool acquired() const { return locked; }
};

#endif // SCOPEDMUTEX_H
