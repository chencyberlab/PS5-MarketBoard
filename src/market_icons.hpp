// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "market_icon_image.hpp"
#include <memory>

namespace market {
// Worker owns I/O/decoding; the render thread owns every GL texture.
// Immutable pixels remain alive while the renderer holds a shared pointer.
void start_icons(const Preferences &prefs) noexcept;
void set_icon_preferences(const Preferences &prefs) noexcept;
std::shared_ptr<const IconPixels> read_icon(const char *base) noexcept;
}
