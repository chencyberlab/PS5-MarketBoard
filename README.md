# PS5 MarketBoard — Native Crypto Watchlist

A homebrew **native PS5 game**, not a media app, ELF payload, or Linux boot environment. Uses the PS5 Homebrew UI OpenGL kit. Preview **v0.2**; locally cross-compiled and packaged. The shell only starts it from the Games row.

## Features

- Full-screen dark UI, animated backdrop, local date and digital clock.
- Live price and reported 24-hour change, with exchange attribution and last successful update time.
- Watchlist with up to 12 user-defined BASE/USD or BASE/USDT pairs, and an on-screen DualSense keyboard.
- Native pairs, **without** USD/USDT conversion. No AUD.
- Configurable refresh: **5s / 15s / 30s / 60s / 120s**, default 15 seconds, saved per title to `/download0/marketboard/prefs.txt`.
- Binance / KuCoin / Coinbase / CoinGecko fallback. Only providers listing the **exact** pair are eligible. For USDT: Binance and KuCoin; for USD: Coinbase, Binance, KuCoin and CoinGecko (CoinGecko only for explicitly mapped assets).
- After a `429` response, per-provider exponential cooldown (30 to 600 seconds), or longer if the response has a numeric `Retry-After` (up to 3600 seconds). `403`/`451` triggers a temporary 120-second cooldown.
- Data requests execute on a background network thread with HTTPS certificate verification. Retains last known price on outages with a stale state. Never fabricates chart history; sparklines use only successfully fetched prices collected during the current app session.
- Bundled BTC, ETH, SOL, XRP, DOGE and USDT logos, plus optional CoinGecko image downloads, a persistent local icon cache, and colored symbol badges when no image is available.
- Exchange price endpoints require no credentials. Optional CoinGecko icon lookup can use a Demo API key configured on the PS5.

## Controls

| Button | Action |
|---|---|
| D-pad ↑ / ↓ | Select a watchlist pair |
| Options or Cross | Add a pair (on-screen keyboard) |
| Triangle | Remove selected pair (at least one must remain) |
| L1 | Faster refresh interval |
| R1 | Slower refresh interval |
| Square | Request an immediate refresh |
| Circle | Exit normally |
| Keyboard: D-pad, Cross | Navigate/type symbol |
| Keyboard: Triangle | Delete last character |
| Keyboard: Options | Save new pair |
| Keyboard: Circle | Cancel |

Examples: `BTC/USDT`, `ETH/USD`, `DOGE/USDT`. The base must have 2–10 uppercase letters/digits. Unsupported exchange pairs will show as unavailable rather than substituted.

## Token icons

Both the selected quote and watchlist show an icon for the **base token**.
`BTC/USD` and `BTC/USDT` share the same image and GPU texture. Resolution order:

1. Load a bundled PNG from `/app0/assets/crypto` for BTC, ETH, SOL, XRP, DOGE or USDT.
2. Reuse a downloaded image from `/download0/marketboard/icons/BASE.img`.
3. If downloads are enabled, query [CoinGecko Search](https://docs.coingecko.com/reference/search-data) and download its image over verified HTTPS.
4. Display a circle containing the base symbol while loading or when lookup fails.

The `.img` cache files contain the original PNG/JPEG bytes. Images are decoded and validated **before** atomic cache writes. Downloads are limited to 256 KiB and 256×256 pixels; WebP/SVG and larger images use the symbol badge. The cache keeps at most 64 downloaded images and evicts the oldest saved files. Bundled images stay in the application folder.

Image I/O, decoding and network requests use a dedicated background thread, separate from price polling. The render thread uploads at most one ready image per frame, shares textures across quote currencies, and releases textures when a base leaves the watchlist. Cached images are reused across launches without another download. Unavailable images retry after 30 minutes; metadata requests are spaced by at least 10 seconds with additional failure/rate-limit backoff and numeric `Retry-After` support.

Symbols are not unique identifiers. Explicitly mapped assets must match the existing CoinGecko ID in `market_core.cpp`; other symbols need exactly one exact symbol match in the search response. Ambiguous matches use the badge. Only CoinGecko's HTTPS coin-image hosts are accepted; redirects are disabled. Image lookup never changes the trading pair or price-provider choice.

Downloads are enabled by default. To disable them, create `/download0/marketboard/icons-enabled.txt` containing `0`. Set it to `1` or remove it to enable downloads. Bundled and cached images work in either mode. Configuration is read at launch; restart after changing it.

For [CoinGecko Demo authentication](https://docs.coingecko.com/demo/reference/authentication), optionally put only your Demo key in `/download0/marketboard/coingecko-demo-key.txt` on the PS5 (one line, up to 128 printable ASCII characters). It is sent through the `x-cg-demo-api-key` header to the CoinGecko API, never to image hosts, and is not saved in preferences or bundled into the title. Without a key the app attempts the public endpoint; authentication/rate-limit failures leave the badge visible. No Mac environment setup is needed for the code change.

The six checked-in PNGs come from the CC0 [Cryptocurrency Icons](https://github.com/spothq/cryptocurrency-icons) project. A vendored `stb_image.h` provides PNG/JPEG decoding without another installed package or link dependency. Source and license details are included beside the assets and decoder.

## Build (Ubuntu 24.04, Debian with clang-18, or WSL2)

The supplied `build-linux.sh` downloads [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) and overlays this application's sources into the upstream native application builder. The upstream project builds the native home-screen title using PS5 Payload SDK, ps5-opengl, its bundled native FSELF/signing conversion flow, and its clean-room runtime.

For **Ubuntu 24.04 x64**, install these system tools manually first. `build-linux.sh` does **not** run `apt` or install system packages:

```bash
sudo apt update
sudo apt install -y \
  build-essential clang-18 llvm-18-dev lld-18 libclang-rt-18-dev \
  ninja-build ccache pkg-config git curl wget unzip zip tar \
  python3 ca-certificates
```

Then, from this project's root directory, select LLVM 18, run the portable C++ tests, and build the PS5 title:

```bash
export LLVM_CONFIG=/usr/bin/llvm-config-18
./run-tests.sh
./build-linux.sh
```

The `LLVM_CONFIG` setting applies to the current shell; set it again when building from a new terminal.

The build script and upstream builder automatically:

- Clone `ps5-homebrew-ui` and overlay MarketBoard's sources and bundled token icons.
- Download the pinned PS5 Payload SDK and ps5-opengl SDK.
- Download and build a local zlib dependency and the native build tools.
- Build the clean-room runtime and application, then package the title folder and ZIP.

Downloaded dependencies and intermediate build files stay under this project's `.build/` directory. If the project path contains spaces, the script uses `/tmp/ps5-marketboard-build-<user ID>` because the upstream zlib installation does not support spaces in its output path. To choose another cache, set `MARKETBOARD_BUILD_DIR` to an absolute path without spaces, for example:

```bash
MARKETBOARD_BUILD_DIR=/tmp/ps5-marketboard-build ./build-linux.sh
```

The finished title and ZIP always go into this project's `output/` directory. Internet access is required for the first build. Node.js, pip packages and a separate PNG library are unnecessary for this build; the token-image decoder is included in the source. `clang-format-18` and `clang-tidy-18` are optional for upstream lint/format checks. The basic build does not require `python3-pip` or `python3-venv`.

The script builds an **installable title folder**, creating:

```text
output/
  PPSA99880/
    eboot.bin
    sce_module/libc.prx
    sce_sys/param.json
    sce_sys/icon0.png
    assets/...
  PPSA99880.zip
```

This archive is **not a conventional PS5 PKG**: it is the game/homebrew directory that ShadowMountPlus can discover and register. The script cannot complete without downloading the upstream UI kit and PS5 build dependencies. A PS5 hardware test remains necessary.

The default title ID is `PPSA99880`; **make sure it does not conflict with an existing title**. Change it in `build-linux.sh` if needed, in the `make init`, output validation and copy commands.

`build-linux.sh` initializes the title as a **game** (`APP_CATEGORY=game`). That writes the three fields the shell uses to place and start a native title:

| Field | Game value | What the shell does with it |
|---|---|---|
| `applicationCategoryType` | `0` | Native game. `65536` is a Prospero native media app and is filed under Media. |
| `contentBadgeType` | `1` | Game badge. `2` is the non-game badge. |
| `gameIntent.permittedIntents` | `launchActivity` | Required before the shell will start a game. The media initializer deletes this field. |

The executable is the UI kit's game FSELF. A media `param.json` still produces an icon, but the media launch path rejects that executable before `main` and the system reports that it cannot open the app. The build stops if `sce_sys/param.json` is not this game combination. Category does not change the sandbox: HTTPS stays on the unprivileged `sceHttp`/`sceSsl` path.

## Installation — ShadowMountPlus

Prerequisites: PS5 already running a compatible jailbreak/homebrew environment, with ShadowMountPlus configured.

Recommended method: copy the **uncompressed** `output/PPSA99880/` directory via FTP to:

```text
/data/homebrew/PPSA99880/
```

If MarketBoard is already on the home screen, delete that tile first (Options, then Delete). The shell keeps the category from the first registration. Replacing the files for `PPSA99880` in place leaves the old Media entry, and that entry still refuses to open. Then rescan/register with ShadowMountPlus. The new tile belongs in **Games**. ShadowMountPlus's default scan roots also include a USB drive's `homebrew` directory. Folder installs are easier to update than disk images.

For distribution as one mountable file, package the compiled title as a raw exFAT filesystem image:

```bash
bash package-exfat.sh
```

The script installs the optional [MkPFS](https://github.com/PSBrew/MkPFS#pack-exfat) tool into a local package directory under `.build/exfat-tools` on its first run. It needs Python 3.9+ with pip (`sudo apt install python3-pip` on Ubuntu if missing) and internet access for that first installation. It does not need root privileges or mount a disk. It creates `output/PPSA99880.exfat` with 64 KiB clusters and `output/PPSA99880.exfat.sha256`, and compares every image file to the compiled folder by SHA-256 before publishing the image.

An `.exfat` image is another package of the same compiled executable; it does not require a different compile target. `sce_sys/param.json`, `sce_module/libc.prx`, assets and `eboot.bin` sit directly at the image root. Do not rename `PPSA99880.zip` to `.exfat`.

Copy the image to a ShadowMountPlus scan folder such as `/data/homebrew/MarketBoard/PPSA99880.exfat` or `/mnt/usb0/homebrew/MarketBoard/PPSA99880.exfat`, then rescan/register. Use either the folder install or the image for this title ID to avoid duplicate registrations. Image creation and integrity checks do not verify launching on a PS5; test on hardware before distributing it as a working release.

ShadowMountPlus documentation: https://github.com/VB7749/ShadowMountPlus

## Implementation / limitations

- The local PS5 cross-build succeeds and produces a signed title folder and ZIP. The executable and runtime pass the upstream container integrity checks; console behaviour remains unverified.
- The graphical UI is written for the upstream kit's `gfx::Renderer`, `ui::Fonts`, and `ps5::Display`. The HTTPS transport follows the tested **sandboxed** `sceHttp`/`sceSsl` path documented in ps5-native-app-boilerplate. Do not elevate the app, and do not switch it back to `APP_CATEGORY=media`: elevated titles can encounter different certificate behaviour, and a media classification is what places the icon in Media and makes the shell refuse to open it.
- 24h change values are exchange-specific; providers may calculate the period differently.
- Public endpoint geographic availability and rate limits vary; not every listed pair exists everywhere.
- The five-second interval may still be too aggressive with a long watchlist; the backoff limits repeated failures, and longer settings are recommended for 10+ pairs.
- Price fetches are currently per pair (not yet batched); batching is a later improvement.
- The date/time displayed is the PS5's configured local time, not a remote time service.
- Future enhancements: cached history across sessions, configurable source ordering, batch requests and host-side screenshot verification against the full UI kit.

## Technical validation performed

- Host tests for watchlist normalization/add/remove/save/load and four source parsers, including rejection of malformed input, passed under AddressSanitizer and UndefinedBehaviorSanitizer.
- Portable icon tests for exact/ambiguous identity matching, safe URLs/cache paths, malformed JSON, bundled image decoding and size limits passed under the same sanitizers.
- The actual PS5 cross-build completed with LLVM 18, PS5 Payload SDK v0.42 and ps5-opengl SDK 1.0.0. The runtime checksum and signed `eboot.bin`/`libc.prx` container integrity checks passed.
- ZIP integrity checks passed. The raw `.exfat` image was created with MkPFS 1.1.0 and passed image structure and file-by-file SHA-256 checks against the compiled title folder.
- **Not** yet performed: USB/ShadowMount installation or networking/graphics tests on a console.

## Licensing / acknowledgments

GPL-3.0-or-later. Relies on the GPL-licensed [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) and [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate). See the upstream projects' notices and third-party font/library licenses. This is an independent homebrew experiment, not affiliated with Sony or exchanges.
