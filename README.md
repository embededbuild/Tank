# Tank v7 — ESP32 Wireless Research Tool

> A portable, handheld wireless analysis platform built on the ESP32 with a 128×64 OLED display, 4-button navigation, nRF24L01+ radio, and SD card logging.

---

## Features

| Module | Capability |
|---|---|
| **WiFi Scanner** | Scan all networks or open-only; logs SSID, BSSID, channel, RSSI, encryption |
| **BLE Scanner** | Discover BLE devices; flags Apple / Tile / Samsung trackers by manufacturer ID |
| **RF24 Tools** | Spectrum analysis across all 125 channels, packet capture, and packet replay |
| **Probe Sniffer** | Promiscuous-mode channel-hopping capture of 802.11 probe requests |
| **Device Spoofing** | Create, activate, and manage MAC/IP spoof profiles stored on SD |
| **SD Card Browser** | Browse, view, and delete saved scan files from the onboard SD |
| **Status Screen** | Live heap, packet counts, and peripheral health |

---

## Hardware

### Bill of Materials

| Component | Notes |
|---|---|
| ESP32 dev board | Any standard 38-pin module |
| SSD1306 OLED 128×64 | I²C, connected on pins 21/22 |
| nRF24L01+ module | SPI via VSPI bus |
| MicroSD card module | Shares VSPI with nRF24 |
| 4× tactile buttons | UP / DOWN / SELECT / BACK |
| MicroSD card | FAT32 formatted |

### Pin Map

```
OLED SDA  → GPIO 21
OLED SCL  → GPIO 22

SD  CS    → GPIO 15
SD  SCK   → GPIO 18
SD  MISO  → GPIO 19
SD  MOSI  → GPIO 23

nRF24 CE  → GPIO 4
nRF24 CSN → GPIO 5

BTN UP    → GPIO 32
BTN DOWN  → GPIO 33
BTN SELECT→ GPIO 25
BTN BACK  → GPIO 26
```

> **Note:** The nRF24L01+ and SD card share the VSPI bus. The SD chip-select is pulled HIGH before nRF24 operations to prevent bus contention.

---

## Software Dependencies

Install these libraries via the Arduino Library Manager or PlatformIO:

```
U8g2          — OLED display driver
NimBLE-Arduino — Lightweight BLE stack
RF24          — nRF24L01+ driver
SD (built-in) — SD card filesystem
FS (built-in) — ESP32 filesystem abstraction
WiFi (built-in)— ESP32 WiFi stack
```

---

## Building & Flashing

1. Open the `.ino` file in Arduino IDE.
2. Select your ESP32 board and COM port.
3. Install all dependencies listed above.
4. Flash at 115200 baud.

---

## SD Card Structure

The firmware auto-creates the following directory tree on first boot:

```
/tank/
├── wifi/       — WiFi scan results
├── ble/        — BLE scan results
├── rf24/       — RF24 packet captures
├── probes/     — Probe request captures
└── spoof/      — Spoof profiles (profiles.txt)
```

All log files are timestamped using uptime (`HHhMMmSSs` format).

---

## Navigation

```
[ UP ]   / [ DOWN ]  — scroll menu or results
[ SELECT ]           — confirm / enter submenu / run action
[ BACK ]             — return to previous menu / stop active scan
```

During **Probe Sniffing**, press `BACK` to stop the capture and display results.  
During **RF24 Replay**, press `BACK` to abort mid-replay.

---

## Menu Structure

```
Main Menu
├── 1. WiFi Scan
│   ├── Scan All Networks
│   ├── Scan Open Only
│   └── Save Last Scan
├── 2. BLE Scan
│   ├── Scan Devices
│   ├── Find Trackers
│   └── Save Scan
├── 3. RF24 Tools
│   ├── Spectrum Analyze   (125 channels, 2 s each)
│   ├── Capture Packets    (up to 200 packets)
│   ├── Replay Packets     (30 s window)
│   └── Save Capture
├── 4. Probe Sniff
│   ├── Start Sniffing     (channel-hopping 1–11)
│   ├── Stop Sniffing
│   └── Save Probes
├── 5. Device Spoof
│   ├── Create Profile     (random MAC + IP)
│   ├── Activate Profile
│   └── Delete Profile
├── 6. SD Card
│   ├── Browse Files
│   ├── Card Info
│   └── Delete File
└── 7. Status
```

---

## Spoof Profiles

Profiles are stored in `/tank/spoof/profiles.txt` as pipe-delimited records:

```
ProfileName|02:XX:XX:XX:XX:XX|192.168.1.XXX|0
```

Fields: `name | mac | ip | active (0/1)`

New profiles are generated with a locally-administered MAC address (first octet `02`) and a random host IP in the `192.168.1.0/24` range.

---

## Boot Sequence

On power-up the device runs a glitchy animated splash, then initializes peripherals in order:

1. BLE stack (`NimBLE`)
2. nRF24L01+ radio
3. SD card + directory tree
4. Spoof profile loader
5. WiFi disabled (conserves power until needed)

---

## Legal Notice

This tool is intended for **authorized security research, educational use, and testing on networks and devices you own or have explicit permission to test.**

Unauthorized interception of wireless communications, unauthorized access to computer networks, and MAC address spoofing on networks without permission may violate local laws including (but not limited to) the Computer Fraud and Abuse Act (US), the Computer Misuse Act (UK), and equivalent legislation in other jurisdictions.

**Use responsibly. The authors assume no liability for misuse. Aka you're stupidity is not my fault nor really anyone but yourself. Don't be a fucktard**

---

## License

MIT — see `LICENSE` file for details.
## Disclaimer

This project is intended for **educational and authorized security research purposes only**. Do not use Tank on networks, devices, or systems you do not own or have explicit permission to test. The authors are not responsible for misuse. Aka you're stupidity is not my fault nor really anyone but yourself. Don't be a fucktard
