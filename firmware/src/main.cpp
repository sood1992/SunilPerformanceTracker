/**
 * Dog Walker GPS Tracker Firmware - MINIMAL TEST VERSION
 *
 * This is a minimal version to debug boot loop issues.
 * Features are added incrementally to identify the problem.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include "config.h"

// Only include libraries as needed for testing
#include <SD.h>
#include <Adafruit_ADXL345_U.h>
#include <ArduinoJson.h>

// ============================================================================
// Global Objects - initialized lazily to avoid boot crashes
// ============================================================================

HardwareSerial SerialAT(1);
Adafruit_ADXL345_Unified* accel = nullptr;  // Lazy init

// SD Card SPI - use default VSPI
SPIClass* sdSPI = nullptr;  // Lazy init

// ============================================================================
// State Variables
// ============================================================================

bool sdCardReady = false;
bool modemReady = false;
bool gpsEnabled = false;
bool accelReady = false;

// GPS Data
struct GPSData {
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    double speed = 0;
    int satellites = 0;
    bool valid = false;
} currentGPS;

// Accelerometer Data
struct AccelData {
    float x = 0;
    float y = 0;
    float z = 0;
    float magnitude = 0;
    bool isMoving = false;
} currentAccel;

// Walk Session
struct WalkSession {
    unsigned long startTime = 0;
    unsigned long endTime = 0;
    double totalDistance = 0;
    double maxSpeed = 0;
    double avgSpeed = 0;
    int dataPoints = 0;
    bool isActive = false;
    String filename = "";
} currentWalk;

// Timing
unsigned long lastGPSUpdate = 0;
unsigned long lastLogTime = 0;
unsigned long lastActivityTime = 0;
unsigned long lastStatusPrint = 0;

// ============================================================================
// Forward Declarations
// ============================================================================

void initModem();
void initGPS();
void initAccelerometer();
void initSDCard();
void initWiFi();
void readGPS();
void readAccelerometer();
bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout);
String sendATCommandGetResponse(const char* cmd, unsigned long timeout);

// ============================================================================
// Setup
// ============================================================================

void setup() {
    // Initialize debug serial FIRST
    Serial.begin(115200);

    // Wait for serial with timeout
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

    // Step 1: I2C
    Serial.println("[STEP 1/5] Initializing I2C...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.printf("  SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println("  [OK]\n");
    delay(100);

    // Step 2: SD Card
    Serial.println("[STEP 2/5] Initializing SD Card...");
    initSDCard();
    delay(100);

    // Step 3: Accelerometer
    Serial.println("[STEP 3/5] Initializing Accelerometer...");
    initAccelerometer();
    delay(100);

    // Step 4: Modem
    Serial.println("[STEP 4/5] Initializing Modem...");
    Serial.println("  (Requires battery connection!)");
    initModem();
    delay(100);

    // Step 5: GPS
    Serial.println("[STEP 5/5] Initializing GPS...");
    initGPS();
    delay(100);

    // WiFi
    Serial.println("[WIFI] Connecting...");
    initWiFi();

    // Summary
    Serial.println();
    Serial.println("============================================");
    Serial.println("           INITIALIZATION SUMMARY");
    Serial.println("============================================");
    Serial.printf("  SD Card:       %s\n", sdCardReady ? "OK" : "FAIL");
    Serial.printf("  Accelerometer: %s\n", accelReady ? "OK" : "FAIL");
    Serial.printf("  Modem:         %s\n", modemReady ? "OK" : "FAIL");
    Serial.printf("  GPS:           %s\n", gpsEnabled ? "OK" : "FAIL");
    Serial.printf("  WiFi:          %s\n", WiFi.status() == WL_CONNECTED ? "OK" : "FAIL");
    Serial.println("============================================");
    Serial.println();
    Serial.println("System ready. Monitoring...");
    Serial.println();
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    unsigned long now = millis();

    // Read sensors
    if (now - lastGPSUpdate >= 1000) {
        readGPS();
        lastGPSUpdate = now;
    }
    readAccelerometer();

    // Print status every 5 seconds
    if (now - lastStatusPrint >= 5000) {
        Serial.println("---------------------------------------------");
        Serial.printf("[TIME]  Uptime: %lu sec\n", now / 1000);

        if (currentGPS.valid) {
            Serial.printf("[GPS]   Lat: %.6f, Lon: %.6f\n", currentGPS.latitude, currentGPS.longitude);
            Serial.printf("        Speed: %.1f km/h, Sats: %d\n", currentGPS.speed, currentGPS.satellites);
        } else {
            Serial.println("[GPS]   Waiting for fix...");
        }

        Serial.printf("[ACCEL] X=%.2f Y=%.2f Z=%.2f Mag=%.2f\n",
            currentAccel.x, currentAccel.y, currentAccel.z, currentAccel.magnitude);
        Serial.printf("        Moving: %s\n", currentAccel.isMoving ? "YES" : "NO");

        Serial.printf("[WIFI]  %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");

        lastStatusPrint = now;
    }

    delay(10);
}

// ============================================================================
// Initialization Functions
// ============================================================================

void initSDCard() {
    // Lazy init SPI
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

    // Create directories
    if (!SD.exists("/walks")) SD.mkdir("/walks");
    if (!SD.exists("/pending")) SD.mkdir("/pending");

    sdCardReady = true;
    Serial.println("  [OK]");
}

void initAccelerometer() {
    // Lazy init
    accel = new Adafruit_ADXL345_Unified(12345);

    if (!accel->begin(ADXL345_ADDRESS)) {
        Serial.println("  [FAIL] ADXL345 not found");
        Serial.printf("  Check: SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
        accelReady = false;
        return;
    }

    accel->setRange(ADXL345_RANGE_4_G);
    accel->setDataRate(ADXL345_DATARATE_50_HZ);

    accelReady = true;
    Serial.println("  [OK] ADXL345 initialized");
}

void initModem() {
    // Power pins
    pinMode(MODEM_POWER_ON_PIN, OUTPUT);
    pinMode(MODEM_PWRKEY_PIN, OUTPUT);
    pinMode(MODEM_RESET_PIN, OUTPUT);

    Serial.println("  Powering on modem...");
    digitalWrite(MODEM_POWER_ON_PIN, HIGH);
    delay(100);

    // Power key sequence
    digitalWrite(MODEM_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(MODEM_PWRKEY_PIN, HIGH);
    delay(1000);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);

    // Start serial
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(3000);

    // Check modem
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

void initGPS() {
    if (!modemReady) {
        Serial.println("  [SKIP] Modem not ready");
        return;
    }

    // Power on GPS
    if (sendATCommand("AT+CGNSPWR=1", "OK", 2000)) {
        gpsEnabled = true;
        Serial.println("  [OK] GPS powered on");
    } else {
        Serial.println("  [FAIL] GPS power on failed");
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
    } else {
        Serial.println("  [FAIL] Could not connect");
    }
}

// ============================================================================
// Sensor Reading Functions
// ============================================================================

void readGPS() {
    if (!gpsEnabled) return;

    // Request GPS info
    String response = sendATCommandGetResponse("AT+CGNSINF", 1000);

    // Parse response: +CGNSINF: <mode>,<fix>,<date>,<lat>,<lon>,<alt>,<speed>,...
    if (response.indexOf("+CGNSINF:") >= 0) {
        int start = response.indexOf(":") + 2;
        String data = response.substring(start);

        // Split by commas
        int idx = 0;
        String parts[20];
        int partCount = 0;

        while (data.length() > 0 && partCount < 20) {
            idx = data.indexOf(",");
            if (idx == -1) {
                parts[partCount++] = data;
                break;
            }
            parts[partCount++] = data.substring(0, idx);
            data = data.substring(idx + 1);
        }

        if (partCount >= 7) {
            int fix = parts[1].toInt();
            if (fix == 1) {
                currentGPS.valid = true;
                currentGPS.latitude = parts[3].toDouble();
                currentGPS.longitude = parts[4].toDouble();
                currentGPS.altitude = parts[5].toDouble();
                currentGPS.speed = parts[6].toDouble() * 1.852; // knots to km/h
            } else {
                currentGPS.valid = false;
            }
        }
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

    // Detect movement (deviation from gravity ~9.8)
    float deviation = abs(currentAccel.magnitude - 9.8);
    currentAccel.isMoving = (deviation > ACTIVITY_THRESHOLD);
}

// ============================================================================
// AT Command Helpers
// ============================================================================

bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout) {
    SerialAT.println(cmd);

    unsigned long start = millis();
    String response = "";

    while (millis() - start < timeout) {
        if (SerialAT.available()) {
            char c = SerialAT.read();
            response += c;
            if (response.indexOf(expected) >= 0) {
                return true;
            }
        }
    }
    return false;
}

String sendATCommandGetResponse(const char* cmd, unsigned long timeout) {
    SerialAT.println(cmd);

    unsigned long start = millis();
    String response = "";

    while (millis() - start < timeout) {
        if (SerialAT.available()) {
            response += (char)SerialAT.read();
        }
    }
    return response;
}
