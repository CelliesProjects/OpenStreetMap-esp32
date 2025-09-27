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

#ifndef TILEPROVIDER_HPP_
#define TILEPROVIDER_HPP_

struct TileProvider
{
    const char *name;
    const char *urlTemplate;
    const char *attribution;
    bool requiresApiKey;
    const char *apiKey;
    int maxZoom;
    int minZoom;
    int tileSize;
};

const TileProvider osmStandard = {
    "OSM Standard",
    "https://tile.openstreetmap.org/%d/%d/%d.png",
    "© OpenStreetMap contributors",
    false,
    "",
    19, 0, 256};

const TileProvider ThunderTransportDark256 = {
    "Thunderforest Transport Dark 256px",
    "https://tile.thunderforest.com/transport-dark/%d/%d/%d.png?apikey=%s",
    "© Thunderforest, OpenStreetMap contributors",
    true,
    "YOUR_THUNDERFOREST_KEY",
    22, 0, 256};

const TileProvider ThunderForestCycle512 = {
    "Thunderforest Cycle 512px",
    "https://tile.thunderforest.com/transport-dark/%d/%d/%d@2x.png?apikey=%s",
    "© Thunderforest, OpenStreetMap contributors",
    true,
    "YOUR_THUNDERFOREST_KEY",
    22, 0, 512};

const TileProvider ThunderForestCycle256 = {
    "Thunderforest Cycle 256px",
    "https://tile.thunderforest.com/cycle/%d/%d/%d.png?apikey=%s",
    "© Thunderforest, OpenStreetMap contributors",
    true,
    "YOUR_THUNDERFOREST_KEY",
    22, 0, 256};

// Replace 'YOUR_THUNDERFOREST_KEY' above with a -free- Thunderforest API key
// and uncomment one of the following line to use Thunderforest tiles

// const TileProvider tileProviders[] = {osmStandard, ThunderTransportDark256, ThunderForestCycle512, ThunderForestCycle256};
// const TileProvider tileProviders[] = {ThunderTransportDark256};
// const TileProvider tileProviders[] = {ThunderForestCycle512};
// const TileProvider tileProviders[] = {ThunderForestCycle256};

// If one of the above definitions is used, the following line should be commented out
const TileProvider tileProviders[] = {osmStandard};

constexpr int OSM_TILEPROVIDERS = sizeof(tileProviders) / sizeof(TileProvider);

static_assert(OSM_TILEPROVIDERS > 0, "No TileProvider configured");

#endif
