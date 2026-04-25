# DaemonLink

> Headless multiprotocol pentesting daemon for ESP32-S3, controlled from a phone over USB-C via the Web Serial API.

DaemonLink turns an ESP32-S3 into a tactical, screenless pentesting tool. The hardware acts as a **daemon**: a C++ backend (built on a fork of [ESP32 Marauder](https://github.com/justcallmecoco/esp32_marauder)) that listens on the native USB serial port and waits for commands. The user interface is a lightweight **PWA** running in Chrome on Android, talking to the device over a wired USB-C link through the browser's native [Web Serial API](https://developer.mozilla.org/docs/Web/API/Web_Serial_API). No companion app, no Bluetooth pairing, no Wi-Fi handshake — plug, grant permission, operate.

## Why headless?

Traditional pentesting hardware ships with displays, buttons and rotary encoders. Those parts add cost, fragility and battery drain, and they constrain UX to whatever fits on a 1.3" screen. DaemonLink moves the entire UI into the phone the operator already carries, while keeping the radio work on dedicated, isolated hardware. The split is intentional:

- **ESP32-S3** does the timing-sensitive RF work (Wi-Fi, Bluetooth, NFC, IR).
- **Phone (PWA)** does presentation, history, exports and ergonomics.

## Architecture

```
+---------------------+        USB-C        +-----------------------------+
|  Android / Chrome   |  <-------------->   |   ESP32-S3 (DaemonLink)     |
|  PWA (HTML+JS)      |    Web Serial API   |   FreeRTOS dual-core        |
|  - Console I/O      |   115200 8N1 CDC    |   - Marauder core (Wi-Fi/BT)|
|  - Quick commands   |                     |   - Custom CLI hooks        |
|  - Channel parser   |                     |   - NFC / IR modules        |
+---------------------+                     +-----------------------------+
```

The backend exposes a CLI on `Serial`. Built-in Marauder commands (`scanap`, `sniffraw`, …) keep working; DaemonLink injects custom verbs (`nfc_read`, `ir_capture`, …) into Marauder's command parser and dispatches them to isolated modules. RF attacks and serial I/O run on separate FreeRTOS tasks so the CLI never blocks under load.

## Hardware

| Component | Purpose | Interface | Pins |
|-----------|---------|-----------|------|
| ESP32-S3 DevKitC-1 | MCU + native USB-C CDC | USB | — |
| PN532 V3 | NFC / RFID (ISO14443A, Mifare) | I²C | SDA=GPIO8, SCL=GPIO9 |
| VS1838B | IR receiver | Digital | GPIO4 |
| IR LED | IR transmitter | Digital (PWM) | GPIO5 |

The ESP32-S3's two USB-C ports are leveraged: one is the native USB CDC used by the PWA, the other is for power / programming.

## Software stack

- **Backend**: C++17, [PlatformIO](https://platformio.org/) on the Arduino-ESP32 framework, fork of ESP32 Marauder built with `HEADLESS_MODE=1` (no display, no buttons, no UI).
- **NFC**: [Adafruit PN532](https://github.com/adafruit/Adafruit-PN532).
- **IR**: [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266).
- **Frontend**: vanilla HTML/CSS/JS, single-file PWA, dark terminal theme, no build step, no framework.

## Repository layout

```
DaemonLink/
├── platformio.ini            # build configuration (headless flags + libs)
├── src/
│   ├── DaemonLink_NFC.h      # NFC module API
│   └── DaemonLink_NFC.cpp    # PN532 driver wrapper
├── include/                  # shared headers (CLI hooks, etc.)
└── web/                      # PWA frontend (Web Serial API client)
```

## Getting started

### 1. Flash the firmware

Requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).

```bash
git clone https://github.com/FelipeCupito/daemonlink.git
cd daemonlink
pio run -t upload
pio device monitor
```

### 2. Open the PWA on your phone

Open `web/index.html` in Chrome on Android (or serve it from any HTTPS host — Web Serial requires a secure context). Plug the ESP32-S3 into the phone via USB-C, hit **Connect**, and pick the device from the browser's port picker.

### 3. Send a command

Type `nfc_read` and hover a Mifare card over the PN532. The UID streams back into the console:

```
[NFC] PN532 OK, firmware=0x32010607
[NFC] Waiting for ISO14443A tag (timeout=1000ms)...
[NFC] UID len=4 bytes: a3:1f:bc:0e
[NFC] type=Mifare Classic (4-byte UID)
```

## Roadmap

- [x] PlatformIO scaffolding for ESP32-S3 in headless mode
- [x] PN532 NFC module (Mifare UID read)
- [ ] CLI injection into Marauder's parser (`nfc_read`, `ir_capture`, …)
- [ ] IR capture / replay module
- [ ] PWA frontend (console + quick commands + channel multiplexing)
- [ ] Structured JSON framing on the serial channel
- [ ] BadUSB / HID payload module

## Legal

DaemonLink is intended **exclusively** for authorized security testing, education, CTFs, and defensive research. Use it only on systems and hardware you own or have explicit written permission to assess. The authors take no responsibility for misuse.

## License

TBD.
