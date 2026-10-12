# PS5 MarketBoard — Native Crypto Watchlist

A homebrew **native PS5 title**, not an ELF payload or Linux boot environment. Uses the PS5 Homebrew UI OpenGL kit. Source-only preview **v0.2**; not yet tested on PS5 hardware.

## Features

- Full-screen dark UI, animated backdrop, local date and digital clock.
- Live price and reported 24-hour change, with exchange attribution and last successful update time.
- Watchlist with up to 12 user-defined BASE/USD or BASE/USDT pairs, and an on-screen DualSense keyboard.
- Native pairs, **without** USD/USDT conversion. No AUD.
- Configurable refresh: **5s / 15s / 30s / 60s / 120s**, default 15 seconds, saved per title to `/download0/marketboard/prefs.txt`.
- Binance / KuCoin / Coinbase / CoinGecko fallback. Only providers listing the **exact** pair are eligible. For USDT: Binance and KuCoin; for USD: Coinbase, Binance, KuCoin and CoinGecko (CoinGecko only for explicitly mapped assets).
- After a `429` response, per-provider exponential cooldown (30 to 600 seconds), or longer if the response has a numeric `Retry-After` (up to 3600 seconds). `403`/`451` triggers a temporary 120-second cooldown.
- Data requests execute on a background network thread with HTTPS certificate verification. Retains last known price on outages with a stale state. Never fabricates chart history; sparklines use only successfully fetched prices collected during the current app session.
- No API keys or credentials required for the public endpoints.

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

## Build (Ubuntu 24.04, Debian with clang-18, or WSL2)

The supplied `build-linux.sh` downloads [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) and overlays this application's sources into the upstream native application builder. The upstream project builds the native home-screen title using PS5 Payload SDK, ps5-opengl, its bundled native FSELF/signing conversion flow, and its clean-room runtime.

Install system tools (Ubuntu):

```bash
sudo apt update
sudo apt install -y clang-18 clang-format-18 clang-tidy-18 lld-18 clang \
  make ninja-build ccache pkg-config git curl wget unzip tar zip \
  python3 python3-pip python3-venv
```

Test portable C++ components (works on a PC):

```bash
./run-tests.sh
```

Then run:

```bash
./build-linux.sh
```

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

## Installation — ShadowMountPlus

Prerequisites: PS5 already running a compatible jailbreak/homebrew environment, with ShadowMountPlus configured.

Recommended method: copy the **uncompressed** `output/PPSA99880/` directory via FTP to:

```text
/data/homebrew/PPSA99880/
```

Then rescan/register with ShadowMountPlus. Its default scan roots also include a USB drive's `homebrew` directory. Folder installs are easier to update than disk images.

An **optional** `.exfat` image can be created **after the native folder has successfully run**, using ShadowMountPlus's own `mkexfat.sh`. The image must have `sce_sys/param.json` and `eboot.bin` directly in its filesystem root. Do not rename `PPSA99880.zip` to `.exfat`.

ShadowMountPlus documentation: https://github.com/VB7749/ShadowMountPlus

## Implementation / limitations

- This is a source deliverable, **not** a claimed compiled or hardware-verified binary. The cross-compilation toolchain and upstream SDK could not be downloaded in the authoring environment.
- The graphical UI is written for the upstream kit's `gfx::Renderer`, `ui::Fonts`, and `ps5::Display`. The HTTPS transport follows the tested **sandboxed** `sceHttp`/`sceSsl` path documented in ps5-native-app-boilerplate. Do not elevate the app: elevated titles can encounter different certificate behaviour.
- 24h change values are exchange-specific; providers may calculate the period differently.
- Public endpoint geographic availability and rate limits vary; not every listed pair exists everywhere.
- The five-second interval may still be too aggressive with a long watchlist; the backoff limits repeated failures, and longer settings are recommended for 10+ pairs.
- Price fetches are currently per pair (not yet batched); batching is a later improvement.
- The date/time displayed is the PS5's configured local time, not a remote time service.
- Future enhancements: cached history across sessions, configurable source ordering, batch requests and host-side screenshot verification against the full UI kit.

## Technical validation performed

- Host tests for watchlist normalization/add/remove/save/load and four source parsers, including rejection of malformed input.
- C++20 compile-level syntax check of `main.cpp` and network source against API-compatible local declarations.
- **Not** yet performed: actual PS5 cross-build, signed output validation, USB/ShadowMount installation, networking/graphics tests on a console.

## Licensing / acknowledgments

GPL-3.0-or-later. Relies on the GPL-licensed [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) and [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate). See the upstream projects' notices and third-party font/library licenses. This is an independent homebrew experiment, not affiliated with Sony or exchanges.
