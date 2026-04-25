# DaemonLink — Project Context for Claude

> **Status**: Phases A → H complete. The backend, frontend, persistence layer
> and CI/CD pipeline form a coherent, deployable system. This document is the
> single source of truth for picking the project back up; read it before
> proposing any change.

## 1. Mission
DaemonLink is a tactical, multiprotocol, **strictly headless** hardware pentesting device — no screen, no buttons, no battery. The hardware acts as a silent daemon, controlled entirely from an Android phone over a wired USB-C link. The UI is a PWA (vanilla HTML/JS) that talks to the device through the browser's native **Web Serial API**.

## 2. Architecture & Code-Quality Rules
Adhere strictly:

- **C++ (Backend)**: C++17. All code must be **non-blocking**. The ESP32-S3 runs timing-critical RF attacks; any excessive delay or blocking loop will trigger a Watchdog Reset. Use FreeRTOS tasks (pinned to core 1, prio 1, 4–6 KB stack) to isolate heavy operations. The dispatcher (`DaemonLink_handleCli`) **always** returns immediately.
- **JS/HTML (Frontend)**: Zero frameworks (no React, no Vue). Pure vanilla JS. Mandatory `async/await` for Web Serial API stream handling. The line parser uses `TextDecoderStream` piped through a custom `LineBreakTransformer` that buffers chunks and emits one event per `\r?\n`-terminated line.
- **Communication Protocol (Phase E)**: **Strict JSON contract**, see §6. No more regex over `[NFC]` / `[IR]` text prefixes. Plain-text channel routing only survives as a *fallback path* for native Marauder logs (Wi-Fi scan output, etc.).
- **Marauder upstream patch**: 4 lines added across 3 files, **0 modified, 0 deleted**. Every new feature must keep this footprint at zero — drop new logic into `lib/DaemonLink/`, not into the submodule.

## 3. Hardware Stack & Pinout (ESP32-S3 DevKitC-1)
**Do not invent or reassign pins.** This is the definitive map:

| Function | Component | Interface | Pins |
|---|---|---|---|
| Host link | Native USB-C (CDC enabled) | USB | — |
| NFC | PN532 V3 | I²C | `SDA = GPIO 8`, `SCL = GPIO 9` |
| IR receiver | VS1838B | RMT (DMA) | `GPIO 4` |
| IR emitter | LED | LEDC (PWM) | `GPIO 5` |
| *(reserved)* RF Sub-1 GHz | CC1101 / NRF24L01 | SPI | `SCK = 12`, `MISO = 13`, `MOSI = 11` |

The reserved SPI pins for the future RF module are already declared at the macro level in our `MARAUDER_DAEMONLINK` block of `configs.h` so no Marauder feature can accidentally claim them.

## 4. Repository Layout
```
DaemonLink/
├── platformio.ini                            # build config, libs, partition CSV pointer
├── boards/daemonlink_partitions.csv          # custom partition table (see §7)
├── lib/DaemonLink/                           # auto-discovered by PlatformIO from lib/
│   ├── DaemonLink_NFC.{h,cpp}                # PN532 driver (Mifare UID read)
│   ├── DaemonLink_IR.{h,cpp}                 # IR capture + replay + last-capture cache
│   ├── DaemonLink_FS.{h,cpp}                 # LittleFS-backed payload store
│   ├── DaemonLink_CLI.{h,cpp}                # CLI shim injected into Marauder
│   └── DaemonLink_Json.h                     # ArduinoJson v7 emit helpers
├── external/ESP32Marauder/                   # git submodule -> upstream Marauder
├── patches/marauder.patch                    # 4-line idempotent injection
├── scripts/apply_patches.py                  # PIO pre-script + standalone CLI
├── web/                                      # PWA frontend
│   ├── index.html                            # single-file console (CSS+JS embedded)
│   ├── manifest.json                         # PWA manifest (standalone, dark theme)
│   ├── sw.js                                 # Service Worker (offline shell)
│   ├── icon.svg                              # any-purpose icon
│   └── icon-maskable.svg                     # Android adaptive icon
├── firebase.json                             # Firebase Hosting config (see §8)
├── .firebaserc                               # Firebase project alias (daemonlink-541cd)
├── requirements.txt                          # Python deps for the CI runner
└── .github/workflows/main.yml                # CI/CD pipeline (see §8)
```

