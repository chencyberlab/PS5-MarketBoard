// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>

namespace market {
struct HttpResponse {
    int transport=0;
    int http_status=0;
    std::size_t used=0;
    int retry_after=0;
};
// Binary-safe, bounded response (cap includes a spare NUL for JSON callers).
// Redirects are disabled and all TLS checks stay enabled. An optional Demo
// key is sent as a header only to api.coingecko.com, never to image hosts.
HttpResponse get_https(const char *url,char *buffer,std::size_t cap,
                       const char *coingecko_demo_key=nullptr) noexcept;
}
