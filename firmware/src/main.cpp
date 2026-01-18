/**
 * Dog Walker GPS Tracker Firmware
 * Hardware: LilyGo T-A7670G R2 + ADXL345 Accelerometer
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_ADXL345_U.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================================
// Global Objects - use pointers for lazy init
// ============================================================================

HardwareSerial SerialAT(1);
Adafruit_ADXL345_Unified* accel = nullptr;
SPIClass* sdSPI = nullptr;

// ============================================================================
// State Variables
// ============================================================================

bool sdCardReady = false;
bool modemReady = false;
bool gpsEnabled = false;
bool accelReady = false;

struct GPSData {
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    double speed = 0;
    int satellites = 0;
    bool valid = false;
} currentGPS;

struct AccelData {
    float x = 0;
    float y = 0;
    float z = 0;
    float magnitude = 0;
    bool isMoving = false;
} currentAccel;

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

GPSData lastValidGPS;
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
void startWalk();
void endWalk();
void logWalkData();
void uploadPendingWalks();
bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout);
String sendATCommandGetResponse(const char* cmd, unsigned long timeout);
double calculateDistance(double lat1, double lon1, double lat2, double lon2);

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
    Serial.println("System ready. Monitoring for walks...");
    Serial.println();
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    unsigned long now = millis();

    // Read sensors
    if (now - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
        readGPS();
        lastGPSUpdate = now;
    }
    readAccelerometer();

    // Update activity time if moving
    if (currentAccel.isMoving || (currentGPS.valid && currentGPS.speed > WALK_START_SPEED)) {
        lastActivityTime = now;
    }

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
        if (now - lastActivityTime > WALK_END_TIMEOUT) {
            endWalk();
        }
    }

    // WiFi check every minute
    static unsigned long lastWiFiCheck = 0;
    if (now - lastWiFiCheck > 60000) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WIFI] Reconnecting...");
            initWiFi();
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

    sdCardReady = true;
    Serial.println("  [OK]");
}

void initAccelerometer() {
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

void initGPS() {
    if (!modemReady) {
        Serial.println("  [SKIP] Modem not ready");
        return;
    }

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

    String response = sendATCommandGetResponse("AT+CGNSINF", 1000);

    if (response.indexOf("+CGNSINF:") >= 0) {
        int start = response.indexOf(":") + 2;
        String data = response.substring(start);

        String parts[20];
        int partCount = 0;
        int idx;

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
                currentGPS.speed = parts[6].toDouble() * 1.852;
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
    currentWalk.totalDistance = 0;
    currentWalk.maxSpeed = 0;
    currentWalk.avgSpeed = 0;
    currentWalk.dataPoints = 0;

    if (sdCardReady) {
        currentWalk.filename = "/walks/walk_" + String(millis()) + ".json";

        File file = SD.open(currentWalk.filename, FILE_WRITE);
        if (file) {
            JsonDocument doc;
            doc["deviceId"] = DEVICE_ID;
            doc["startTime"] = currentWalk.startTime;
            doc["startLat"] = currentGPS.latitude;
            doc["startLon"] = currentGPS.longitude;
            serializeJson(doc, file);
            file.close();
            Serial.printf("  File: %s\n", currentWalk.filename.c_str());
        }
    }

    lastValidGPS = currentGPS;
    lastActivityTime = millis();
}

void endWalk() {
    currentWalk.endTime = millis();
    unsigned long duration = currentWalk.endTime - currentWalk.startTime;

    Serial.println("\n*** WALK ENDED ***");
    Serial.printf("  Duration: %lu sec\n", duration / 1000);
    Serial.printf("  Distance: %.0f m\n", currentWalk.totalDistance);
    Serial.printf("  Points: %d\n", currentWalk.dataPoints);

    if (duration < MIN_WALK_DURATION) {
        Serial.println("  Walk too short - discarding");
        if (sdCardReady && currentWalk.filename.length() > 0) {
            SD.remove(currentWalk.filename.c_str());
        }
    } else if (sdCardReady && currentWalk.filename.length() > 0) {
        String pendingPath = "/pending/walk_" + String(currentWalk.startTime) + ".json";
        SD.rename(currentWalk.filename.c_str(), pendingPath.c_str());
        Serial.printf("  Moved to: %s\n", pendingPath.c_str());
    }

    currentWalk.isActive = false;
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

    File dir = SD.open("/pending");
    if (!dir || !dir.isDirectory()) return;

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            Serial.printf("  Uploading: %s\n", filename.c_str());

            String content = "";
            while (file.available()) {
                content += (char)file.read();
            }

            HTTPClient http;
            String url = String(API_BASE_URL) + String(API_ENDPOINT);
            http.begin(url);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("X-Device-ID", DEVICE_ID);

            JsonDocument uploadDoc;
            uploadDoc["deviceId"] = DEVICE_ID;
            uploadDoc["rawData"] = content;

            String payload;
            serializeJson(uploadDoc, payload);

            int httpCode = http.POST(payload);
            if (httpCode == 200 || httpCode == 201) {
                String fullPath = "/pending/" + filename;
                SD.remove(fullPath.c_str());
                Serial.println("    [OK] Uploaded and deleted");
            } else {
                Serial.printf("    [FAIL] HTTP %d\n", httpCode);
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

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000; // Earth radius in meters
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLon = (lon2 - lon1) * PI / 180.0;
    double a = sin(dLat/2) * sin(dLat/2) +
               cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
               sin(dLon/2) * sin(dLon/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}
