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

## 4. Backend Structure & Current State
The firmware core is a fork of **ESP32 Marauder**, built with PlatformIO.

**Already implemented — do not rewrite:**
- `platformio.ini` — `esp32-s3-devkitc-1`, flags `-DHEADLESS_MODE=1`, `-DNO_DISPLAY=1`, native USB CDC enabled.
- `src/DaemonLink_NFC.cpp` / `.h` — isolated class that initializes the I²C bus and the PN532, with a non-blocking `readMifareUID()` function.

## 5. Next Steps (work to perform on user request)

### Phase A — CLI Injection (C++)
Intercept Marauder's command parser (typically `CommandLine.cpp`).
- **Goal:** inject custom commands (`nfc_read`, `ir_capture`, …) into Marauder's command table.
- **Constraint:** the injection must be as clean as possible to allow a future merge if upstream Marauder updates. Calls to our modules (e.g. `DaemonLink_NFC`) happen here and stream their output to the Serial terminal.

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
