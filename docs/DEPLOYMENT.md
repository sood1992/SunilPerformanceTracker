# Dog Walker GPS Tracker - Deployment Guide

Complete step-by-step instructions for deploying the entire Dog Walker GPS Tracker system.

## Table of Contents

1. [Hardware Setup](#1-hardware-setup)
2. [Firmware Deployment](#2-firmware-deployment)
3. [Backend Deployment (Railway)](#3-backend-deployment-railway)
4. [Frontend Deployment (Vercel/Railway)](#4-frontend-deployment)
5. [Testing the System](#5-testing-the-system)
6. [Troubleshooting](#6-troubleshooting)

---

## 1. Hardware Setup

### Required Components

| Component | Quantity | Purpose |
|-----------|----------|---------|
| LilyGo T-A7670G R2 | 1 | Main board (ESP32 + 4G + GPS) |
| ADXL345 Breakout Board | 1 | Activity detection |
| MicroSD Card (8GB+) | 1 | Local data storage |
| 18650 Battery | 1 | Power supply |
| Jumper Wires | 6 | Connections |

### Wiring Diagram

Connect the ADXL345 to the LilyGo T-A7670G R2:

```
ADXL345          LilyGo T-A7670G R2
-------          ------------------
VCC     ───────► 3.3V
GND     ───────► GND
SDA     ───────► GPIO 21
SCL     ───────► GPIO 22
CS      ───────► 3.3V (for I2C mode)
SDO     ───────► GND (I2C address 0x53)
```

### Hardware Checklist

- [ ] Insert SIM card (data plan required for 4G, optional for WiFi-only mode)
- [ ] Insert formatted MicroSD card (FAT32)
- [ ] Connect ADXL345 accelerometer
- [ ] Insert 18650 battery
- [ ] Verify all connections are secure

---

## 2. Firmware Deployment

### Prerequisites

1. Install [PlatformIO IDE](https://platformio.org/install/ide) (VS Code extension recommended)
2. Install USB-to-Serial drivers for ESP32 (usually automatic)

### Step-by-Step Firmware Upload

#### Step 1: Configure WiFi and API Settings

Edit `firmware/include/config.h`:

```cpp
// Update these values:
#define WIFI_SSID "YOUR_HOME_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Update after deploying backend:
#define API_BASE_URL "https://your-app-name.railway.app"
#define DEVICE_ID "DOG_WALKER_001"  // Unique ID for this device
```

#### Step 2: Open Project in PlatformIO

1. Open VS Code
2. Click PlatformIO icon in sidebar
3. Click "Open Project"
4. Navigate to `firmware/` folder
5. Click "Open"

#### Step 3: Build the Firmware

```bash
# From command line (alternative to IDE)
cd firmware
pio run
```

Or click the checkmark (✓) button in PlatformIO toolbar.

#### Step 4: Upload to Device

1. Connect LilyGo board via USB
2. **Important**: Remove SD card during upload (GPIO 2 conflict)
3. Click Upload button (→) in PlatformIO
4. Wait for "SUCCESS" message

```bash
# Command line alternative
pio run --target upload
```

#### Step 5: Monitor Serial Output

1. Click Serial Monitor button in PlatformIO
2. Set baud rate to 115200
3. You should see:
   ```
   =================================
   Dog Walker GPS Tracker
   =================================

   Initializing SD card...
   SD card initialized successfully!
   Initializing ADXL345 accelerometer...
   ADXL345 initialized successfully!
   Initializing A7670G modem...
   Modem responded!
   Initializing GPS...
   GPS powered on
   Connecting to WiFi...
   WiFi connected!

   =================================
   Initialization Complete!
   =================================
   ```

---

## 3. Backend Deployment (Railway)

### Prerequisites

1. Create a [Railway](https://railway.app) account
2. Install [Railway CLI](https://docs.railway.app/develop/cli) (optional)

### Step-by-Step Backend Deployment

#### Step 1: Create New Project

1. Go to [Railway Dashboard](https://railway.app/dashboard)
2. Click "New Project"
3. Select "Deploy from GitHub repo"
4. Connect your GitHub account if needed
5. Select your repository

#### Step 2: Add PostgreSQL Database

1. In your project, click "New"
2. Select "Database" → "PostgreSQL"
3. Railway will provision a PostgreSQL instance
4. The `DATABASE_URL` will be automatically available

#### Step 3: Configure Backend Service

1. Click "New" → "GitHub Repo"
2. Select your repository
3. Set the **Root Directory** to `backend`
4. Railway will auto-detect the Dockerfile

#### Step 4: Set Environment Variables

In the backend service settings, add:

| Variable | Value |
|----------|-------|
| `NODE_ENV` | `production` |
| `FRONTEND_URL` | `https://your-frontend-url.vercel.app` (update after frontend deploy) |

The `DATABASE_URL` is automatically set by Railway.

#### Step 5: Deploy

1. Railway will automatically build and deploy
2. Click "Generate Domain" to get a public URL
3. Note your backend URL: `https://your-app.railway.app`

#### Step 6: Verify Deployment

```bash
curl https://your-app.railway.app/health
# Should return: {"status":"ok","timestamp":"...","version":"1.0.0"}
```

---

## 4. Frontend Deployment

### Option A: Deploy on Vercel (Recommended)

#### Step 1: Create Vercel Account

1. Go to [Vercel](https://vercel.com)
2. Sign up with GitHub

#### Step 2: Import Project

1. Click "Add New..." → "Project"
2. Import from GitHub
3. Select your repository

#### Step 3: Configure Build Settings

| Setting | Value |
|---------|-------|
| Framework Preset | Vite |
| Root Directory | `frontend` |
| Build Command | `npm run build` |
| Output Directory | `dist` |

#### Step 4: Set Environment Variables

| Variable | Value |
|----------|-------|
| `VITE_API_URL` | `https://your-backend.railway.app` |

#### Step 5: Deploy

1. Click "Deploy"
2. Wait for build to complete
3. Note your frontend URL

### Option B: Deploy on Railway

1. Add another service to your Railway project
2. Set Root Directory to `frontend`
3. Add environment variable: `VITE_API_URL=https://your-backend.railway.app`
4. Generate domain

---

## 5. Testing the System

### Test 1: Backend API

```bash
# Health check
curl https://your-backend.railway.app/health

# List walks (should be empty initially)
curl https://your-backend.railway.app/api/walks

# Check stats
curl https://your-backend.railway.app/api/stats/summary
```

### Test 2: Frontend Dashboard

1. Open your frontend URL in a browser
2. Dashboard should load (empty state initially)
3. Stats cards should show zeros

### Test 3: Device Upload Simulation

```bash
# Simulate a walk upload
curl -X POST https://your-backend.railway.app/api/walks/upload \
  -H "Content-Type: application/json" \
  -d '{
    "deviceId": "TEST_DEVICE",
    "startTime": 1704067200,
    "startLat": 40.7128,
    "startLon": -74.0060,
    "points": [
      {"t": 0, "lat": 40.7128, "lon": -74.0060, "spd": 4.5},
      {"t": 60, "lat": 40.7135, "lon": -74.0055, "spd": 5.2},
      {"t": 120, "lat": 40.7142, "lon": -74.0050, "spd": 4.8}
    ]
  }'
```

### Test 4: Device End-to-End

1. Power on the LilyGo device with firmware
2. Wait for GPS fix (may take 1-2 minutes outdoors)
3. Walk around with the device
4. Stop walking for 5 minutes (walk auto-ends)
5. Ensure device is in WiFi range
6. Check frontend dashboard for uploaded walk

---

## 6. Troubleshooting

### Firmware Issues

| Problem | Solution |
|---------|----------|
| Upload fails | Remove SD card, try different USB cable |
| "ADXL345 not found" | Check I2C wiring (SDA→21, SCL→22) |
| "Modem not responding" | Check battery charge, wait 5 seconds after power on |
| "GPS: No fix" | Move outdoors, wait 1-2 minutes |
| "SD card failed" | Format card as FAT32, check connections |

### Backend Issues

| Problem | Solution |
|---------|----------|
| Database connection error | Check `DATABASE_URL` is set correctly |
| CORS errors | Update `FRONTEND_URL` in Railway variables |
| 500 errors | Check Railway logs for stack trace |

### Frontend Issues

| Problem | Solution |
|---------|----------|
| "Failed to fetch" | Verify `VITE_API_URL` is correct |
| Map not loading | Check Leaflet CSS is included |
| Empty dashboard | Check backend is running, test API endpoints |

### Device-to-Cloud Issues

| Problem | Solution |
|---------|----------|
| Walks not uploading | Check WiFi credentials in config.h |
| Partial uploads | Increase timeout values |
| Data on SD but not uploading | Check API_BASE_URL in config.h |

---

## Quick Reference: URLs to Update

After deployment, update these values:

### In `firmware/include/config.h`:
```cpp
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"
#define API_BASE_URL "https://your-backend.railway.app"
```

### In Railway Backend:
```
FRONTEND_URL=https://your-frontend.vercel.app
```

### In Vercel/Railway Frontend:
```
VITE_API_URL=https://your-backend.railway.app
```

---

## Cost Estimates

| Service | Free Tier | Paid |
|---------|-----------|------|
| Railway Backend | $5/month credit | ~$5-10/month |
| Railway PostgreSQL | Included | Included |
| Vercel Frontend | Free | Free for personal use |
| **Total** | **~Free to $5/month** | **~$5-10/month** |

---

## Architecture Overview

```
┌─────────────┐     WiFi      ┌─────────────┐
│   Device    │──────────────►│   Backend   │
│  (ESP32 +   │   POST /api   │  (Railway)  │
│   GPS +     │   /walks/     │             │
│  ADXL345)   │   upload      │  PostgreSQL │
└─────────────┘               └──────┬──────┘
      │                              │
      │ SD Card                      │ REST API
      │ (offline                     │
      │  storage)                    ▼
      │                       ┌─────────────┐
      │                       │  Frontend   │
      │                       │  (Vercel)   │
      │                       │             │
      │                       │  Dashboard  │
      │                       │    + Map    │
      │                       └─────────────┘
```

---

## Next Steps

1. **Add multiple devices**: Change `DEVICE_ID` in config.h for each device
2. **Set up alerts**: Add email/push notifications for walk completion
3. **Add authentication**: Secure the API with JWT tokens
4. **Mobile app**: Build a React Native app using the same API