PlatformIO compiles `external/ESP32Marauder/esp32_marauder/` as the main sketch (`src_dir`); our `lib/DaemonLink/` is auto-linked. The patch script emits `esp32_marauder.ino.cpp` next to the .ino because PlatformIO does not auto-preprocess Arduino sketches when `src_dir` lives outside the project root.

## 5. Current State — Phases A → H complete

| Phase | Theme | Headline outcome |
|---|---|---|
| A   | Vendor Marauder + patch pipeline | submodule pinned, idempotent `apply_patches.py` |
| A.5 | Compile target (`MARAUDER_DAEMONLINK`) | firmware compiles green for headless ESP32-S3 |
| B   | Single-file PWA | Web Serial console, channel-coloured terminal |
| B.1 | PWA installable + offline | manifest + Service Worker (`web/sw.js`) |
| C   | IR capture | non-blocking RX on RMT/DMA, `[IR] capture` events |
| D   | IR replay | `ir_send` parser (protocol + raw), `replay:` payload |
| E   | JSON framing | every module emits structured `{source, event, ...}` lines |
| G   | Tactical persistence | LittleFS partition + `ir_save` / `ir_play` / `fs_list` |
| H   | Hosting + CI/CD | Firebase Hosting at `daemonlink-541cd.web.app` + GitHub Actions |

(Phase F — CI-only — was folded into Phase H. There is no ungrouped Phase F.)

### CLI surface
All commands are absorbed by `DaemonLink_handleCli()` before Marauder's parser sees them. Marauder native commands (`scanall`, `sniffraw`, `attack -t deauth`, …) keep working untouched.

| Command | Effect | Backed by |
|---|---|---|
| `dl_help` | List DaemonLink commands as a `{source: sys, event: help, commands: [...]}` event | shim |
| `nfc_read` | Read Mifare UID via PN532, emit `{source: nfc, event: tag, ...}` | `DaemonLink_NFC` |
| `ir_capture` | Capture & decode IR remote, emit `{source: ir, event: capture, ..., replay: ...}` | `DaemonLink_IR` |
| `ir_send <PROTO> <bits> <hex>` | Transmit a known protocol (e.g. `ir_send NEC 32 0xff629d`) | `DaemonLink_IR` |
| `ir_send raw <khz> <us,us,...>` | Transmit a raw timing payload (e.g. `ir_send raw 38 9000,4500,…`) | `DaemonLink_IR` |
| `ir_save <name>` | Persist the last RAM capture to `/ir/<name>.json` on LittleFS | `DaemonLink_FS` |
| `ir_play <name>` | Load `/ir/<name>.json` and re-feed it into IR transmit | `DaemonLink_FS` + `DaemonLink_IR` |
| `fs_list` | List the IR library as `{source: fs, event: list, items: [...], total, used}` | `DaemonLink_FS` |

`<name>` is validated against `[A-Za-z0-9_-]{1,32}` on both the firmware and the PWA (defence in depth — never accept path injection).

## 6. Communication Protocol (Phase E) — JSON Contract

Every event our modules emit is a **single line of minified JSON terminated by `\n`**. Plain-text framing with `[NFC]` / `[IR]` prefixes was retired in this phase — the PWA no longer regex-matches; it `JSON.parse`s. Marauder's native plain-text logs are NOT touched and the PWA falls through them via a `^\s*\{` heuristic.

### Required fields
- `source`: one of `"nfc"`, `"ir"`, `"fs"`, `"sys"`. Routes the card colour and the badge in the PWA.
- Either `event` (state) or `level: "error"` (failure).

### Memory discipline
- Every `JsonDocument` is **stack-scoped** inside the FreeRTOS task that emits it (NFC task = 4 KB stack, IR/FS tasks = 6 KB).
- ArduinoJson v7's dynamic backing heap is freed deterministically when the document leaves scope. Documents are tiny (<256 B typical, ~6 KB worst case for raw IR replay) — OOM is impossible at the configured stack budgets.
- No global `JsonDocument`. No `StaticJsonDocument` (deprecated, prone to silent overflow).

