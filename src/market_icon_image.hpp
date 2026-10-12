// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "market_icon_core.hpp"
#include <vector>

namespace market {
struct IconPixels {
    int width=0,height=0;
    std::vector<std::uint8_t> rgba;
};
// PNG/JPEG only, at most 256 KiB and 256x256, decoded to straight-alpha RGBA.
// Output remains unchanged on failure. No filesystem or GL calls here.
bool decode_icon(const char *data,std::size_t size,IconPixels &out) noexcept;
}
