# DaemonLink

> Headless multiprotocol pentesting daemon for ESP32-S3, controlled from a phone over USB-C via the Web Serial API.

**Live PWA**: [`https://daemonlink-541cd.web.app`](https://daemonlink-541cd.web.app) — installable on Android, works offline once installed.

DaemonLink turns an ESP32-S3 into a tactical, screenless pentesting tool. The hardware acts as a **daemon**: a C++ backend (built on a fork of [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder)) that listens on the native USB serial port and waits for commands. The user interface is a lightweight **PWA** running in Chrome on Android, talking to the device over a wired USB-C link through the browser's native [Web Serial API](https://developer.mozilla.org/docs/Web/API/Web_Serial_API). No companion app, no Bluetooth pairing, no Wi-Fi handshake — plug, grant permission, operate.

The PWA is hosted on Firebase and shipped through a GitHub Actions CI/CD pipeline that compiles the firmware on every push and refuses to deploy the frontend if the embedded build is broken — so the public console can never speak a protocol the firmware doesn't understand.

## Why headless?

Traditional pentesting hardware ships with displays, buttons and rotary encoders. Those parts add cost, fragility and battery drain, and they constrain UX to whatever fits on a 1.3" screen. DaemonLink moves the entire UI into the phone the operator already carries, while keeping the radio work on dedicated, isolated hardware. The split is intentional:

- **ESP32-S3** does the timing-sensitive RF work (Wi-Fi, Bluetooth, NFC, IR).
- **Phone (PWA)** does presentation, history, exports and ergonomics.

## Architecture

```
+---------------------+        USB-C        +-----------------------------+
|  Android / Chrome   |  <-------------->   |   ESP32-S3 (DaemonLink)     |
|  PWA (HTML+JS)      |    Web Serial API   |   FreeRTOS dual-core        |
|  - JSON event cards |   115200 8N1 CDC    |   - Marauder core (Wi-Fi/BT)|
|  - IR library panel |                     |   - NFC / IR modules        |
|  - Quick commands   |                     |   - LittleFS payload store  |
+---------------------+                     +-----------------------------+
```

The backend exposes a CLI on `Serial`. Built-in Marauder commands (`scanall`, `sniffraw`, …) keep working untouched; DaemonLink injects custom verbs (`nfc_read`, `ir_capture`, `ir_save`, `ir_play`, `fs_list`, …) into Marauder's command parser via a 4-line patch (0 modified lines, 0 deletions) and dispatches them to isolated modules. Heavy operations run on dedicated FreeRTOS tasks pinned to core 1 so the CLI never blocks under load.

Every event our modules emit is a single line of minified JSON terminated by `\n`. The PWA `JSON.parse`s those lines and renders structured cards; Marauder's native plain-text logs fall through to a coloured-text renderer.

## Hardware

| Component | Purpose | Interface | Pins |
|-----------|---------|-----------|------|
| ESP32-S3 DevKitC-1 | MCU + native USB-C CDC | USB | — |
| PN532 V3 | NFC / RFID (ISO14443A, Mifare) | I²C | SDA=GPIO8, SCL=GPIO9 |
| VS1838B | IR receiver | RMT (DMA) | GPIO4 |
| IR LED | IR transmitter | LEDC (PWM) | GPIO5 |
| *(reserved)* CC1101 / NRF24L01 | RF Sub-1GHz / 2.4GHz | SPI | SCK=12, MISO=13, MOSI=11 |

The ESP32-S3's two USB-C ports are leveraged: one is the native USB CDC used by the PWA, the other is for power / programming.

## Software stack

- **Backend**: C++17, [PlatformIO](https://platformio.org/) on the Arduino-ESP32 framework, fork of ESP32 Marauder built with a custom `MARAUDER_DAEMONLINK` target (no display, no buttons, no SD, no battery).
- **NFC**: [Adafruit PN532](https://github.com/adafruit/Adafruit-PN532).
- **IR (capture + replay)**: [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266).
- **Persistence**: [LittleFS](https://github.com/littlefs-project/littlefs) on a dedicated 384 KB flash partition, side-by-side with Marauder's SPIFFS settings store via a custom partition table.
- **JSON framing**: [ArduinoJson v7](https://arduinojson.org/) with stack-scoped `JsonDocument` lifetimes inside FreeRTOS tasks (no globals, no static buffers).
- **Frontend**: vanilla HTML/CSS/JS, single-file PWA, dark terminal theme, no build step, no framework. Service Worker for offline shell.
- **Hosting & CI/CD**: [Firebase Hosting](https://firebase.google.com/products/hosting) + GitHub Actions (firmware compile gate + auto-deploy + PR previews).

## Repository layout

```
DaemonLink/
├── platformio.ini                     # build configuration, libs, partition pointer
├── boards/daemonlink_partitions.csv   # custom partition table (SPIFFS + LittleFS coexisting)
├── lib/DaemonLink/                    # NFC, IR, FS, CLI shim, JSON helpers
├── external/ESP32Marauder/            # git submodule -> upstream Marauder fork
├── patches/marauder.patch             # 4-line idempotent injection
├── scripts/apply_patches.py           # PlatformIO pre-script + standalone CLI
├── web/                               # PWA frontend (index.html + sw.js + manifest)
├── firebase.json + .firebaserc        # Firebase Hosting config
├── requirements.txt                   # Python deps for the GitHub Actions runner
└── .github/workflows/main.yml         # CI/CD pipeline
```

## Getting started

### 1. Use the live PWA (no install needed for the firmware build)

If you just want to drive an already-flashed DaemonLink:

1. On Android Chrome, open **[`https://daemonlink-541cd.web.app`](https://daemonlink-541cd.web.app)**.
2. *(One-time, if Web Serial is hidden behind a flag)* enable `chrome://flags/#enable-experimental-web-platform-features` and restart Chrome.
3. Browser menu (⋮) → **Install app** / **Add to Home screen**. DaemonLink will live in your launcher as a standalone app and **work offline** thanks to the precached Service Worker shell — handy for fieldwork in airplane mode.
4. Plug the ESP32-S3 via USB-C → tap **Connect** → pick the CDC port from the browser picker.

### 2. Build and flash the firmware

Requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).

```bash
git clone https://github.com/FelipeCupito/daemonlink.git
cd daemonlink
git submodule update --init                 # pulls Marauder, non-recursive on purpose
```

> **First flash only — important.** The partition table changed in Phase G (LittleFS lives next to Marauder's SPIFFS). The bootloader caches the old layout, so a plain upload would land the firmware on top of stale partition metadata. Wipe the whole flash once:
>
> ```bash
> pio run -t erase    # one-time, only on the first flash with this firmware
> pio run -t upload
> pio device monitor  # optional — see the JSON event stream live
> ```
>
> Subsequent iterations are just `pio run -t upload` — no erase needed.

### 3. Send a command

Open the PWA, tap **Connect**, then either type into the command bar or use a Quick chip:

- `nfc_read` → present a Mifare card to the PN532; the UID streams back as a structured card.
- `ir_capture` → point a TV remote at the receiver; protocol/bits/hex are decoded and a `replay:` payload is emitted.
- Type a name in the IR-save row → **Save**. The **Library** panel opens with a `▶ Play` button per saved payload, ready to retransmit.

Sample event from the Serial stream:

```json
{"source":"nfc","event":"tag","type":"mifare_classic","uid":"a3:1f:bc:0e","uid_len":4}
{"source":"ir","event":"capture","protocol":"NEC","bits":32,"hex":"00FF629D","repeat":false,"replay":"NEC 32 0xff629d"}
```

## Deploying the PWA

Pushes to `main` automatically run the GitHub Actions pipeline:

1. **firmware** job — compiles `pio run` on Ubuntu, uploads `firmware.bin` as a 30-day artifact.
2. **deploy** job — only if the firmware compiles, ships `web/` to the Firebase live channel.
3. **preview** job — every pull request gets a throwaway preview URL (7-day TTL) commented on the PR.

To wire up Firebase locally one-time (only needed if cloning fresh):

```bash
firebase login
firebase init hosting:github           # auto-provisions FIREBASE_SERVICE_ACCOUNT_DAEMONLINK_541CD as a repo secret
firebase deploy --only hosting         # optional sanity-check deploy from your laptop
```

## Roadmap

- [x] PlatformIO scaffolding + headless `MARAUDER_DAEMONLINK` board target
- [x] CLI injection into Marauder's parser (4-line patch, zero footprint upstream)
- [x] PN532 NFC module (Mifare UID read)
- [x] IR capture / replay (RX + TX) with hex- and raw-format payloads
- [x] Single-file PWA (Web Serial console, channel-coloured cards)
- [x] PWA installable + offline (manifest + Service Worker)
- [x] Structured JSON framing on the serial channel (ArduinoJson v7, stack-scoped)
- [x] LittleFS persistence (`ir_save`, `ir_play`, `fs_list`)
- [x] Firebase Hosting + GitHub Actions CI/CD with PR previews
- [ ] `nfc_write` for Mifare Classic blocks
- [ ] RF Sub-1GHz module (CC1101) on the reserved SPI pins
- [ ] Service Worker "Update available — Reload" prompt promoted to a sticky banner
- [ ] Export / import the LittleFS payload library to a PC over Web Serial
- [ ] BadUSB / HID payload module

## Legal

DaemonLink is intended **exclusively** for authorized security testing, education, CTFs, and defensive research. Use it only on systems and hardware you own or have explicit written permission to assess. The authors take no responsibility for misuse.

## License

TBD.