### Sample payloads
```json
{"source":"nfc","event":"tag","type":"mifare_classic","uid":"a3:1f:bc:0e","uid_len":4}
{"source":"ir","event":"capture","protocol":"NEC","bits":32,"hex":"00FF629D","repeat":false,"replay":"NEC 32 0xff629d"}
{"source":"ir","event":"send","mode":"protocol","protocol":"NEC","bits":32,"hex":"00FF629D","status":"ok"}
{"source":"fs","event":"list","path":"/ir","total":393216,"used":62,"count":1,"items":[{"name":"tv_samsung","size":62}]}
{"source":"sys","level":"error","msg":"PN532 not responding on I2C (SDA=8 SCL=9)"}
```

### Frontend rendering
- Lines that pass the `^\s*\{` check go through `JSON.parse` → `renderJsonEvent()` (cards with coloured left border, uppercase badge, key/value grid).
- Anything else (Marauder Wi-Fi/BT scan output, garbled chunks) renders as plain coloured text. The fallback is silent on parse failure.

## 7. Storage & Partitions (Phase G)

Marauder mounts SPIFFS for its settings store inside `settings.cpp`. **We mount LittleFS in a separate partition** so the two filesystems never alias the same flash region — neither side knows the other exists.

### `boards/daemonlink_partitions.csv`
```
nvs       0x09000   20 KB
otadata   0x0E000    8 KB
app0      0x10000  3.25 MB    (Marauder + DaemonLink firmware)
spiffs    0x350000  256 KB    (Marauder settings, mount label "spiffs")
littlefs  0x390000  384 KB    (DaemonLink payloads, mount label "littlefs")
coredump  0x3F0000   64 KB
```

ESP-IDF discriminates partitions by **label**, not subtype — both partitions can share `SubType=spiffs` because the labels differ. `LittleFS.begin(formatOnFail=true, "/littlefs", 5, "littlefs")` mounts our partition; Marauder's `SPIFFS.begin()` finds its own.

### Persistence commands
- `ir_save <name>` — synchronous (LittleFS write < 50 ms; never spawns a task because nothing RF-critical runs on this path).
- `ir_play <name>` — runs in a FreeRTOS task because it calls `IRsend.send()` (potentially long); single-flighted via `g_ir_busy` against `ir_capture` and `ir_send`.
- `fs_list` — synchronous; returns a single `{source: fs, event: list}` JSON line that the PWA's library panel auto-renders.

The PWA library panel auto-refreshes when it sees `{source: fs, event: save | remove}`, so the operator workflow is *capture → name → Save → Library panel updates → ▶ Play* with zero manual refresh.

## 8. Frontend, Hosting & CI/CD (Phases B.1 + H)

### PWA (Phase B.1)
- Single `web/index.html` (CSS + JS embedded). No bundler, no build step.
- Installable: `manifest.json` declares `display: standalone`, dark theme `#080a08`, two SVG icons (one regular, one maskable for Android adaptive launchers).
- Offline-capable: `web/sw.js` precaches the shell on `install`, `network-first` for navigation requests (so the shell rolls forward when online), `cache-first` for same-origin assets, passthrough for cross-origin. `CACHE_VERSION` constant — bump it when shipping a shell change.

### Hosting on Firebase
- Live URL: **`https://daemonlink-541cd.web.app`** (live channel, served by Firebase Hosting).
- `firebase.json` configures `web/` as the public root with sane PWA cache headers:
  - `sw.js`, `index.html` → `no-cache, no-store, must-revalidate` (CDN can never pin a stale shell).
  - `manifest.json` → 5-minute revalidating cache.
  - `*.svg` → `immutable, max-age=1y` (icons are content-hashed by filename — we never rename them).
  - `Permissions-Policy: serial=(self)` blocks any future iframe from abusing Web Serial against the device.
- `.firebaserc` pins the project alias `default → daemonlink-541cd`.

