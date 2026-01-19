#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// WiFi Configuration
// ============================================================================
#define WIFI_SSID "CTNF-Universal"
#define WIFI_PASSWORD "9868164136"

// ============================================================================
// Backend API Configuration
// ============================================================================
#define API_BASE_URL "https://sunilperformancetracker-production.up.railway.app"
#define API_ENDPOINT "/api/walks/upload"
#define DEVICE_ID "DOG_WALKER_001"

// ============================================================================
// LilyGo T-A7670G R2 Pin Definitions
// ============================================================================

// Board Power - Keep HIGH to stay powered when USB disconnected
#define BOARD_POWERON_PIN   12

// Modem (A7670G) UART Pins
#define MODEM_TX_PIN        26
#define MODEM_RX_PIN        27
#define MODEM_DTR_PIN       25
#define MODEM_PWRKEY_PIN    4
#define MODEM_POWER_ON_PIN  12
#define MODEM_RESET_PIN     5
#define MODEM_RING_PIN      33

// Modem Settings
#define MODEM_BAUDRATE      115200
#define MODEM_RESET_LEVEL   HIGH

// GPS (L76K) - Dedicated UART, NOT through modem!
#define GPS_TX_PIN          21
#define GPS_RX_PIN          22
#define GPS_PPS_PIN         23
#define GPS_WAKEUP_PIN      19
#define GPS_BAUDRATE        9600

// SD Card SPI Pins
#define SD_MISO_PIN         2
#define SD_MOSI_PIN         15
#define SD_SCK_PIN          14
#define SD_CS_PIN           13

// Battery ADC
#define BAT_ADC_PIN         35

// ============================================================================
// I2C Configuration (Shared by ADXL345 and OLED)
// NOTE: Moved from GPIO 21/22 to avoid conflict with GPS L76K
// ============================================================================
#define I2C_SDA_PIN         32
#define I2C_SCL_PIN         33

// ADXL345 Accelerometer
#define ADXL345_ADDRESS     0x53  // SDO connected to GND

// OLED SH1106 Display (1.3" 128x64)
// Note: 1.3" OLEDs use SH1106 controller, NOT SSD1306
// Using U8g2 library for proper SH1106 support
#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_ADDRESS        0x3C  // Common I2C address for SH1106
#define OLED_RESET          -1    // No reset pin (share with ESP32 reset)

// Activity Detection Thresholds
#define ACTIVITY_THRESHOLD  2.0    // m/s² - threshold for detecting movement
#define INACTIVITY_TIMEOUT  30000  // ms - time before marking inactive

// ============================================================================
// GPS Configuration
// ============================================================================
#define GPS_UPDATE_INTERVAL 1000   // ms between GPS reads
#define GPS_MIN_SATELLITES  4      // Minimum satellites for valid fix

// ============================================================================
// Data Logging Configuration
// ============================================================================
#define LOG_FILE_PREFIX     "/walks/walk_"
#define LOG_INTERVAL        1000   // ms between log entries
#define MAX_LOG_ENTRIES     86400  // Max entries per walk (24h at 1/sec)

// ============================================================================
// Walk Detection Settings
// ============================================================================
#define WALK_START_SPEED    0.5    // km/h - minimum speed to start walk
#define WALK_END_TIMEOUT    300000 // ms (5 min) - inactivity before ending walk
#define MIN_WALK_DURATION   60000  // ms (1 min) - minimum walk duration to save

// ============================================================================
// Upload Settings
// ============================================================================
#define WIFI_CONNECT_TIMEOUT 30000  // ms to wait for WiFi connection
#define UPLOAD_RETRY_COUNT   3      // Number of upload retries
#define UPLOAD_RETRY_DELAY   5000   // ms between retries

// ============================================================================
// Debug Settings
// ============================================================================
#define DEBUG_SERIAL        Serial
#define DEBUG_BAUDRATE      115200
#define ENABLE_DEBUG        true

#if ENABLE_DEBUG
  #define DEBUG_PRINT(x)    DEBUG_SERIAL.print(x)
  #define DEBUG_PRINTLN(x)  DEBUG_SERIAL.println(x)
  #define DEBUG_PRINTF(...) DEBUG_SERIAL.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#endif // CONFIG_H
