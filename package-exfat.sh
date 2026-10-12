#!/usr/bin/env bash
# Package the compiled native title as a raw exFAT image for ShadowMountPlus.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
INPUT="$ROOT/output/PPSA99880"
TOOLS="$ROOT/.build/exfat-tools"
MKPFS_VERSION=1.1.0

if [[ ! -f "$INPUT/eboot.bin" || ! -f "$INPUT/sce_sys/param.json" ||
      ! -f "$INPUT/sce_module/libc.prx" ]]; then
  echo 'ERROR: build the native title with ./build-linux.sh first' >&2
  exit 1
fi

# Keep optional packages local, including on hosts without python3-venv.
run_python(){
  PYTHONPATH="$TOOLS" python3 "$@"
}
if ! run_python -c 'from importlib.metadata import version; import sys; sys.exit(version("mkpfs") != sys.argv[1])' "$MKPFS_VERSION" 2>/dev/null; then
  python3 -m pip install --disable-pip-version-check --no-cache-dir \
    --upgrade --target "$TOOLS" "mkpfs==$MKPFS_VERSION"
fi

# Publish only after all image files match the compiled title by SHA-256.
IMAGE_STAGE=$(mktemp "$ROOT/output/.PPSA99880.XXXXXX.exfat")
trap 'rm -f -- "$IMAGE_STAGE"' EXIT
run_python -m mkpfs pack exfat "$INPUT" "$IMAGE_STAGE" \
  --cluster-size 65536 --overwrite --no-progress
run_python -m mkpfs verify "$IMAGE_STAGE" --format exfat --source-dir "$INPUT"
echo 'Image structure and file contents verified against the compiled title.'
mv -f -- "$IMAGE_STAGE" "$ROOT/output/PPSA99880.exfat"
(cd "$ROOT/output" && sha256sum PPSA99880.exfat > PPSA99880.exfat.sha256)
echo "Created: $ROOT/output/PPSA99880.exfat"
