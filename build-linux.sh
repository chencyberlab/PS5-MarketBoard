#!/usr/bin/env bash
# Fetch the GPL-licensed PS5 native UI and integrate MarketBoard as its only app.
# Requires Ubuntu/Debian or WSL2, network, clang-18, GNU Make and Python 3.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
UPSTREAM=https://github.com/blackbearreloaded/ps5-homebrew-ui.git
BUILD_CACHE=${MARKETBOARD_BUILD_DIR:-"$ROOT/.build"}
# The upstream zlib install recipes do not quote paths containing spaces.
if [[ "$BUILD_CACHE" == *[[:space:]]* ]]; then
  if [[ -n ${MARKETBOARD_BUILD_DIR:-} ]]; then
    echo 'ERROR: MARKETBOARD_BUILD_DIR must be a path without whitespace' >&2
    exit 1
  fi
  BUILD_CACHE="/tmp/ps5-marketboard-build-$UID"
fi
WORK="$BUILD_CACHE/ps5-homebrew-ui"
mkdir -p "$BUILD_CACHE"
echo "Build cache: $BUILD_CACHE"
if [[ ! -d "$WORK/.git" ]]; then
  git clone --depth 1 "$UPSTREAM" "$WORK"
fi
# The kit is reused, not forked or modified in place in the remote repository.
cp -a "$ROOT/src/." "$WORK/src/"
mkdir -p "$WORK/assets/crypto"
cp -a "$ROOT/assets/crypto/." "$WORK/assets/crypto/"
cp "$ROOT/sce_sys/icon0.png" "$WORK/sce_sys/icon0.png"
# Preserve app-local source in a separate folder and copy the original GPL notices.
make -C "$WORK" init TITLE_ID=PPSA99880 APP_NAME="MarketBoard" APP_CATEGORY=media
make -C "$WORK" app
if [[ ! -f "$WORK/dist/PPSA99880/eboot.bin" ]]; then
  echo 'ERROR: expected eboot.bin was not produced' >&2; exit 1
fi
mkdir -p "$ROOT/output"
rm -rf "$ROOT/output/PPSA99880"
cp -a "$WORK/dist/PPSA99880" "$ROOT/output/"
# Preserve the console file permissions stored by the upstream ZIP builder.
cp "$WORK/dist/PPSA99880.zip" "$ROOT/output/PPSA99880.zip"
echo "Created: $ROOT/output/PPSA99880.zip"
echo 'Copy output/PPSA99880/ to /data/homebrew/PPSA99880/ for ShadowMountPlus.'