### CI/CD on GitHub Actions (`.github/workflows/main.yml`)
Three jobs, all triggered on push/PR to `main`:
1. **firmware** — checks out submodules (NON-recursive: Marauder pins old NimBLE/TFT_eSPI as nested submodules and we don't want PIO to find them under `lib_extra_dirs`), sets up Python 3.11 with pip cache, caches `~/.platformio/{packages,platforms,.cache}` and `.pio/libdeps`, then runs `apply_patches.py --status` + `apply_patches.py` + `pio run`. Uploads `firmware.bin/elf/partitions.bin` as a 30-day artifact. Catches Marauder-upstream drift before any human notices.
2. **deploy** — depends on `firmware`, gated to push-on-main. Ships `web/` to Firebase Hosting on the `live` channel via `FirebaseExtended/action-hosting-deploy@v0`. Authenticates with the `FIREBASE_SERVICE_ACCOUNT_DAEMONLINK_541CD` secret auto-provisioned by `firebase init hosting:github`; project id is hardcoded in the workflow.
3. **preview** — every PR gets a throwaway preview channel (`expires: 7d`); the action posts the preview URL as a PR comment.

A `concurrency` group cancels redundant in-flight runs on the same branch.

## 9. Build & Flash Instructions

### Local build
```
pio run                                  # compiles firmware (apply_patches runs as pre-script)
python scripts/apply_patches.py --status # check the Marauder patch state
python scripts/apply_patches.py --reverse # un-patch the submodule (for inspecting upstream)
```

### Flashing — **CRITICAL FOR FIRST FLASH**
The partition table changed in Phase G. Bootloader caches the old table on flash, so a plain `upload` corrupts the layout. Run **once**:
```
pio run -t erase           # wipe the entire flash
pio run -t upload          # flash the new firmware + partitions
```
After that, regular `pio run -t upload` is fine for normal iteration.

### LittleFS data partition
`format-on-fail` is enabled, so the first boot self-formats. No `uploadfs` step required.

## 10. Operator Flow (no hardware required to design against)
1. Open `https://daemonlink-541cd.web.app` in Chrome on Android (enable `chrome://flags/#enable-experimental-web-platform-features` if Web Serial is gated).
2. Tap **Install** in the browser menu — DaemonLink shows up as a standalone app with the dark icon. Works in airplane mode.
3. Plug the ESP32-S3 via USB-C → tap **Connect** → pick the CDC port.
4. Tap **Capture IR**, point a remote at the receiver. The card shows protocol/bits/hex with a clamped `replay` field.
5. Type a name in the IR-save row → **Save** → the **Library** panel opens with a `▶ Play` button per entry. Tap to retransmit.

## 11. Final Instruction
When the user asks to continue, read the current project state (this file is authoritative) and proceed to the requested work. **Drop new modules under `lib/DaemonLink/` first**; touch `patches/marauder.patch` only if absolutely unavoidable, and never grow it beyond a few extra dispatch lines.

## Commit Convention
**Do not add `Co-Authored-By: Claude …` trailers.** The user does not want Claude credited as co-author on commits.

## 12. Roadmap — Out of Scope (deliberately deferred)

Documented so future-Claude does not re-debate decisions or duplicate work.

### Hardware features
- **`nfc_write`** — extend `DaemonLink_NFC` with `nfc_write_block <sector> <key> <data>` for Mifare Classic. Authenticates with key A/B before writing. Reuses the existing FreeRTOS task pattern.
- **RF Sub-1 GHz module (CC1101)** — replays for car key fobs, garage remotes, etc. SPI pins already reserved (`SCK=12, MISO=13, MOSI=11`). New `lib/DaemonLink/DaemonLink_RF.{h,cpp}` + commands `rf_capture`, `rf_send`, `rf_save`/`rf_play` (mirroring the IR command shape).
- **NRF24L01 (2.4 GHz)** — for Mousejack-style HID injection. Same SPI bus as CC1101 with separate CS.

### Firmware quality
- **JSON Schema in this file** — formal `event` schemas under §6 so the PWA can typecheck at runtime in dev builds. Right now the contract is informal.
- **Telemetry / heartbeat** — periodic `{source: sys, event: heartbeat, free_heap, uptime}` so the PWA can detect a hung device.

### Frontend / Ops
- **Service Worker "Update available" prompt** — today the SW logs `update available — reload to apply` to the meta channel, but the user can miss it. Promote to a sticky banner inside the console with a one-tap "Reload" button so a fresh shell is always one gesture away.
- **Library export/import to PC** — let the PWA download `/ir/<name>.json` (single file or zipped) for backup, and re-upload to a different DaemonLink. Web Serial protocol: a `fs_get <name>` command that streams the file body and a `fs_put <name> <base64>` command that writes it.
- **Saved-payload metadata** — friendly description + tags + date in each LittleFS file so the library panel can group/filter (e.g. "all TV remotes").
- **Cancel-current-task** button — `dl_cancel` command + a kill-switch that `vTaskDelete()`'s the active worker (NFC waiting on a tag, IR capture timeout, etc.).
