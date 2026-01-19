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
#include "config.h"

// ============================================================================
// Global Objects
// ============================================================================

HardwareSerial SerialAT(1);   // Modem on UART1
HardwareSerial SerialGPS(2);  // GPS L76K on UART2
TinyGPSPlus gps;
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

void initPower();
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

    // Step 0: Board Power
    Serial.println("[STEP 0/6] Setting board power...");
    initPower();
    Serial.println("  [OK] Board power pin HIGH\n");
    delay(100);

    // Step 1: I2C (on new pins to avoid GPS conflict)
    Serial.println("[STEP 1/6] Initializing I2C...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.printf("  SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println("  [OK]\n");
    delay(100);

    // Step 2: SD Card
    Serial.println("[STEP 2/6] Initializing SD Card...");
    initSDCard();
    delay(100);

    // Step 3: Accelerometer
    Serial.println("[STEP 3/6] Initializing Accelerometer...");
    initAccelerometer();
    delay(100);

    // Step 4: GPS (L76K direct UART)
    Serial.println("[STEP 4/6] Initializing GPS (L76K)...");
    initGPS();
    delay(100);

    // Step 5: Modem (for 4G, not GPS)
    Serial.println("[STEP 5/6] Initializing Modem (4G)...");
    Serial.println("  (Requires battery connection!)");
    initModem();
    delay(100);

    // Step 6: WiFi
    Serial.println("[STEP 6/6] Connecting WiFi...");
    initWiFi();

    // Summary
    Serial.println();
    Serial.println("============================================");
    Serial.println("           INITIALIZATION SUMMARY");
    Serial.println("============================================");
    Serial.printf("  SD Card:       %s\n", sdCardReady ? "OK" : "FAIL");
    Serial.printf("  Accelerometer: %s\n", accelReady ? "OK" : "FAIL");
    Serial.printf("  GPS (L76K):    %s\n", gpsEnabled ? "OK" : "FAIL");
    Serial.printf("  Modem (4G):    %s\n", modemReady ? "OK" : "FAIL");
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

    // Read GPS continuously
    readGPS();

    // Read accelerometer
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
            Serial.printf("[GPS]   Waiting for fix... (chars: %lu)\n", gps.charsProcessed());
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

void initPower() {
    // Keep board powered when USB is disconnected
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);
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
    // Wake up GPS module
    pinMode(GPS_WAKEUP_PIN, OUTPUT);
    digitalWrite(GPS_WAKEUP_PIN, HIGH);
    delay(100);

    // Initialize GPS serial (L76K at 9600 baud)
    SerialGPS.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    delay(500);

    // Check for GPS data
    Serial.printf("  GPS UART: TX=GPIO%d, RX=GPIO%d @ %d baud\n",
        GPS_TX_PIN, GPS_RX_PIN, GPS_BAUDRATE);

    // Wait briefly to see if we get any data
    unsigned long start = millis();
    int chars = 0;
    while (millis() - start < 2000) {
        if (SerialGPS.available()) {
            SerialGPS.read();
            chars++;
        }
    }

    if (chars > 0) {
        gpsEnabled = true;
        Serial.printf("  [OK] GPS responding (%d chars received)\n", chars);
    } else {
        gpsEnabled = true;  // Still enable, might just need time for fix
        Serial.println("  [OK] GPS initialized (waiting for data)");
    }
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
// Sensor Reading Functions
// ============================================================================

void readGPS() {
    // Read all available GPS data from L76K
    while (SerialGPS.available() > 0) {
        char c = SerialGPS.read();
        gps.encode(c);
    }

    // Update GPS data if valid
    if (gps.location.isUpdated() && gps.location.isValid()) {
        currentGPS.valid = true;
        currentGPS.latitude = gps.location.lat();
        currentGPS.longitude = gps.location.lng();
        currentGPS.altitude = gps.altitude.meters();
        currentGPS.speed = gps.speed.kmph();
        currentGPS.satellites = gps.satellites.value();
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

            // Read file content
            String content = "";
            while (file.available()) {
                content += (char)file.read();
            }
            file.close();

            // Parse the file: first line is metadata, rest are points
            int firstNewline = content.indexOf('\n');
            if (firstNewline < 0) {
                Serial.println("    [SKIP] Invalid file format");
                file = dir.openNextFile();
                continue;
            }

            String metaLine = content.substring(0, firstNewline);
            String pointsData = content.substring(firstNewline + 1);

            // Parse metadata
            JsonDocument metaDoc;
            DeserializationError metaErr = deserializeJson(metaDoc, metaLine);
            if (metaErr) {
                Serial.printf("    [SKIP] Invalid metadata: %s\n", metaErr.c_str());
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

            // Send to server
            HTTPClient http;
            String url = String(API_BASE_URL) + String(API_ENDPOINT);
            http.begin(url);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("X-Device-ID", DEVICE_ID);
            http.setTimeout(30000);  // 30 second timeout

            String payload;
            serializeJson(uploadDoc, payload);

            Serial.printf("    Payload size: %d bytes\n", payload.length());

            int httpCode = http.POST(payload);
            if (httpCode == 200 || httpCode == 201) {
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
                    } else {
                        Serial.println("    [WARN] Uploaded but archive failed - keeping in pending");
                        if (srcFile) srcFile.close();
                        if (dstFile) dstFile.close();
                    }
                }
            } else {
                Serial.printf("    [FAIL] HTTP %d\n", httpCode);
                String response = http.getString();
                Serial.printf("    Response: %s\n", response.substring(0, 200).c_str());
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
