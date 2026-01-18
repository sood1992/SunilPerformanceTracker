# Dog Walker GPS Tracker

A complete IoT solution for tracking dog walking accountability with GPS location, speed, time tracking, and route visualization.

## Hardware Requirements

- **LilyGo T-A7670G R2** (ESP32 with 4G LTE + GPS)
- **ADXL345** Accelerometer (for motion/activity detection)
- **MicroSD Card** (for local data storage)
- 18650 Battery (for portable power)

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        HARDWARE LAYER                           │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────┐  │
│  │  LilyGo T-A7670G │  │     ADXL345      │  │   MicroSD    │  │
│  │  R2 (ESP32+GPS)  │  │  Accelerometer   │  │     Card     │  │
│  └────────┬─────────┘  └────────┬─────────┘  └──────┬───────┘  │
│           │                     │                    │          │
│           └─────────────────────┼────────────────────┘          │
│                                 │                               │
└─────────────────────────────────┼───────────────────────────────┘
                                  │ WiFi Upload
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                        BACKEND (Railway)                        │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────┐  │
│  │   Express API    │  │    PostgreSQL    │  │    Redis     │  │
│  │   /api/walks     │  │   Walk Data      │  │   Sessions   │  │
│  └──────────────────┘  └──────────────────┘  └──────────────┘  │
└─────────────────────────────────┼───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                     FRONTEND (Vercel/Railway)                   │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────┐  │
│  │   React + Vite   │  │   Leaflet Maps   │  │   Charts     │  │
│  │   Dashboard      │  │  Route Display   │  │   Stats      │  │
│  └──────────────────┘  └──────────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Pin Configuration

### LilyGo T-A7670G R2 Default Pins

| Function | GPIO Pin | Notes |
|----------|----------|-------|
| **Modem UART** | | |
| MODEM_TX | 26 | ESP32 TX to Modem RX |
| MODEM_RX | 27 | ESP32 RX from Modem TX |
| MODEM_DTR | 25 | Data Terminal Ready |
| MODEM_PWRKEY | 4 | Power key control |
| MODEM_POWER_ON | 12 | Must be HIGH for power |
| MODEM_RESET | 5 | Reset pin (active HIGH) |
| MODEM_RING | 33 | Ring indicator |
| **SD Card (SPI)** | | |
| SD_MISO | 2 | Master In Slave Out |
| SD_MOSI | 15 | Master Out Slave In |
| SD_SCK | 14 | SPI Clock |
| SD_CS | 13 | Chip Select |
| **Battery ADC** | | |
| BAT_ADC | 35 | Battery voltage monitor |
| **I2C (for ADXL345)** | | |
| I2C_SDA | 21 | Data line |
| I2C_SCL | 22 | Clock line |

### ADXL345 Accelerometer Wiring

| ADXL345 Pin | Connect To | Notes |
|-------------|------------|-------|
| VCC | 3.3V | Power supply |
| GND | GND | Ground |
| SDA | GPIO 21 | I2C Data |
| SCL | GPIO 22 | I2C Clock |
| CS | 3.3V | HIGH for I2C mode |
| SDO | GND | I2C address 0x53 |

## Project Structure

```
├── firmware/               # ESP32 PlatformIO project
│   ├── src/
│   │   └── main.cpp       # Main firmware code
│   ├── include/
│   │   └── config.h       # Configuration file
│   └── platformio.ini     # PlatformIO configuration
├── backend/               # Node.js Express API
│   ├── src/
│   │   ├── index.js       # Entry point
│   │   ├── routes/        # API routes
│   │   └── models/        # Database models
│   ├── package.json
│   └── Dockerfile
├── frontend/              # React Vite dashboard
│   ├── src/
│   │   ├── App.jsx
│   │   └── components/
│   ├── package.json
│   └── Dockerfile
└── docs/                  # Documentation
    └── DEPLOYMENT.md      # Deployment guide
```

## Quick Start

See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for detailed deployment instructions.

## Features

- Real-time GPS tracking during walks
- Walking speed calculation
- Total distance tracking
- Route visualization on map
- Activity detection via accelerometer
- Offline data storage on SD card
- Automatic upload when WiFi available
- Walk history and statistics
- Battery level monitoring

## License

MIT License
