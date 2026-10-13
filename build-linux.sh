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
# Native game, not a media app. APP_CATEGORY=media writes
# applicationCategoryType 65536 and contentBadgeType 2, and deletes gameIntent.
# The shell then files the icon under Media. This eboot is the kit's game
# FSELF, so that media launch path rejects it before main() with the system
# "cannot open the app" dialog. game restores category 0, badge 1, and
# gameIntent launchActivity, which is what the shell requires to start it.
make -C "$WORK" init TITLE_ID=PPSA99880 APP_NAME="MarketBoard" APP_CATEGORY=game
python3 - "$WORK/sce_sys/param.json" <<'PY'
import json
import sys

param = json.load(open(sys.argv[1], encoding="utf-8"))
intents = param.get("gameIntent", {}).get("permittedIntents", [])
launchable = any(
    isinstance(item, dict) and item.get("intentType") == "launchActivity"
    for item in intents
)
if (
    param.get("titleId") != "PPSA99880"
    or param.get("applicationCategoryType") != 0
    or param.get("contentBadgeType") != 1
    or not launchable
):
    raise SystemExit(
        "ERROR: sce_sys/param.json is not a launchable native game "
        "(need category 0, badge 1, and gameIntent launchActivity)"
    )
print(
    "param.json: native game",
    param["titleId"],
    param.get("contentId", ""),
)
PY
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
