# Tank is still in working progress

> A Flipper Zero-inspired portable hacking & RF toolkit built on the ESP32. Scan WiFi, sniff BLE devices, detect trackers, capture 2.4GHz RF packets, and execute BadUSB payloads wirelessly through a Raspberry Pi Pico — all from a compact handheld device with an OLED display.

---

## Features

- **WiFi** — Scan all networks or filter open-only, connect to a configured AP, and save results to SD
- **BLE Scanner** — Discover nearby Bluetooth Low Energy devices; auto-detects AirTags, Tile, and Samsung SmartTags
- **Pico Commander** — Wirelessly connect to a Raspberry Pi Pico running a BLE UART service and execute HID payloads (BLE Rubber Ducky)
- **RF24 / 2.4GHz** — Spectrum scanning, packet capture, and replay using an nRF24L01+ module
- **SD Card** — Browse, view, and delete saved scan data; store and stream DuckyScript payloads
- **Status Screen** — Live system info: WiFi, BLE, Pico connection, nRF24, SD, CPU freq, and free RAM

---

## Hardware

| Component | Details |
|-----------|---------|
| **MCU** | ESP32 (WROOM / DevKit) |
| **Display** | SSD1306 128×64 OLED (I²C) |
| **RF Module** | nRF24L01+ (SPI via VSPI) |
| **Storage** | MicroSD card (SPI) |
| **Payload Runner** | Raspberry Pi Pico W (BLE UART HID) |
| **Buttons** | 4× momentary push buttons (Up / Down / Select / Back) |

### Pin Mapping

**OLED (I²C)**
| Signal | GPIO |
|--------|------|
| SDA | 21 |
| SCL | 22 |

**nRF24L01+ (VSPI)**
| Signal | GPIO |
|--------|------|
| CE | 4 |
| CSN | 5 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |

**SD Card (SPI — shared with nRF24)**
| Signal | GPIO |
|--------|------|
| CS | 15 |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| Detect | 34 |

**Buttons (INPUT_PULLUP — active LOW)**
| Button | GPIO |
|--------|------|
| Up | 32 |
| Down | 33 |
| Select | 25 |
| Back | 26 |

---

## Software Dependencies

Install via Arduino Library Manager or PlatformIO:

| Library | Purpose |
|---------|---------|
| `U8g2` | OLED display driver |
| `NimBLE-Arduino` | Bluetooth Low Energy (scan + client) |
| `RF24` | nRF24L01+ radio |
| `SD` | MicroSD file system |
| `Wire` / `SPI` | Built-in ESP32 core libraries |

---

## Getting Started

1. **Clone this repo** and open the `.ino` file in Arduino IDE (or PlatformIO).
2. **Install dependencies** listed above.
3. **Set your WiFi credentials** in the source if you want the Connect feature:
   ```cpp
   const char* WIFI_SSID     = "YourSSID";
   const char* WIFI_PASSWORD = "YourPassword";
   ```
4. **Flash to your ESP32.**
5. On first boot, Tank initializes BLE, nRF24, and SD card, then drops you into the main menu.

---

## SD Card Layout

Tank automatically creates the following directory structure:

```
/tank/
├── wifi/       ← Saved WiFi scan results
├── ble/        ← Saved BLE scan results
├── rf24/       ← Saved RF24 capture data
└── ducky/      ← DuckyScript payload files (.txt / .duck)
```

Place DuckyScript payloads in `/tank/ducky/` to run them via **Pico Control → Run Payload**.

---

## Pico BLE Rubber Ducky

Tank can pair with a Raspberry Pi Pico running a BLE UART HID service and send keystrokes wirelessly.

**Pico firmware requirements:**
- Device name must be `PicoDucky-<AUTH_CODE>` (e.g. `PicoDucky-A1B2`)
- Exposes BLE UART service: `0000FFE0-0000-1000-8000-00805F9B34FB`
- RX characteristic: `0000FFE2-0000-1000-8000-00805F9B34FB`

**Supported commands sent from Tank to the Pico:**

| Command | Description |
|---------|-------------|
| `AUTH:<code>` | Authenticate the session |
| `TYPE:<text>` | Type a string |
| `COMBO:<keys>` | Send a key combination (e.g. `GUI+r`) |
| `DELAY:<ms>` | Wait for a specified number of milliseconds |
| `ENTER` / `BACKSPACE` / `TAB` / `ESC` | Send individual keys |
| `PAYLOAD:START:<name>` | Begin streaming a payload |
| `LINE:<content>` | Send one line of a payload |
| `PAYLOAD:END` | Signal end of payload |
| `PING` / `STATUS` | Health check |

---

## Navigation

| Button | Action |
|--------|--------|
| **Up / Down** | Navigate menu or scroll results |
| **Select** | Confirm / enter submenu / run action |
| **Back** | Return to previous menu |

---

## Menu Structure

```
Main Menu
├── 1. WiFi
│   ├── Scan All
│   ├── Open Only
│   ├── Connect
│   └── Save Last Scan
├── 2. BLE Scan
│   ├── Scan Devices
│   ├── Find Trackers
│   └── Save Last Scan
├── 3. Pico Control
│   ├── Connect to Pico
│   ├── Disconnect
│   ├── Type Text
│   ├── Send Combo
│   ├── Run Payload
│   ├── Send Command
│   └── Status
├── 4. RF24
│   ├── Scan Spectrum
│   ├── Capture Pkts
│   ├── Replay Pkts
│   └── Save Capture
├── 5. SD Card
│   ├── Browse Files
│   ├── Card Info
│   └── Delete File
└── 6. Status
```

---

## Tracker Detection

The BLE scanner automatically identifies common tracking devices by manufacturer data and service UUIDs:

| Tracker | Detection Method |
|---------|-----------------|
| Apple AirTag | Manufacturer ID `0x004C` + payload bytes `0x12 0x19` |
| Tile | Manufacturer ID `0x00E0` or service UUID `FEED` |
| Samsung SmartTag | Manufacturer ID `0x0075` or service UUID `FD5A` |

Detected trackers are flagged with `!` in scan results.

---

## Version

**Tank v3.0** — Pico Commander Edition

---

## Disclaimer

This project is intended for **educational and authorized security research purposes only**. Do not use Tank on networks, devices, or systems you do not own or have explicit permission to test. The authors are not responsible for misuse.
