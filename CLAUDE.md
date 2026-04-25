# DaemonLink — Project Context for Claude

## 1. Mission
DaemonLink is a tactical, multiprotocol, **strictly headless** hardware pentesting device — no screen, no buttons, no battery. The hardware acts as a silent daemon, controlled entirely from an Android phone over a wired USB-C link. The UI is a PWA (vanilla HTML/JS) that talks to the device through the browser's native **Web Serial API**.

## 2. Architecture & Code Quality Rules
As the primary developer (AI), adhere strictly to these rules:

- **C++ (Backend):** C++17. All code must be **non-blocking**. The ESP32-S3 runs timing-critical RF attacks; any excessive delay or blocking loop will trigger a Watchdog Reset. Use FreeRTOS tasks to isolate heavy processes.
- **JS/HTML (Frontend):** Zero frameworks (no React, no Vue). Pure vanilla JS. Mandatory `async/await` for Web Serial API stream handling. Implement a robust parser that reads the serial buffer **line by line** (handles chunk fragmentation up to `\n`).
- **Communication Protocol:** Modules emit **structured logs** to the serial port using channel prefixes (e.g. `[NFC]`, `[IR]`, `[SYS]`). The frontend uses these prefixes to route output to the correct DOM region.

## 3. Hardware Stack & Pinout (ESP32-S3 DevKitC-1)
**Do not invent or reassign pins.** This is the definitive map:

| Function | Component | Interface | Pins |
|---|---|---|---|
| Host link | Native USB-C (CDC enabled) | USB | — |
| NFC | PN532 V3 | I²C | `SDA = GPIO 8`, `SCL = GPIO 9` |
| IR receiver | VS1838B | Digital | `GPIO 4` |
| IR emitter | LED | Digital (PWM) | `GPIO 5` |
| *(future)* RF | CC1101 / NRF24L01 | SPI | `SCK = 12`, `MISO = 13`, `MOSI = 11` |

## 4. Repository Layout
```
DaemonLink/
├── platformio.ini                            # build config + extra_scripts hook
├── lib/DaemonLink/                           # our modules (auto-discovered by PIO)
│   ├── DaemonLink_NFC.{h,cpp}                # PN532 driver wrapper (non-blocking)
│   └── DaemonLink_CLI.{h,cpp}                # CLI shim injected into Marauder
├── external/ESP32Marauder/                   # git submodule -> upstream Marauder
│   └── esp32_marauder/                       # the actual sketch (src_dir)
├── patches/marauder.patch                    # 4-line injection (idempotent)
├── scripts/apply_patches.py                  # PIO pre-script + standalone CLI
└── web/                                      # PWA frontend (Phase B, pending)
```
- PlatformIO compiles `external/ESP32Marauder/esp32_marauder/` as the main sketch (`src_dir`). Our `lib/DaemonLink/` is auto-linked because PlatformIO scans `lib/`.
- `scripts/apply_patches.py` runs as `extra_scripts = pre:` on every `pio run`. It is idempotent: detects an already-patched submodule via `git apply --reverse --check` and skips.

## 5. Current State (Phase A — DONE)
- `platformio.ini` — board `esp32-s3-devkitc-1`, native USB CDC enabled, `extra_scripts` wired.
- `lib/DaemonLink/DaemonLink_NFC.{h,cpp}` — PN532 over I²C (SDA=8, SCL=9), `readMifareUID()`.
- `lib/DaemonLink/DaemonLink_CLI.{h,cpp}` — shim with `DaemonLink_initCli()` + `DaemonLink_handleCli()`. Spawns the NFC read on a pinned FreeRTOS task (core 1, prio 1, 4 KB stack), single-flight via volatile busy flag.
- `patches/marauder.patch` — adds 4 lines across `CommandLine.h`, `CommandLine.cpp`, `esp32_marauder.ino`. No existing line is modified — keeps future upstream merges trivial.
- Marauder vendored at `external/ESP32Marauder` (submodule, master).

### Known gap before first build
Marauder's `configs.h` currently has no `HEADLESS_MODE` flag — board target must be picked from the existing list (`XIAO_ESP32_S3` is the closest match for our DevKitC-1). This is a separate task from Phase A; the injection is correct regardless.

## 6. Next Steps

### Phase B — PWA Frontend (HTML/JS)
Build `web/index.html`.
- **Goal:** dark interface (hacker/terminal theme).
- **Required functionality:**
  1. `requestPort()` button to pair with the ESP32-S3.
  2. Read loop on the serial port's `ReadableStream`, decoded with `TextDecoder`, handling chunk fragmentation up to `\n`.
  3. Visual console (`<div>`) that prints output.
  4. Text input and/or quick-action buttons that send strings (e.g. `nfc_read\n`) to the hardware's `WritableStream`.

## Final Instruction
When the user asks to continue, read the current project code and execute **Phase A** or **Phase B** as instructed, providing complete code blocks and concise explanations on how to integrate them.

## Commit Convention
**Do not add `Co-Authored-By: Claude …` trailers.** The user does not want Claude credited as co-author on commits.
