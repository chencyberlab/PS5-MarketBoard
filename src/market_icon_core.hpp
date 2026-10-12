// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "market_core.hpp"
#include <cstddef>
#include <cstdint>

namespace market {
constexpr int kIconBaseSize=11;
constexpr std::size_t kIconResponseMax=256*1024;
constexpr int kIconMaxDimension=256;
constexpr int kIconCacheEntries=64;
constexpr char kIconDataRoot[]="/download0/marketboard";
constexpr char kIconCacheRoot[]="/download0/marketboard/icons";
constexpr char kIconAssetRoot[]="/app0/assets/crypto";

bool icon_base(const char *pair,char out[kIconBaseSize]) noexcept;
bool valid_icon_base(const char *base) noexcept;
bool bundled_icon(const char *base) noexcept;
bool icon_path(const char *root,const char *base,const char *extension,
               char *out,std::size_t capacity) noexcept;
std::uint32_t icon_color(const char *base) noexcept;
// Exact symbols only. Known assets must match their explicit CoinGecko ID;
// unfamiliar symbols must have exactly one match in the coins array.
// Rejects malformed JSON, unsafe image hosts, and ambiguous matches.
bool parse_icon_search(const char *json,std::size_t size,const char *base,
                       char *url,std::size_t capacity) noexcept;
}
