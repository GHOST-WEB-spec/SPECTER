# ⚡ SPECTER

A handheld ESP32-based cybersecurity research tool running JINX OS. Built from scratch — custom firmware, custom PCB, custom 3D printed enclosure.

## ✨ Features

**📡 WiFi**
- Network scanner with OUI vendor lookup and threat assessment
- Probe request sniffer
- Evil Twin access point with captive portal credential capture
- KARMA attack — automatically impersonates known networks

**🔴 IR**
- TV-B-Gone (26 codes, all major brands)
- IR blaster with capture and replay
- IR self-test

**📶 BLE**
- Scanner with Apple device detection
- Apple BLE proximity spam (6 payloads)

**🔜 Coming Soon**
- CC1101 sub-GHz scanner and replay
- NFC read/write/clone
- RFID read/clone
- BadUSB payloads
- Deauth and beacon flood

## 🔧 Hardware

- ESP32-WROVER on Freenove extension board
- 2.8" ILI9341 TFT touchscreen (240x320)
- VS1838B IR receiver + TSAL6400 IR LEDs
- CC1101 x2 (sub-GHz radios)
- PN532 NFC module
- RDM6300 RFID module
- LiPo 1000mAh + TP4056 charging
- Custom KiCad PCB — ordered from JLCPCB
- 3D printed enclosure — Fusion 360

## 🚀 Status

Firmware working. PCB designed and ready to order. Parts arriving shortly for final assembly.

## 🛠️ Build

Flash using Arduino IDE with ESP32 core v2.0.17. Partition scheme: Huge APP 3MB No OTA.
