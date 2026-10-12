#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$ROOT/.build"
${CXX:-g++} -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  "$ROOT/src/market_core.cpp" "$ROOT/tests/test_market_core.cpp" \
  -o "$ROOT/.build/market_test"
"$ROOT/.build/market_test"
