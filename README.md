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
- **Daily Goal Tracking** - Set exercise goals for your dog
- **30-Day Dashboard** - Track walker performance over time
- **Activity Breakdown** - Walking vs Running vs Resting time

## Walk Detection Logic

### How Walk Start is Detected

A walk is considered **started** when BOTH conditions are met:

1. **GPS Speed > 0.5 km/h** - The device is moving at a walking pace
2. **Accelerometer Shows Movement** - The magnitude deviation from gravity exceeds the threshold

The accelerometer magnitude is calculated as:
```
magnitude = √(x² + y² + z²)
```

At rest, this equals ~9.8 m/s² (gravity). Movement is detected when:
- `|magnitude - 9.8| > 0.5 m/s²` (sitting threshold)
- Walking: deviation > 1.5 m/s²
- Running: deviation > 3.0 m/s²

### How Walk End is Detected

A walk is considered **ended** when:

1. **5 minutes of inactivity** - No significant movement detected
2. **GPS speed near zero** - Speed < 0.5 km/h for sustained period
3. **Accelerometer stable** - Magnitude stays near gravity (~9.8 m/s²)

The firmware uses a state machine:
```
IDLE → WALKING → IDLE
       ↓
   [5 min timeout with no movement]
       ↓
     END WALK
```

### Activity Classification

During a walk, each GPS point is classified based on accelerometer data:

| Activity | Accel Magnitude Deviation | Speed |
|----------|---------------------------|-------|
| **Sitting/Resting** | < 0.5 m/s² | < 0.5 km/h |
| **Walking** | 0.5 - 1.5 m/s² | 2-4 km/h |
| **Fast Walking** | 1.5 - 3.0 m/s² | 4-6 km/h |
| **Running** | > 3.0 m/s² | > 6 km/h |

## Accountability Features

### Walker Scoring System

Each walk receives a score (0-100) based on:

| Metric | Weight | Scoring |
|--------|--------|---------|
| **Duration** | 30% | 0 pts (<10min), 15 pts (10-20min), 30 pts (>30min) |
| **Distance** | 30% | 0 pts (<0.5km), 15 pts (0.5-1km), 30 pts (>1.5km) |
| **Activity Quality** | 25% | Based on walking % vs resting % |
| **Data Quality** | 15% | GPS point density and consistency |

### Daily Goals (Configured for Popcorn - Mini Poodle)

Based on mini-poodle exercise requirements:

| Goal | Target | Description |
|------|--------|-------------|
| **Total Minutes** | 60 min/day | Total walking time |
| **Active Minutes** | 40 min/day | Walking + running time |
| **Distance** | 2.5 km/day | Total distance covered |
| **Min Walks** | 2 walks/day | Separate walk sessions |
| **Max Rest %** | 30% | Maximum time spent sitting |

### Alerts

The system generates alerts for:

- 🏆 **Goal Met** - Daily exercise goal completed
- ⚠️ **Goal At Risk** - Evening approaching, goals not met
- 📢 **Too Much Resting** - Walk has excessive sitting time (>30%)
- 💡 **Streak Milestone** - 7-day or 30-day goal streak achieved

## API Endpoints

### Goals API

```
GET  /api/goals/profile     # Dog profile and exercise requirements
GET  /api/goals/today       # Today's progress and scores
GET  /api/goals/history     # 30-day history with streaks
GET  /api/goals/alerts      # Current alerts for owner/walker
```

### Accountability API

```
GET  /api/accountability/score/:walkId   # Individual walk score
GET  /api/accountability/report          # Weekly accountability report
GET  /api/accountability/alerts          # Walker alerts
```

## License

MIT License
