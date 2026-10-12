// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "market_core.hpp"
namespace market {
struct FeedState {
    Preferences prefs{};
    Entry entries[kMaxPairs]{};
    bool worker_running = false;
    bool updating = false;
    long long last_cycle_at = 0;
    long long next_cycle_at = 0;
    long long last_success_at = 0;
    unsigned completed_cycles = 0;
    int last_http_status = 0;
    int last_transport_error = 0;
};
void start_market_updates(const Preferences &prefs) noexcept;
void set_market_preferences(const Preferences &prefs) noexcept;
void force_market_refresh() noexcept;
FeedState read_market_state() noexcept;
}
