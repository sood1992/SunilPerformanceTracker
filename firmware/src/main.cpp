/**
 * Dog Walker GPS Tracker Firmware
 * Hardware: LilyGo T-A7670G R2 + ADXL345 Accelerometer
 *
 * GPS: L76K module on dedicated UART (GPIO 21/22)
 * Modem: A7670G for 4G (GPIO 26/27) - NOT used for GPS
 * Accel: ADXL345 on I2C (GPIO 32/33) - moved from 21/22
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPSPlus.h>
#include <Adafruit_ADXL345_U.h>
#include <ArduinoJson.h>
#include <time.h>
#include <stdarg.h>
#include <U8g2lib.h>
#include "config.h"

// ============================================================================
// Global Objects
// ============================================================================

HardwareSerial SerialAT(1);   // Modem on UART1
HardwareSerial SerialGPS(2);  // GPS L76K on UART2
TinyGPSPlus gps;
Adafruit_ADXL345_Unified* accel = nullptr;
SPIClass* sdSPI = nullptr;
// U8g2 display for SH1106 1.3" OLED (128x64) on hardware I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C* display = nullptr;

// ============================================================================
// State Variables
// ============================================================================

bool sdCardReady = false;
bool modemReady = false;
bool gpsEnabled = false;
bool accelReady = false;
bool displayReady = false;

struct GPSData {
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    double speed = 0;
    int satellites = 0;
    bool valid = false;
} currentGPS;

// Simple 1D Kalman Filter for GPS smoothing
class KalmanFilter {
public:
    double Q;  // Process noise covariance
    double R;  // Measurement noise covariance
    double P;  // Estimation error covariance
    double K;  // Kalman gain
    double X;  // State estimate
    bool initialized;

    KalmanFilter(double processNoise = 0.00001, double measurementNoise = 0.0001) {
        Q = processNoise;
        R = measurementNoise;
        P = 1.0;
        K = 0;
        X = 0;
        initialized = false;
    }

    double update(double measurement) {
        if (!initialized) {
            X = measurement;
            initialized = true;
            return X;
        }

        // Prediction update
        P = P + Q;

        // Measurement update
        K = P / (P + R);
        X = X + K * (measurement - X);
        P = (1 - K) * P;

        return X;
    }

    void reset() {
        initialized = false;
        P = 1.0;
    }
};

// Kalman filters for GPS coordinates
KalmanFilter kalmanLat(0.00001, 0.0005);   // Lat filter (tune R based on GPS noise)
KalmanFilter kalmanLon(0.00001, 0.0005);   // Lon filter
KalmanFilter kalmanSpeed(0.1, 1.0);         // Speed filter (more aggressive smoothing)

struct AccelData {
    float x = 0;
    float y = 0;
    float z = 0;
    float magnitude = 0;
    bool isMoving = false;
} currentAccel;

struct WalkSession {
    unsigned long startTime = 0;      // millis() for duration calculation
    unsigned long endTime = 0;        // millis() for duration calculation
    time_t startTimeUnix = 0;         // Unix timestamp for actual date/time
    double totalDistance = 0;
    double maxSpeed = 0;
    double avgSpeed = 0;
    int dataPoints = 0;
    bool isActive = false;
    String filename = "";
} currentWalk;

GPSData lastValidGPS;
unsigned long lastGPSUpdate = 0;
unsigned long lastLogTime = 0;
unsigned long lastActivityTime = 0;
unsigned long lastStatusPrint = 0;
unsigned long lastDisplayUpdate = 0;

// Step Counter Variables
unsigned long stepCount = 0;
unsigned long todayStepCount = 0;
float lastAccelMagnitude = 0;
bool stepDetected = false;
unsigned long lastStepTime = 0;

// Display Status
struct DisplayStatus {
    int uploadsToday = 0;
    int archivesToday = 0;
    int uploadsFailed = 0;
    float batteryPercent = 100.0;
    String lastEvent = "Ready";
} displayStatus;

// Logging System
String currentLogFile = "";
unsigned long lastLogFlush = 0;

// Geofence & Standby Mode
bool isAtHome = true;              // Assume at home on boot
bool wasAtHome = true;             // Previous state for edge detection
bool standbyMode = true;           // Low power mode when at home
unsigned long lastHomeCheck = 0;   // Last geofence check time
unsigned long displayOffTime = 0;  // When to turn off display in standby
bool displayIsOff = false;         // Display power state

// Button State
bool buttonPressed = false;
unsigned long buttonPressStart = 0;
bool buttonHandled = false;
bool manualWalkMode = false;  // True when walk started manually via button

// Log levels for filtering
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_UPLOAD = 4  // Special level for upload events
};

const char* LOG_LEVEL_NAMES[] = {"DEBUG", "INFO", "WARN", "ERROR", "UPLOAD"};

// ============================================================================
// Forward Declarations
// ============================================================================

void initPower();
void initModem();
void initGPS();
void initAccelerometer();
void initSDCard();
void initWiFi();
void initDisplay();
void initLogging();
void initButton();
void handleButton();
void checkHomeStatus();
double getDistanceFromHome();
bool isConnectedToHomeWiFi();
void writeLog(LogLevel level, const char* category, const char* message);
void writeLogf(LogLevel level, const char* category, const char* format, ...);
String sendATCommandGetResponse(const char* cmd, unsigned long timeout);
void readGPS();
void readAccelerometer();
void detectSteps();
void startWalk();
void startWalkManual();
void endWalk();
void endWalkManual();
void logWalkData();
void uploadPendingWalks();
int countPendingFiles();
void updateDisplay();
void readBattery();
bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout);
double calculateDistance(double lat1, double lon1, double lat2, double lon2);

// GPS query counter (used in readGPS and updateDisplay)
unsigned long gpsQueryCount = 0;

// ============================================================================
// Setup
// ============================================================================

void setup() {
    Serial.begin(115200);

    // Wait for serial
    unsigned long start = millis();
    while (!Serial && millis() - start < 3000) {
        delay(10);
    }
    delay(500);

    Serial.println();
    Serial.println("============================================");
    Serial.println("    DOG WALKER GPS TRACKER - Popcorn");
    Serial.println("    LilyGo T-A7670G R2 + ADXL345");
    Serial.println("============================================");
    Serial.println();
    Serial.printf("Device ID: %s\n", DEVICE_ID);
    Serial.printf("WiFi SSID: %s\n", WIFI_SSID);
    Serial.println();

    // Step 0: Board Power and Button
    Serial.println("[STEP 0/8] Setting board power...");
    initPower();
    initButton();
    Serial.println("  [OK] Board power pin HIGH");
    Serial.printf("  [OK] Button on GPIO%d (press=start, long=stop)\n", BUTTON_PIN);
    delay(100);

    // Step 1: I2C (on new pins to avoid GPS conflict)
    Serial.println("[STEP 1/7] Initializing I2C...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.printf("  SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println("  [OK]\n");
    delay(100);

    // Step 2: OLED Display
    Serial.println("[STEP 2/7] Initializing OLED Display...");
    initDisplay();
    delay(100);

    // Step 3: SD Card
    Serial.println("[STEP 3/7] Initializing SD Card...");
    initSDCard();
    delay(100);

    // Step 4: Accelerometer
    Serial.println("[STEP 4/7] Initializing Accelerometer...");
    initAccelerometer();
    delay(100);

    // Step 5: GPS (L76K on UART2)
    Serial.println("[STEP 5/7] Initializing GPS (L76K UART)...");
    initGPS();
    delay(100);

    // Step 6: Modem (4G connectivity)
    Serial.println("[STEP 6/7] Initializing Modem (4G)...");
    Serial.println("  (Requires battery connection!)");
    initModem();
    delay(100);

    // Step 7: WiFi
    Serial.println("[STEP 7/7] Connecting WiFi...");
    initWiFi();

    // Initialize logging (after WiFi for NTP time)
    Serial.println("\n[LOGGING] Initializing log file...");
    initLogging();

    // Summary
    Serial.println();
    Serial.println("============================================");
    Serial.println("           INITIALIZATION SUMMARY");
    Serial.println("============================================");
    Serial.printf("  OLED Display:  %s\n", displayReady ? "OK" : "FAIL");
    Serial.printf("  SD Card:       %s\n", sdCardReady ? "OK" : "FAIL");
    Serial.printf("  Accelerometer: %s\n", accelReady ? "OK" : "FAIL");
    Serial.printf("  Modem (4G):    %s\n", modemReady ? "OK" : "FAIL");
    Serial.printf("  GPS (modem):   %s\n", gpsEnabled ? "OK" : "FAIL");
    Serial.printf("  WiFi:          %s\n", WiFi.status() == WL_CONNECTED ? "OK" : "FAIL");
    Serial.println("============================================");
    Serial.println();
    Serial.println("System ready. Monitoring for walks...");
    Serial.println();

    // Log system startup
    writeLog(LOG_INFO, "SYSTEM", "=== SYSTEM STARTUP COMPLETE ===");
    writeLogf(LOG_INFO, "SYSTEM", "Device: %s", DEVICE_ID);
    writeLogf(LOG_INFO, "SYSTEM", "Display: %s | SD: %s | Accel: %s",
        displayReady ? "OK" : "FAIL", sdCardReady ? "OK" : "FAIL", accelReady ? "OK" : "FAIL");
    writeLogf(LOG_INFO, "SYSTEM", "GPS: %s | Modem: %s | WiFi: %s",
        gpsEnabled ? "OK" : "FAIL", modemReady ? "OK" : "FAIL",
        WiFi.status() == WL_CONNECTED ? "OK" : "FAIL");
    if (WiFi.status() == WL_CONNECTED) {
        writeLogf(LOG_INFO, "WIFI", "Connected to %s, IP: %s", WIFI_SSID, WiFi.localIP().toString().c_str());
    }

    // Log geofence configuration
    writeLogf(LOG_INFO, "GEOFENCE", "Home: %.6f, %.6f | Start: %dm | End: %dm",
        HOME_LATITUDE, HOME_LONGITUDE, HOME_RADIUS_START, HOME_RADIUS_END);
    writeLogf(LOG_INFO, "GEOFENCE", "Home WiFi: %s | Standby display off: %ds",
        HOME_WIFI_SSID, STANDBY_DISPLAY_OFF_MS / 1000);
    Serial.printf("[GEOFENCE] Home: %.6f, %.6f\n", HOME_LATITUDE, HOME_LONGITUDE);
    Serial.printf("[GEOFENCE] Leave radius: %dm | Return radius: %dm\n", HOME_RADIUS_START, HOME_RADIUS_END);

    // Initial display update
    if (displayReady) {
        displayStatus.lastEvent = "System Ready";
        updateDisplay();
    }

    // Initialize display off timer (prevent immediate turn-off in standby)
    displayOffTime = millis() + STANDBY_DISPLAY_OFF_MS;
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    unsigned long now = millis();

    // Handle button input (for manual start/stop)
    // Button press also wakes display from standby
    handleButton();

    // Read GPS (reduced frequency in standby mode)
    static unsigned long lastStandbyGPS = 0;
    if (!standbyMode || (now - lastStandbyGPS >= STANDBY_GPS_INTERVAL)) {
        readGPS();
        if (standbyMode) lastStandbyGPS = now;
    }

    // Read accelerometer and detect steps
    readAccelerometer();
    detectSteps();

    // Check geofence / home status (for auto start/stop)
    checkHomeStatus();

    // Read battery level periodically
    static unsigned long lastBatteryRead = 0;
    if (now - lastBatteryRead >= 10000) {  // Every 10 seconds
        readBattery();
        lastBatteryRead = now;
    }

    // Update activity time if moving
    if (currentAccel.isMoving || (currentGPS.valid && currentGPS.speed > WALK_START_SPEED)) {
        lastActivityTime = now;
    }

    // Update display every 500ms (skip if display is off in standby)
    if (displayReady && !displayIsOff && now - lastDisplayUpdate >= 500) {
        updateDisplay();
        lastDisplayUpdate = now;
    }

    // Log detailed status to file every 2 minutes for debugging
    static unsigned long lastStatusLog = 0;
    if (now - lastStatusLog >= 120000) {  // 2 minutes
        // System status
        writeLogf(LOG_INFO, "STATUS", "=== Periodic Status Report ===");
        writeLogf(LOG_INFO, "STATUS", "Uptime: %lu min | Free heap: %lu bytes",
            now / 60000, ESP.getFreeHeap());

        // Battery & Power
        writeLogf(LOG_INFO, "POWER", "Battery: %.0f%% | Standby: %s | Display: %s",
            displayStatus.batteryPercent,
            standbyMode ? "YES" : "NO",
            displayIsOff ? "OFF" : "ON");

        // GPS details
        writeLogf(LOG_INFO, "GPS", "Valid: %s | Sats: %d | Lat: %.6f | Lon: %.6f | Speed: %.1f km/h",
            currentGPS.valid ? "YES" : "NO",
            currentGPS.satellites,
            currentGPS.latitude, currentGPS.longitude,
            currentGPS.speed);
        writeLogf(LOG_INFO, "GPS", "Chars: %lu | Fixes: %lu | Errors: %lu | Alt: %.1fm",
            gps.charsProcessed(), gps.sentencesWithFix(), gps.failedChecksum(),
            currentGPS.altitude);

        // Geofence status
        double distHome = getDistanceFromHome();
        writeLogf(LOG_INFO, "GEOFENCE", "AtHome: %s | Distance: %.0fm | WiFi: %s",
            isAtHome ? "YES" : "NO",
            distHome >= 0 ? distHome : -1,
            isConnectedToHomeWiFi() ? HOME_WIFI_SSID : "disconnected");

        // Walk status
        if (currentWalk.isActive) {
            unsigned long walkDur = (now - currentWalk.startTime) / 1000;
            writeLogf(LOG_INFO, "WALK", "Active: YES | Duration: %lu sec | Distance: %.0fm | Points: %d",
                walkDur, currentWalk.totalDistance, currentWalk.dataPoints);
            writeLogf(LOG_INFO, "WALK", "Steps: %lu | MaxSpeed: %.1f | AvgSpeed: %.1f | Manual: %s",
                stepCount, currentWalk.maxSpeed, currentWalk.avgSpeed,
                manualWalkMode ? "YES" : "NO");
        } else {
            writeLogf(LOG_INFO, "WALK", "Active: NO | Today steps: %lu", todayStepCount);
        }

        // Upload status
        int pending = countPendingFiles();
        writeLogf(LOG_INFO, "UPLOAD", "Pending: %d | Today uploads: %d | Failed: %d",
            pending, displayStatus.uploadsToday, displayStatus.uploadsFailed);

        // Network status
        if (WiFi.status() == WL_CONNECTED) {
            writeLogf(LOG_INFO, "WIFI", "Connected: %s | IP: %s | RSSI: %d dBm",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else {
            writeLog(LOG_INFO, "WIFI", "Disconnected");
        }

        // Accelerometer
        writeLogf(LOG_INFO, "ACCEL", "X: %.2f | Y: %.2f | Z: %.2f | Mag: %.2f | Moving: %s",
            currentAccel.x, currentAccel.y, currentAccel.z, currentAccel.magnitude,
            currentAccel.isMoving ? "YES" : "NO");

        lastStatusLog = now;
    }

    // Print status every 5 seconds
    if (now - lastStatusPrint >= 5000) {
        Serial.println("---------------------------------------------");
        Serial.printf("[TIME]  Uptime: %lu sec\n", now / 1000);
        Serial.printf("[STEPS] Today: %lu | Walk: %lu\n", todayStepCount, stepCount);

        if (currentGPS.valid) {
            Serial.printf("[GPS]   Lat: %.6f, Lon: %.6f\n", currentGPS.latitude, currentGPS.longitude);
            Serial.printf("        Speed: %.1f km/h, Sats: %d\n", currentGPS.speed, currentGPS.satellites);
        } else {
            Serial.printf("[GPS]   Waiting for fix... (sats: %d, chars: %lu)\n",
                currentGPS.satellites, gps.charsProcessed());
        }

        if (accelReady) {
            Serial.printf("[ACCEL] X=%.2f Y=%.2f Z=%.2f Mag=%.2f\n",
                currentAccel.x, currentAccel.y, currentAccel.z, currentAccel.magnitude);
            Serial.printf("        Moving: %s\n", currentAccel.isMoving ? "YES" : "NO");
        }

        if (currentWalk.isActive) {
            unsigned long walkDuration = (now - currentWalk.startTime) / 1000;
            Serial.printf("[WALK]  ACTIVE: %lu sec | Dist: %.0f m | Pts: %d\n",
                walkDuration, currentWalk.totalDistance, currentWalk.dataPoints);
        } else {
            Serial.println("[WALK]  Idle - waiting for movement");
        }

        Serial.printf("[BATT]  %.0f%%\n", displayStatus.batteryPercent);
        Serial.printf("[WIFI]  %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
        lastStatusPrint = now;
    }

    // Walk state machine
    if (!currentWalk.isActive) {
        if (currentGPS.valid && currentGPS.speed > WALK_START_SPEED && currentAccel.isMoving) {
            startWalk();
        }
    } else {
        if (now - lastLogTime >= LOG_INTERVAL) {
            logWalkData();
            lastLogTime = now;
        }
        // Only auto-timeout for auto-detected walks, not manual walks
        // Manual walks are stopped by long button press only
        if (!manualWalkMode && now - lastActivityTime > WALK_END_TIMEOUT) {
            endWalk();
        }
    }

    // WiFi check every minute
    static unsigned long lastWiFiCheck = 0;
    static bool wasConnected = false;
    if (now - lastWiFiCheck > 60000) {
        bool isConnected = (WiFi.status() == WL_CONNECTED);

        // Log WiFi state changes
        if (isConnected && !wasConnected) {
            writeLogf(LOG_INFO, "WIFI", "Connected! IP: %s", WiFi.localIP().toString().c_str());
        } else if (!isConnected && wasConnected) {
            writeLog(LOG_WARN, "WIFI", "Connection lost!");
        }
        wasConnected = isConnected;

        if (!isConnected) {
            Serial.println("[WIFI] Reconnecting...");
            writeLog(LOG_INFO, "WIFI", "Attempting reconnection...");
            initWiFi();
            if (WiFi.status() == WL_CONNECTED) {
                writeLogf(LOG_INFO, "WIFI", "Reconnected! IP: %s", WiFi.localIP().toString().c_str());
                wasConnected = true;
            } else {
                writeLog(LOG_WARN, "WIFI", "Reconnection failed");
            }
        }
        if (WiFi.status() == WL_CONNECTED && sdCardReady) {
            uploadPendingWalks();
        }
        lastWiFiCheck = now;
    }

    delay(10);
}

// ============================================================================
// Initialization Functions
// ============================================================================

void initPower() {
    // Keep board powered when USB is disconnected
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);
}

void initButton() {
    // Initialize button pin with internal pull-up
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void handleButton() {
    // Read button state (active LOW)
    bool isPressed = (digitalRead(BUTTON_PIN) == LOW);
    unsigned long now = millis();

    if (isPressed && !buttonPressed) {
        // Button just pressed
        buttonPressed = true;
        buttonPressStart = now;
        buttonHandled = false;

        // Wake display from standby on any button press
        if (displayIsOff) {
            displayIsOff = false;
            displayOffTime = now + STANDBY_DISPLAY_OFF_MS;
            Serial.println("[BUTTON] Woke display from standby");
        }
    } else if (!isPressed && buttonPressed) {
        // Button just released
        unsigned long pressDuration = now - buttonPressStart;

        if (!buttonHandled) {
            if (pressDuration >= BUTTON_LONG_PRESS_MS) {
                // Long press - stop activity
                if (currentWalk.isActive) {
                    Serial.println("\n[BUTTON] Long press detected - stopping activity");
                    endWalkManual();
                } else {
                    Serial.println("[BUTTON] Long press - no active walk to stop");
                    displayStatus.lastEvent = "No walk active";
                }
            } else if (pressDuration >= BUTTON_DEBOUNCE_MS) {
                // Short press - start activity
                if (!currentWalk.isActive) {
                    Serial.println("\n[BUTTON] Short press detected - starting activity");
                    startWalkManual();
                } else {
                    Serial.println("[BUTTON] Short press - walk already active");
                    displayStatus.lastEvent = "Walk in progress";
                }
            }
        }

        buttonPressed = false;
        buttonHandled = false;
    } else if (isPressed && buttonPressed && !buttonHandled) {
        // Button still held - check for long press feedback
        unsigned long pressDuration = now - buttonPressStart;
        if (pressDuration >= BUTTON_LONG_PRESS_MS) {
            // Give feedback that long press is detected
            if (currentWalk.isActive) {
                displayStatus.lastEvent = "Release to STOP";
            }
            buttonHandled = true;  // Will handle on release
            buttonHandled = false; // Actually handle on release
        } else if (pressDuration >= 500 && currentWalk.isActive) {
            // Show "hold to stop" hint
            displayStatus.lastEvent = "Hold 2s to stop...";
        }
    }
}

void initSDCard() {
    sdSPI = new SPIClass(VSPI);
    sdSPI->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN, *sdSPI)) {
        Serial.println("  [FAIL] SD Card not found");
        sdCardReady = false;
        return;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("  [FAIL] No SD card inserted");
        sdCardReady = false;
        return;
    }

    Serial.print("  Card type: ");
    switch(cardType) {
        case CARD_MMC:  Serial.println("MMC"); break;
        case CARD_SD:   Serial.println("SD"); break;
        case CARD_SDHC: Serial.println("SDHC"); break;
        default:        Serial.println("Unknown"); break;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("  Card size: %lluMB\n", cardSize);

    if (!SD.exists("/walks")) SD.mkdir("/walks");
    if (!SD.exists("/pending")) SD.mkdir("/pending");
    if (!SD.exists("/logs")) SD.mkdir("/logs");

    // Write immediate boot marker (before WiFi/NTP)
    File bootLog = SD.open("/logs/boot.log", FILE_APPEND);
    if (bootLog) {
        bootLog.printf("\n[BOOT] Device started at millis=%lu\n", millis());
        bootLog.close();
        Serial.println("  Boot marker written to /logs/boot.log");
    } else {
        Serial.println("  [WARN] Could not write boot marker");
    }

    sdCardReady = true;
    Serial.println("  [OK]");
}

void initAccelerometer() {
    accel = new Adafruit_ADXL345_Unified(12345);

    if (!accel->begin(ADXL345_ADDRESS)) {
        Serial.println("  [FAIL] ADXL345 not found");
        Serial.printf("  Wiring: SDA=GPIO%d, SCL=GPIO%d, Addr=0x%02X\n",
            I2C_SDA_PIN, I2C_SCL_PIN, ADXL345_ADDRESS);
        accelReady = false;
        return;
    }

    accel->setRange(ADXL345_RANGE_4_G);
    accel->setDataRate(ADXL345_DATARATE_50_HZ);

    accelReady = true;
    Serial.println("  [OK] ADXL345 initialized");
}

void initGPS() {
    // On LilyGo T-A7670G R2, GPS (L76K) is connected directly to ESP32 via UART2
    // NOT through the modem AT commands!

    Serial.printf("  GPS on UART2: RX=GPIO%d, TX=GPIO%d\n", GPS_RX_PIN, GPS_TX_PIN);

    // Initialize SerialGPS on UART2
    SerialGPS.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    // Clear any stale data in the buffer
    while (SerialGPS.available()) {
        SerialGPS.read();
    }

    gpsEnabled = true;
    Serial.println("  [OK] GPS UART initialized");
    Serial.println("  Note: First fix may take 30-60 seconds outdoors with clear sky");
}

void initModem() {
    pinMode(MODEM_POWER_ON_PIN, OUTPUT);
    pinMode(MODEM_PWRKEY_PIN, OUTPUT);
    pinMode(MODEM_RESET_PIN, OUTPUT);

    Serial.println("  Powering on modem...");
    digitalWrite(MODEM_POWER_ON_PIN, HIGH);
    delay(100);

    digitalWrite(MODEM_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(MODEM_PWRKEY_PIN, HIGH);
    delay(1000);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);

    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(3000);

    Serial.println("  Checking modem response...");
    int retries = 10;
    while (retries > 0) {
        if (sendATCommand("AT", "OK", 1000)) {
            modemReady = true;
            break;
        }
        retries--;
        Serial.printf("  Retry %d/10...\n", 10 - retries);
        delay(500);
    }

    if (modemReady) {
        sendATCommand("ATE0", "OK", 1000);
        Serial.println("  [OK] Modem ready");
    } else {
        Serial.println("  [FAIL] Modem not responding");
        Serial.println("  Check: Battery connected?");
    }
}

void initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("  Connecting to %s", WIFI_SSID);

    int timeout = 30;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        Serial.print(".");
        timeout--;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  [OK] IP: %s\n", WiFi.localIP().toString().c_str());

        // Sync time via NTP for proper archive folder naming
        configTime(19800, 0, "pool.ntp.org", "time.nist.gov");  // IST = UTC+5:30 = 19800 sec
        Serial.print("  Syncing time...");
        int ntpRetries = 10;
        while (time(nullptr) < 1600000000 && ntpRetries > 0) {  // Wait until time > year 2020
            delay(500);
            Serial.print(".");
            ntpRetries--;
        }
        Serial.println();

        time_t now = time(nullptr);
        if (now > 1600000000) {
            struct tm* timeinfo = localtime(&now);
            char timeStr[25];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
            Serial.printf("  [OK] Time: %s\n", timeStr);
        } else {
            Serial.println("  [WARN] NTP sync failed - using default time");
        }
    } else {
        Serial.println("  [FAIL] Could not connect");
    }
}

// ============================================================================
// Logging Functions
// ============================================================================

void initLogging() {
    if (!sdCardReady) {
        Serial.println("  [SKIP] SD card not ready - logging disabled");
        return;
    }

    // Create log filename based on current date: /logs/YYYY-MM-DD.log
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);

    char filename[32];
    if (now > 1600000000) {  // Valid time from NTP
        strftime(filename, sizeof(filename), "/logs/%Y-%m-%d.log", timeinfo);
        Serial.printf("  NTP time valid, using date-based log\n");
    } else {
        // Fallback if no NTP sync
        snprintf(filename, sizeof(filename), "/logs/boot_%lu.log", millis());
        Serial.printf("  NTP failed, using boot-time log\n");
    }

    currentLogFile = String(filename);
    Serial.printf("  Log file: %s\n", currentLogFile.c_str());

    // Write startup header
    File logFile = SD.open(currentLogFile, FILE_APPEND);
    if (logFile) {
        logFile.println();
        logFile.println("================================================================================");
        char timeStr[32];
        if (now > 1600000000) {
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        } else {
            snprintf(timeStr, sizeof(timeStr), "Boot+%lums", millis());
        }
        logFile.printf("=== DOG WALKER GPS TRACKER - Session Started: %s ===\n", timeStr);
        logFile.println("================================================================================");
        logFile.close();
        Serial.println("  [OK] Log file created successfully");
    } else {
        Serial.println("  [FAIL] Could not create log file!");
        currentLogFile = "";  // Disable logging
    }
}

void writeLog(LogLevel level, const char* category, const char* message) {
    // Always print to Serial
    Serial.printf("[%s][%s] %s\n", LOG_LEVEL_NAMES[level], category, message);

    // Write to SD card log file
    if (!sdCardReady || currentLogFile.length() == 0) return;

    File logFile = SD.open(currentLogFile, FILE_APPEND);
    if (logFile) {
        // Get timestamp
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        char timeStr[20];

        if (now > 1600000000) {
            strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
        } else {
            snprintf(timeStr, sizeof(timeStr), "%lu", millis() / 1000);
        }

        logFile.printf("[%s][%s][%s] %s\n", timeStr, LOG_LEVEL_NAMES[level], category, message);
        logFile.close();
    }
}

void writeLogf(LogLevel level, const char* category, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    writeLog(level, category, buffer);
}

// ============================================================================
// Sensor Reading Functions
// ============================================================================

// Helper to send AT command and get response
String sendATCommandGetResponse(const char* cmd, unsigned long timeout) {
    // Clear any pending data
    while (SerialAT.available()) SerialAT.read();

    SerialAT.println(cmd);

    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        if (SerialAT.available()) {
            char c = SerialAT.read();
            response += c;
            // Check if we got complete response
            if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
                break;
            }
        }
    }
    return response;
}

// Track GPS state
static bool hadFirstFix = false;
static unsigned long lastGPSDebugPrint = 0;
// Note: gpsQueryCount tracks number of valid NMEA sentences processed

void readGPS() {
    if (!gpsEnabled) return;

    // Read all available bytes from GPS UART and feed to TinyGPSPlus
    // This should be called every loop iteration to prevent buffer overflow
    while (SerialGPS.available() > 0) {
        char c = SerialGPS.read();
        gps.encode(c);
    }

    // Update satellite count (available even without fix)
    if (gps.satellites.isValid()) {
        currentGPS.satellites = gps.satellites.value();
    }

    // Check if we have a new valid location
    if (gps.location.isUpdated() && gps.location.isValid()) {
        // Get raw GPS values
        double rawLat = gps.location.lat();
        double rawLon = gps.location.lng();
        double rawSpeed = gps.speed.isValid() ? gps.speed.kmph() : 0;

        // Cap raw speed before filtering (reject obvious GPS glitches)
        // No dog walk exceeds 20 km/h, so anything higher is noise
        if (rawSpeed > 20.0) {
            rawSpeed = currentGPS.speed;  // Use previous filtered value
        }

        // Apply Kalman filter to smooth GPS data
        currentGPS.latitude = kalmanLat.update(rawLat);
        currentGPS.longitude = kalmanLon.update(rawLon);
        currentGPS.speed = kalmanSpeed.update(rawSpeed);

        // Hard cap on filtered speed (walking max ~7 km/h, running ~15 km/h)
        if (currentGPS.speed > 15.0) {
            currentGPS.speed = 15.0;
        }

        currentGPS.valid = true;

        if (gps.altitude.isValid()) {
            currentGPS.altitude = gps.altitude.meters();
        }

        gpsQueryCount++;  // Count valid location updates

        // Log first GPS fix
        if (!hadFirstFix) {
            hadFirstFix = true;
            writeLogf(LOG_INFO, "GPS", "First fix! Raw: %.6f, %.6f | Filtered: %.6f, %.6f",
                rawLat, rawLon, currentGPS.latitude, currentGPS.longitude);
            Serial.printf("[GPS] First fix acquired! Lat: %.6f, Lon: %.6f (Kalman filtered)\n",
                currentGPS.latitude, currentGPS.longitude);
        }
    } else if (!gps.location.isValid()) {
        currentGPS.valid = false;
        // Reset Kalman filters when GPS fix is lost
        if (kalmanLat.initialized) {
            kalmanLat.reset();
            kalmanLon.reset();
            kalmanSpeed.reset();
        }
    }

    // Debug output every 30 seconds
    if (millis() - lastGPSDebugPrint >= 30000) {
        lastGPSDebugPrint = millis();
        Serial.printf("[GPS DEBUG] Chars: %lu, Sentences: %lu, Checksum Errors: %lu, Sats: %d, Valid: %s\n",
            gps.charsProcessed(), gps.sentencesWithFix(), gps.failedChecksum(),
            currentGPS.satellites, currentGPS.valid ? "YES" : "NO");
        writeLogf(LOG_DEBUG, "GPS", "Chars: %lu, Good: %lu, Errors: %lu, Sats: %d",
            gps.charsProcessed(), gps.sentencesWithFix(), gps.failedChecksum(),
            currentGPS.satellites);
    }
}

void readAccelerometer() {
    if (!accelReady || accel == nullptr) return;

    sensors_event_t event;
    accel->getEvent(&event);

    currentAccel.x = event.acceleration.x;
    currentAccel.y = event.acceleration.y;
    currentAccel.z = event.acceleration.z;
    currentAccel.magnitude = sqrt(
        currentAccel.x * currentAccel.x +
        currentAccel.y * currentAccel.y +
        currentAccel.z * currentAccel.z
    );

    float deviation = abs(currentAccel.magnitude - 9.8);
    currentAccel.isMoving = (deviation > ACTIVITY_THRESHOLD);
}

// ============================================================================
// Walk Functions
// ============================================================================

void startWalk() {
    Serial.println("\n*** WALK STARTED ***");

    currentWalk.isActive = true;
    currentWalk.startTime = millis();
    currentWalk.startTimeUnix = time(nullptr);  // Get actual Unix timestamp
    currentWalk.totalDistance = 0;
    currentWalk.maxSpeed = 0;
    currentWalk.avgSpeed = 0;
    currentWalk.dataPoints = 0;
    stepCount = 0;  // Reset walk step count

    // Log walk start
    writeLog(LOG_INFO, "WALK", "========== WALK STARTED ==========");
    writeLogf(LOG_INFO, "WALK", "Start location: %.6f, %.6f", currentGPS.latitude, currentGPS.longitude);
    writeLogf(LOG_INFO, "WALK", "GPS satellites: %d, Speed: %.1f km/h", currentGPS.satellites, currentGPS.speed);
    writeLogf(LOG_INFO, "WALK", "Unix timestamp: %ld", (long)currentWalk.startTimeUnix);

    if (sdCardReady) {
        currentWalk.filename = "/walks/walk_" + String(currentWalk.startTimeUnix) + ".json";

        File file = SD.open(currentWalk.filename, FILE_WRITE);
        if (file) {
            JsonDocument doc;
            doc["deviceId"] = DEVICE_ID;
            doc["startTime"] = (long)currentWalk.startTimeUnix;  // Unix timestamp in seconds
            doc["startLat"] = currentGPS.latitude;
            doc["startLon"] = currentGPS.longitude;
            serializeJson(doc, file);
            file.close();
            Serial.printf("  File: %s\n", currentWalk.filename.c_str());
            writeLogf(LOG_INFO, "WALK", "Data file: %s", currentWalk.filename.c_str());
        } else {
            writeLog(LOG_ERROR, "WALK", "Failed to create walk data file!");
        }
    }

    lastValidGPS = currentGPS;
    lastActivityTime = millis();
    displayStatus.lastEvent = "Walk Started!";
}

void endWalk() {
    currentWalk.endTime = millis();
    unsigned long duration = currentWalk.endTime - currentWalk.startTime;

    Serial.println("\n*** WALK ENDED ***");
    Serial.printf("  Duration: %lu sec\n", duration / 1000);
    Serial.printf("  Distance: %.0f m\n", currentWalk.totalDistance);
    Serial.printf("  Points: %d\n", currentWalk.dataPoints);
    Serial.printf("  Steps: %lu\n", stepCount);

    // Log walk end with full stats
    writeLog(LOG_INFO, "WALK", "========== WALK ENDED ==========");
    writeLogf(LOG_INFO, "WALK", "Duration: %lu sec (%.1f min)", duration / 1000, duration / 60000.0);
    writeLogf(LOG_INFO, "WALK", "Distance: %.0f m (%.2f km)", currentWalk.totalDistance, currentWalk.totalDistance / 1000.0);
    writeLogf(LOG_INFO, "WALK", "Data points: %d", currentWalk.dataPoints);
    writeLogf(LOG_INFO, "WALK", "Steps: %lu", stepCount);
    writeLogf(LOG_INFO, "WALK", "Max speed: %.1f km/h, Avg speed: %.1f km/h", currentWalk.maxSpeed, currentWalk.avgSpeed);

    if (duration < MIN_WALK_DURATION) {
        Serial.println("  Walk too short - discarding");
        writeLogf(LOG_WARN, "WALK", "Walk too short (<%lu ms) - discarding", MIN_WALK_DURATION);
        if (sdCardReady && currentWalk.filename.length() > 0) {
            SD.remove(currentWalk.filename.c_str());
        }
        displayStatus.lastEvent = "Walk too short";
    } else if (sdCardReady && currentWalk.filename.length() > 0) {
        String pendingPath = "/pending/walk_" + String(currentWalk.startTimeUnix) + ".json";
        SD.rename(currentWalk.filename.c_str(), pendingPath.c_str());
        Serial.printf("  Moved to: %s\n", pendingPath.c_str());
        writeLogf(LOG_INFO, "WALK", "Queued for upload: %s", pendingPath.c_str());
        char buf[32];
        snprintf(buf, sizeof(buf), "Walk: %.0fm %lus", currentWalk.totalDistance, duration/1000);
        displayStatus.lastEvent = String(buf);
    }

    currentWalk.isActive = false;
    manualWalkMode = false;
}

// Manual walk start (triggered by button press)
void startWalkManual() {
    Serial.println("\n*** MANUAL WALK STARTED ***");

    currentWalk.isActive = true;
    currentWalk.startTime = millis();
    currentWalk.startTimeUnix = time(nullptr);  // Get actual Unix timestamp
    currentWalk.totalDistance = 0;
    currentWalk.maxSpeed = 0;
    currentWalk.avgSpeed = 0;
    currentWalk.dataPoints = 0;
    stepCount = 0;
    manualWalkMode = true;  // Mark as manual start

    // Log walk start
    writeLog(LOG_INFO, "WALK", "========== MANUAL WALK STARTED ==========");
    if (currentGPS.valid) {
        writeLogf(LOG_INFO, "WALK", "Start location: %.6f, %.6f", currentGPS.latitude, currentGPS.longitude);
        writeLogf(LOG_INFO, "WALK", "GPS satellites: %d", currentGPS.satellites);
    } else {
        writeLog(LOG_INFO, "WALK", "Start location: GPS not available");
    }
    writeLogf(LOG_INFO, "WALK", "Unix timestamp: %ld", (long)currentWalk.startTimeUnix);

    if (sdCardReady) {
        currentWalk.filename = "/walks/walk_" + String(currentWalk.startTimeUnix) + ".json";

        File file = SD.open(currentWalk.filename, FILE_WRITE);
        if (file) {
            JsonDocument doc;
            doc["deviceId"] = DEVICE_ID;
            doc["startTime"] = (long)currentWalk.startTimeUnix;  // Unix timestamp in seconds
            doc["manual"] = true;  // Mark as manually started
            if (currentGPS.valid) {
                doc["startLat"] = currentGPS.latitude;
                doc["startLon"] = currentGPS.longitude;
            }
            serializeJson(doc, file);
            file.close();
            Serial.printf("  File: %s\n", currentWalk.filename.c_str());
            writeLogf(LOG_INFO, "WALK", "Data file: %s", currentWalk.filename.c_str());
        } else {
            writeLog(LOG_ERROR, "WALK", "Failed to create walk data file!");
        }
    }

    if (currentGPS.valid) {
        lastValidGPS = currentGPS;
    }
    lastActivityTime = millis();
    displayStatus.lastEvent = "RECORDING...";
}

// Manual walk end (triggered by long button press)
void endWalkManual() {
    currentWalk.endTime = millis();
    unsigned long duration = currentWalk.endTime - currentWalk.startTime;

    Serial.println("\n*** MANUAL WALK ENDED ***");
    Serial.printf("  Duration: %lu sec\n", duration / 1000);
    Serial.printf("  Distance: %.0f m\n", currentWalk.totalDistance);
    Serial.printf("  Points: %d\n", currentWalk.dataPoints);
    Serial.printf("  Steps: %lu\n", stepCount);

    // Log walk end
    writeLog(LOG_INFO, "WALK", "========== MANUAL WALK ENDED ==========");
    writeLogf(LOG_INFO, "WALK", "Duration: %lu sec (%.1f min)", duration / 1000, duration / 60000.0);
    writeLogf(LOG_INFO, "WALK", "Distance: %.0f m (%.2f km)", currentWalk.totalDistance, currentWalk.totalDistance / 1000.0);
    writeLogf(LOG_INFO, "WALK", "Data points: %d", currentWalk.dataPoints);
    writeLogf(LOG_INFO, "WALK", "Steps: %lu", stepCount);

    // For manual walks, always save (no minimum duration check)
    if (sdCardReady && currentWalk.filename.length() > 0) {
        String pendingPath = "/pending/walk_" + String(currentWalk.startTimeUnix) + ".json";
        SD.rename(currentWalk.filename.c_str(), pendingPath.c_str());
        Serial.printf("  Moved to: %s\n", pendingPath.c_str());
        writeLogf(LOG_INFO, "WALK", "Queued for upload: %s", pendingPath.c_str());

        char buf[32];
        snprintf(buf, sizeof(buf), "Saved: %lus %lu steps", duration/1000, stepCount);
        displayStatus.lastEvent = String(buf);
    } else {
        displayStatus.lastEvent = "Walk stopped";
    }

    currentWalk.isActive = false;
    manualWalkMode = false;
}

void logWalkData() {
    if (!currentWalk.isActive || !sdCardReady) return;
    if (!currentGPS.valid) return;

    if (lastValidGPS.valid && currentWalk.dataPoints > 0) {
        double dist = calculateDistance(
            lastValidGPS.latitude, lastValidGPS.longitude,
            currentGPS.latitude, currentGPS.longitude
        );
        currentWalk.totalDistance += dist;
    }

    if (currentGPS.speed > currentWalk.maxSpeed) {
        currentWalk.maxSpeed = currentGPS.speed;
    }

    currentWalk.dataPoints++;
    currentWalk.avgSpeed = ((currentWalk.avgSpeed * (currentWalk.dataPoints - 1)) + currentGPS.speed) / currentWalk.dataPoints;

    File file = SD.open(currentWalk.filename, FILE_APPEND);
    if (file) {
        JsonDocument point;
        point["t"] = (millis() - currentWalk.startTime) / 1000.0;
        point["lat"] = currentGPS.latitude;
        point["lon"] = currentGPS.longitude;
        point["spd"] = currentGPS.speed;
        point["ax"] = currentAccel.x;
        point["ay"] = currentAccel.y;
        point["az"] = currentAccel.z;

        file.print("\n");
        serializeJson(point, file);
        file.close();
    }

    lastValidGPS = currentGPS;
}

void uploadPendingWalks() {
    if (!sdCardReady || WiFi.status() != WL_CONNECTED) return;

    Serial.println("[UPLOAD] Checking pending walks...");
    writeLog(LOG_UPLOAD, "UPLOAD", "--- Starting upload check ---");

    File dir = SD.open("/pending");
    if (!dir || !dir.isDirectory()) {
        writeLog(LOG_WARN, "UPLOAD", "No pending directory or empty");
        return;
    }

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            Serial.printf("  Uploading: %s\n", filename.c_str());
            writeLogf(LOG_UPLOAD, "UPLOAD", "=== Processing: %s ===", filename.c_str());

            // Read file content
            String content = "";
            while (file.available()) {
                content += (char)file.read();
            }
            file.close();

            // Parse the file: first line is metadata, rest are points
            int firstNewline = content.indexOf('\n');
            if (firstNewline < 0) {
                Serial.println("    [SKIP] Invalid file format - moving to /failed/");
                writeLogf(LOG_ERROR, "UPLOAD", "SKIP %s: Invalid file format (no newline)", filename.c_str());
                // Move to failed directory so it doesn't block queue
                String failedPath = "/failed/" + filename;
                SD.mkdir("/failed");
                SD.rename(("/pending/" + filename).c_str(), failedPath.c_str());
                writeLogf(LOG_WARN, "UPLOAD", "Moved to: %s", failedPath.c_str());
                file = dir.openNextFile();
                continue;
            }

            String metaLine = content.substring(0, firstNewline);
            String pointsData = content.substring(firstNewline + 1);

            // Parse metadata
            JsonDocument metaDoc;
            DeserializationError metaErr = deserializeJson(metaDoc, metaLine);
            if (metaErr) {
                Serial.printf("    [SKIP] Invalid metadata: %s - moving to /failed/\n", metaErr.c_str());
                writeLogf(LOG_ERROR, "UPLOAD", "SKIP %s: Invalid metadata JSON: %s", filename.c_str(), metaErr.c_str());
                // Move to failed directory so it doesn't block queue
                String failedPath = "/failed/" + filename;
                SD.mkdir("/failed");
                SD.rename(("/pending/" + filename).c_str(), failedPath.c_str());
                writeLogf(LOG_WARN, "UPLOAD", "Moved to: %s", failedPath.c_str());
                file = dir.openNextFile();
                continue;
            }

            // Build upload payload
            JsonDocument uploadDoc;
            uploadDoc["deviceId"] = metaDoc["deviceId"] | DEVICE_ID;
            uploadDoc["filename"] = filename;
            uploadDoc["startTime"] = metaDoc["startTime"];
            uploadDoc["startLat"] = metaDoc["startLat"];
            uploadDoc["startLon"] = metaDoc["startLon"];

            // Parse points (NDJSON format - one JSON per line)
            JsonArray pointsArray = uploadDoc["points"].to<JsonArray>();
            int pointCount = 0;

            int lineStart = 0;
            while (lineStart < pointsData.length()) {
                int lineEnd = pointsData.indexOf('\n', lineStart);
                if (lineEnd < 0) lineEnd = pointsData.length();

                String line = pointsData.substring(lineStart, lineEnd);
                line.trim();

                if (line.length() > 0) {
                    JsonDocument pointDoc;
                    DeserializationError pointErr = deserializeJson(pointDoc, line);
                    if (!pointErr) {
                        JsonObject point = pointsArray.add<JsonObject>();
                        point["t"] = pointDoc["t"];
                        point["lat"] = pointDoc["lat"];
                        point["lon"] = pointDoc["lon"];
                        point["spd"] = pointDoc["spd"];
                        point["ax"] = pointDoc["ax"];
                        point["ay"] = pointDoc["ay"];
                        point["az"] = pointDoc["az"];
                        pointCount++;
                    }
                }

                lineStart = lineEnd + 1;
            }

            Serial.printf("    Parsed %d points\n", pointCount);
            writeLogf(LOG_UPLOAD, "UPLOAD", "Parsed %d GPS points from file", pointCount);

            // Send to server
            HTTPClient http;
            String url = String(API_BASE_URL) + String(API_ENDPOINT);
            writeLogf(LOG_UPLOAD, "UPLOAD", "URL: %s", url.c_str());

            http.begin(url);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("X-Device-ID", DEVICE_ID);
            http.setTimeout(30000);  // 30 second timeout

            String payload;
            serializeJson(uploadDoc, payload);

            Serial.printf("    Payload size: %d bytes\n", payload.length());
            writeLogf(LOG_UPLOAD, "UPLOAD", "Payload size: %d bytes, sending POST...", payload.length());

            unsigned long uploadStart = millis();
            int httpCode = http.POST(payload);
            unsigned long uploadDuration = millis() - uploadStart;
            if (httpCode == 200 || httpCode == 201) {
                writeLogf(LOG_UPLOAD, "UPLOAD", "SUCCESS! HTTP %d in %lums", httpCode, uploadDuration);

                String fullPath = "/pending/" + filename;

                // Archive instead of delete - organize by year/month
                // Extract timestamp from filename (walk_XXXXXXXX.json)
                unsigned long walkTime = 0;
                int underscorePos = filename.indexOf('_');
                int dotPos = filename.indexOf('.');
                if (underscorePos >= 0 && dotPos > underscorePos) {
                    walkTime = filename.substring(underscorePos + 1, dotPos).toInt();
                }

                // Create archived folder structure: /archived/YYYY/MM/
                // Use current time if walk time not parseable
                time_t now = time(nullptr);
                struct tm* timeinfo = localtime(&now);
                char yearStr[5], monthStr[3];
                strftime(yearStr, sizeof(yearStr), "%Y", timeinfo);
                strftime(monthStr, sizeof(monthStr), "%m", timeinfo);

                String archiveDir = "/archived";
                if (!SD.exists(archiveDir)) SD.mkdir(archiveDir);

                String yearDir = archiveDir + "/" + String(yearStr);
                if (!SD.exists(yearDir)) SD.mkdir(yearDir);

                String monthDir = yearDir + "/" + String(monthStr);
                if (!SD.exists(monthDir)) SD.mkdir(monthDir);

                String archivePath = monthDir + "/" + filename;

                // Move file to archive
                if (SD.rename(fullPath.c_str(), archivePath.c_str())) {
                    Serial.printf("    [OK] Uploaded and archived to %s\n", archivePath.c_str());
                    writeLogf(LOG_UPLOAD, "UPLOAD", "Archived to: %s", archivePath.c_str());
                    displayStatus.uploadsToday++;
                    displayStatus.archivesToday++;
                    displayStatus.lastEvent = "Uploaded " + filename;
                } else {
                    // If rename fails (maybe cross-directory issue), try copy+delete
                    File srcFile = SD.open(fullPath, FILE_READ);
                    File dstFile = SD.open(archivePath, FILE_WRITE);
                    if (srcFile && dstFile) {
                        while (srcFile.available()) {
                            dstFile.write(srcFile.read());
                        }
                        srcFile.close();
                        dstFile.close();
                        SD.remove(fullPath.c_str());
                        Serial.printf("    [OK] Uploaded and archived (copy) to %s\n", archivePath.c_str());
                        writeLogf(LOG_UPLOAD, "UPLOAD", "Archived (via copy) to: %s", archivePath.c_str());
                        displayStatus.uploadsToday++;
                        displayStatus.archivesToday++;
                        displayStatus.lastEvent = "Uploaded " + filename;
                    } else {
                        Serial.println("    [WARN] Uploaded but archive failed - keeping in pending");
                        writeLogf(LOG_WARN, "UPLOAD", "Archive failed for %s - keeping in pending", filename.c_str());
                        if (srcFile) srcFile.close();
                        if (dstFile) dstFile.close();
                        displayStatus.uploadsToday++;
                        displayStatus.lastEvent = "Upload OK (no archive)";
                    }
                }
            } else {
                Serial.printf("    [FAIL] HTTP %d\n", httpCode);
                String response = http.getString();
                Serial.printf("    Response: %s\n", response.substring(0, 200).c_str());

                // Detailed failure logging
                writeLogf(LOG_ERROR, "UPLOAD", "FAILED! HTTP %d after %lums", httpCode, uploadDuration);
                writeLogf(LOG_ERROR, "UPLOAD", "Server response: %s", response.substring(0, 200).c_str());
                writeLogf(LOG_ERROR, "UPLOAD", "File: %s, Points: %d, Size: %d bytes",
                    filename.c_str(), pointCount, payload.length());

                displayStatus.uploadsFailed++;
                displayStatus.lastEvent = "Upload FAIL: " + String(httpCode);
            }
            http.end();
        }
        file = dir.openNextFile();
    }
    dir.close();
}

// ============================================================================
// Helper Functions
// ============================================================================

bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout) {
    SerialAT.println(cmd);

    unsigned long start = millis();
    String response = "";

    while (millis() - start < timeout) {
        if (SerialAT.available()) {
            response += (char)SerialAT.read();
            if (response.indexOf(expected) >= 0) {
                return true;
            }
        }
    }
    return false;
}

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000;
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLon = (lon2 - lon1) * PI / 180.0;
    double a = sin(dLat/2) * sin(dLat/2) +
               cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
               sin(dLon/2) * sin(dLon/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}

// ============================================================================
// Geofence & Home Detection
// ============================================================================

double getDistanceFromHome() {
    if (!currentGPS.valid) return -1;  // Unknown
    return calculateDistance(currentGPS.latitude, currentGPS.longitude,
                            HOME_LATITUDE, HOME_LONGITUDE);
}

bool isConnectedToHomeWiFi() {
    if (WiFi.status() != WL_CONNECTED) return false;
    return (strcmp(WiFi.SSID().c_str(), HOME_WIFI_SSID) == 0);
}

void checkHomeStatus() {
    // Only check every 5 seconds to save CPU
    if (millis() - lastHomeCheck < 5000) return;
    lastHomeCheck = millis();

    wasAtHome = isAtHome;

    // Determine if at home using GPS + WiFi
    double distFromHome = getDistanceFromHome();
    bool wifiAtHome = isConnectedToHomeWiFi();
    bool gpsNearHome = (distFromHome >= 0 && distFromHome < HOME_RADIUS_END);

    // LEAVING HOME detection: WiFi disconnects OR GPS beyond start radius
    // (More permissive - either signal means we left)
    if (isAtHome) {
        if (!wifiAtHome && distFromHome >= 0 && distFromHome > HOME_RADIUS_START) {
            isAtHome = false;
        }
    }

    // ARRIVING HOME detection: MUST have WiFi connected (solves park problem!)
    // GPS alone near home won't trigger arrival - prevents false triggers in park
    if (!isAtHome) {
        #if REQUIRE_WIFI_TO_END
            // Strict: only WiFi connection means home (not just GPS proximity)
            isAtHome = wifiAtHome;
        #else
            // Lenient: WiFi OR GPS proximity
            isAtHome = wifiAtHome || gpsNearHome;
        #endif
    }

    // Handle state transitions
    if (wasAtHome && !isAtHome) {
        // Just left home!
        Serial.println("\n[GEOFENCE] Left home area!");
        writeLog(LOG_INFO, "GEOFENCE", "Left home - auto-starting walk");

        standbyMode = false;
        displayIsOff = false;

        // Auto-start walk if not already active
        if (!currentWalk.isActive) {
            startWalkManual();  // Use manual mode (no auto-timeout)
            displayStatus.lastEvent = "Auto: Left home";
        }
    } else if (!wasAtHome && isAtHome) {
        // Just arrived home!
        Serial.println("\n[GEOFENCE] Arrived home (WiFi connected)!");

        // Check minimum walk duration before auto-ending
        unsigned long walkDuration = currentWalk.isActive ? (millis() - currentWalk.startTime) : 0;

        if (currentWalk.isActive && walkDuration >= MIN_WALK_BEFORE_END) {
            writeLog(LOG_INFO, "GEOFENCE", "Arrived home - auto-ending walk");
            endWalkManual();
            displayStatus.lastEvent = "Auto: Home";
        } else if (currentWalk.isActive) {
            // Walk too short, don't auto-end yet
            Serial.printf("[GEOFENCE] Walk only %lu sec, need %d sec before auto-end\n",
                walkDuration/1000, MIN_WALK_BEFORE_END/1000);
            writeLogf(LOG_INFO, "GEOFENCE", "Home but walk too short (%lu sec), not ending",
                walkDuration/1000);
            // Don't set isAtHome yet - stay in "away" mode
            isAtHome = false;
        }

        if (isAtHome) {
            standbyMode = true;
            displayOffTime = millis() + STANDBY_DISPLAY_OFF_MS;
        }
    }

    // Update standby mode
    if (isAtHome && !currentWalk.isActive) {
        standbyMode = true;

        // Turn off display after timeout in standby
        if (STANDBY_DISPLAY_DIM && millis() > displayOffTime && !displayIsOff) {
            displayIsOff = true;
            if (display != nullptr) {
                display->clearBuffer();
                display->sendBuffer();
                Serial.println("[STANDBY] Display off to save power");
            }
        }
    } else {
        standbyMode = false;
        if (displayIsOff) {
            displayIsOff = false;
            displayOffTime = millis() + STANDBY_DISPLAY_OFF_MS;
            Serial.println("[STANDBY] Display on");
        }
    }
}

// ============================================================================
// OLED Display Functions
// ============================================================================

void initDisplay() {
    // U8g2 constructor for SH1106 128x64 on hardware I2C
    // Using custom I2C pins (SDA=32, SCL=33) - Wire already initialized in setup()
    display = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

    display->begin();
    displayReady = true;
    Serial.println("  [OK] OLED SH1106 initialized (U8g2)");

    // Show splash screen
    display->clearBuffer();
    display->setFont(u8g2_font_helvB14_tr);  // Large font for title
    display->drawStr(20, 25, "POPCORN");
    display->setFont(u8g2_font_6x10_tf);     // Small font for subtitle
    display->drawStr(10, 42, "Dog Walker Tracker");
    display->drawStr(10, 56, "Initializing...");
    display->sendBuffer();
}

// Count files in pending directory
int countPendingFiles() {
    if (!sdCardReady) return 0;
    int count = 0;
    File dir = SD.open("/pending");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            if (!file.isDirectory()) count++;
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    }
    return count;
}

void updateDisplay() {
    if (!displayReady || display == nullptr) return;

    display->clearBuffer();
    char buf[32];
    bool lowBattery = (displayStatus.batteryPercent < 20);
    bool blinkOn = ((millis() / 500) % 2 == 0);  // For flashing elements

    // Get pending upload count (cached, updates every 5 sec in background)
    static int pendingUploads = 0;
    static unsigned long lastPendingCheck = 0;
    if (millis() - lastPendingCheck > 5000) {
        pendingUploads = countPendingFiles();
        lastPendingCheck = millis();
    }

    // Get current time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);

    // ===== ROW 1: Time + Battery (with low battery flash) =====
    display->setFont(u8g2_font_6x10_tf);
    if (now > 1600000000) {  // Valid NTP time
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    display->drawStr(0, 10, buf);

    // Battery with flash warning
    if (lowBattery && blinkOn) {
        snprintf(buf, sizeof(buf), "!%d%% CHARGE!", (int)displayStatus.batteryPercent);
    } else {
        snprintf(buf, sizeof(buf), "%d%%", (int)displayStatus.batteryPercent);
    }
    display->drawStr(90, 10, buf);

    // ===== ROW 2: Steps + Distance (large) =====
    display->setFont(u8g2_font_helvB12_tr);
    snprintf(buf, sizeof(buf), "%lu", todayStepCount);
    display->drawStr(0, 26, buf);
    display->setFont(u8g2_font_6x10_tf);
    display->drawStr(50, 26, "stp");

    // Distance today or in current walk
    if (currentWalk.isActive) {
        if (currentWalk.totalDistance >= 1000) {
            snprintf(buf, sizeof(buf), "%.1fkm", currentWalk.totalDistance / 1000);
        } else {
            snprintf(buf, sizeof(buf), "%.0fm", currentWalk.totalDistance);
        }
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    display->drawStr(80, 26, buf);

    // ===== ROW 3: Recording Status + Walk Time =====
    if (currentWalk.isActive) {
        unsigned long walkSec = (millis() - currentWalk.startTime) / 1000;
        unsigned long walkMin = walkSec / 60;
        unsigned long walkSecRem = walkSec % 60;
        // Blinking REC
        if (blinkOn) {
            snprintf(buf, sizeof(buf), "*REC* %lu:%02lu", walkMin, walkSecRem);
        } else {
            snprintf(buf, sizeof(buf), " REC  %lu:%02lu", walkMin, walkSecRem);
        }
    } else {
        snprintf(buf, sizeof(buf), "IDLE  Ready");
    }
    display->drawStr(0, 38, buf);

    // ===== ROW 4: GPS + WiFi + Upload Status =====
    // GPS status
    if (currentGPS.valid) {
        snprintf(buf, sizeof(buf), "GPS:%d", currentGPS.satellites);
    } else if (currentGPS.satellites > 0) {
        snprintf(buf, sizeof(buf), "GPS:%d*", currentGPS.satellites);
    } else {
        snprintf(buf, sizeof(buf), "GPS:--");
    }
    display->drawStr(0, 50, buf);

    // WiFi status
    display->drawStr(48, 50, (WiFi.status() == WL_CONNECTED) ? "WiFi" : "----");

    // Upload status
    if (pendingUploads > 0) {
        snprintf(buf, sizeof(buf), "P:%d", pendingUploads);
    } else {
        snprintf(buf, sizeof(buf), "OK");
    }
    display->drawStr(90, 50, buf);

    // ===== ROW 5: Location Status =====
    if (isAtHome) {
        snprintf(buf, sizeof(buf), "HOME - Standby");
    } else if (currentWalk.isActive) {
        double dist = getDistanceFromHome();
        if (dist >= 1000) {
            snprintf(buf, sizeof(buf), "WALK %.1fkm from home", dist/1000);
        } else if (dist >= 0) {
            snprintf(buf, sizeof(buf), "WALK %.0fm from home", dist);
        } else {
            snprintf(buf, sizeof(buf), "WALK - GPS searching");
        }
    } else {
        snprintf(buf, sizeof(buf), "AWAY - Press BTN");
    }
    display->drawStr(0, 62, buf);

    display->sendBuffer();
}

// ============================================================================
// Step Detection
// ============================================================================

void detectSteps() {
    if (!accelReady) return;

    // Simple step detection using accelerometer magnitude threshold crossing
    // A step typically creates a spike in acceleration magnitude
    const float STEP_THRESHOLD = 12.0;  // m/s² - adjust based on testing
    const float STEP_MIN_THRESHOLD = 9.0;
    const unsigned long STEP_DEBOUNCE = 250;  // ms between steps (max ~4 steps/sec)

    float mag = currentAccel.magnitude;

    // Detect rising edge crossing threshold
    if (!stepDetected && mag > STEP_THRESHOLD && lastAccelMagnitude < STEP_THRESHOLD) {
        unsigned long now = millis();
        if (now - lastStepTime > STEP_DEBOUNCE) {
            stepCount++;
            todayStepCount++;
            lastStepTime = now;
            stepDetected = true;
        }
    }

    // Reset detection when magnitude falls below lower threshold
    if (stepDetected && mag < STEP_MIN_THRESHOLD) {
        stepDetected = false;
    }

    lastAccelMagnitude = mag;
}

// ============================================================================
// Battery Reading
// ============================================================================

void readBattery() {
    // Read battery voltage from ADC
    // LilyGo T-A7670G has voltage divider on GPIO35
    int adcValue = analogRead(BAT_ADC_PIN);

    // Convert to voltage (ESP32 ADC is 12-bit, 0-4095)
    // With voltage divider ratio (typically 2:1), max voltage is ~4.2V * 2 = 8.4V reference
    // Actual divider may vary - adjust multiplier based on testing
    float voltage = (adcValue / 4095.0) * 3.3 * 2.0;  // Assuming 2:1 divider

    // Convert voltage to percentage (3.2V = 0%, 4.2V = 100%)
    float percent = ((voltage - 3.2) / (4.2 - 3.2)) * 100.0;
    percent = constrain(percent, 0, 100);

    displayStatus.batteryPercent = percent;
}
