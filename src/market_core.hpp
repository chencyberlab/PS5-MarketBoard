// PS5 MarketBoard: platform-independent market model, parsers and settings.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace market {
constexpr int kMaxPairs = 12;
constexpr int kHistory = 96;
constexpr int kIntervalCount = 5;
constexpr int kIntervals[kIntervalCount] = {5,15,30,60,120};
constexpr int kSymbolSize = 20;

enum class Provider: unsigned char { Binance, KuCoin, Coinbase, CoinGecko, None };
struct Quote {
    double price = 0.0;
    double change_24h = 0.0;
    bool has_change = false;
};
struct Entry {
    char symbol[kSymbolSize]{};
    Quote quote{};
    Provider provider = Provider::None;
    bool ready = false;
    bool fresh = false;
    long long updated_at = 0;
    int error_code = 0;
    double history[kHistory]{};
    long long history_at[kHistory]{};
    int history_len = 0;
    int history_next = 0;
};
struct Preferences {
    int interval_index = 1; // 15 seconds
    int count = 3;
    char pairs[kMaxPairs][kSymbolSize] = {"BTC/USDT", "ETH/USDT", "SOL/USDT"};
};

// Accepts BASE/USDT or BASE/USD; BASE is 2-10 ASCII letters/numbers.
bool normalize_pair(const char *input, char output[kSymbolSize]) noexcept;
bool add_pair(Preferences &prefs, const char *input) noexcept;
bool remove_pair(Preferences &prefs, int index) noexcept;
// Human-readable, bounded and versioned, never stores credentials.
bool serialize_prefs(const Preferences &prefs, char *buffer, std::size_t capacity) noexcept;
bool parse_prefs(const char *input, std::size_t size, Preferences &out) noexcept;
const char *provider_name(Provider source) noexcept;
// Parse a single provider's documented JSON response. No output mutation on failure.
bool parse_provider_price(Provider source, const char *json, std::size_t size,
                          const char *base, Quote &out) noexcept;
const char *coingecko_id(const char *base) noexcept;
}
