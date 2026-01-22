#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// LilyGo T-A7670G R2 Pin Definitions
// ============================================================================

// Board Power
#define PIN_BOARD_PWR 12

// Modem (A7670G) - Not prioritized for this phase, but pins defined
#define PIN_MODEM_TX 26
#define PIN_MODEM_RX 27
#define PIN_MODEM_PWR 4
#define PIN_MODEM_PON 12
#define PIN_MODEM_RST 5
// Pin 33 is used by I2C SCL, so we disable RING for now
#define PIN_MODEM_RING -1

// GPS (L76K) - Dedicated UART
#define PIN_GPS_TX 21
#define PIN_GPS_RX 22
#define PIN_GPS_PPS 23
#define PIN_GPS_WAKEUP 19
#define GPS_BAUDRATE 9600

// SD Card (SPI)
#define PIN_SD_MISO 2
#define PIN_SD_MOSI 15
#define PIN_SD_SCK 14
#define PIN_SD_CS 13

// I2C (ADXL345) - Validated from legacy code
#define PIN_I2C_SDA 32
#define PIN_I2C_SCL 33
#define ADXL345_ADDR 0x53

// Button (Shared with DTR? Legacy uses 18)
#define PIN_BUTTON 18

// ============================================================================
// Logic Thresholds
// ============================================================================

// Walk Detection
#define THRESHOLD_SPEED_KMPH 0.5   // Minimum speed to consider moving
#define THRESHOLD_ACCEL_G 0.15     // ~1.5 m/s^2 deviation roughly (0.15g)
#define TIMEOUT_WALK_END_MS 300000 // 5 minutes inactivity

// Geofencing (Simple Home Location)
// Example: New Delhi
#define HOME_LAT 28.5365
#define HOME_LON 77.2546
#define HOME_RADIUS_M 100.0

// WiFi Config
extern const char *WIFI_SSID;
extern const char *WIFI_PASS;
extern const char *API_URL;

#endif // CONFIG_H
