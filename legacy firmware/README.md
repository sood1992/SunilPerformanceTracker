# Production Firmware (Gold Master)

This directory contains the fully upgraded firmware for the Dog Walker Accountability Tool.

## Key Features
1. **Real-Time LTE**: Pushes data every second to Railway using `modem_handler`.
2. **GPS Atomic Time**: Uses satellite time for files (fixes 1970 date bug).
3. **SSL Bypass**: Works with Railway's HTTPS (`setInsecure` + `AT+CSSLCFG`).
4. **Hybrid Sync**: Auto-uploads bulk logs via WiFi when idle.

## Setup
1. Edit `src/main.cpp`: Set `WIFI_SSID`, `WIFI_PASS`, and `API_URL`.
2. Edit `src/config.h`: Set `HOME_LAT` / `HOME_LON`.
3. Flash using PlatformIO.
