/**
 * Dog Walker GPS Tracker Firmware
 *
 * Hardware: LilyGo T-A7670G R2 + ADXL345 Accelerometer
 *
 * Features:
 * - GPS tracking via A7670G modem
 * - Activity detection via ADXL345
 * - Data logging to SD card
 * - WiFi upload to backend when available
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

// Hardware Serial for modem communication
HardwareSerial SerialAT(1);

// GPS Parser
TinyGPSPlus gps;

// ADXL345 Accelerometer
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// SD Card SPI
SPIClass sdSPI(VSPI);

// ============================================================================
// Data Structures
// ============================================================================

struct GPSData {
    double latitude;
    double longitude;
    double altitude;
    double speed;        // km/h
    double course;       // degrees
    int satellites;
    bool valid;
    unsigned long timestamp;
};

struct AccelData {
    float x;
    float y;
    float z;
    float magnitude;
    bool isMoving;
};

struct WalkSession {
    unsigned long startTime;
    unsigned long endTime;
    double totalDistance;     // meters
    double maxSpeed;          // km/h
    double avgSpeed;          // km/h
    int dataPoints;
    bool isActive;
    String filename;
};

// ============================================================================
// Global Variables
// ============================================================================

GPSData currentGPS;
AccelData currentAccel;
WalkSession currentWalk;

GPSData lastValidGPS;
unsigned long lastGPSUpdate = 0;
unsigned long lastLogTime = 0;
unsigned long lastActivityTime = 0;

bool sdCardReady = false;
bool modemReady = false;
bool gpsEnabled = false;

// ============================================================================
// Function Prototypes
// ============================================================================

void initModem();
void initGPS();
void initAccelerometer();
void initSDCard();
void initWiFi();

void readGPS();
void readAccelerometer();
void processATResponse(String response);

void startWalk();
void endWalk();
void logWalkData();
void uploadPendingWalks();

double calculateDistance(double lat1, double lon1, double lat2, double lon2);
String getTimestamp();
bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout);
String sendATCommandGetResponse(const char* cmd, unsigned long timeout);

// ============================================================================
// Setup
// ============================================================================

void setup() {
    // Initialize debug serial
    DEBUG_SERIAL.begin(DEBUG_BAUDRATE);
    delay(1000);
    DEBUG_PRINTLN("\n\n=================================");
    DEBUG_PRINTLN("Dog Walker GPS Tracker");
    DEBUG_PRINTLN("=================================\n");

    // Initialize I2C for ADXL345
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Initialize components
    initSDCard();
    initAccelerometer();
    initModem();
    initGPS();

    // Try to connect to WiFi and upload any pending data
    initWiFi();

    // Initialize walk session
    currentWalk.isActive = false;
    currentWalk.totalDistance = 0;
    currentWalk.maxSpeed = 0;
    currentWalk.avgSpeed = 0;
    currentWalk.dataPoints = 0;

    DEBUG_PRINTLN("\n=================================");
    DEBUG_PRINTLN("Initialization Complete!");
    DEBUG_PRINTLN("=================================\n");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    unsigned long now = millis();

    // Read GPS data
    if (now - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
        readGPS();
        lastGPSUpdate = now;
    }

    // Read accelerometer data
    readAccelerometer();

    // Update activity time if moving
    if (currentAccel.isMoving || (currentGPS.valid && currentGPS.speed > WALK_START_SPEED)) {
        lastActivityTime = now;
    }

    // Walk state machine
    if (!currentWalk.isActive) {
        // Check if walk should start
        if (currentGPS.valid && currentGPS.speed > WALK_START_SPEED && currentAccel.isMoving) {
            startWalk();
        }
    } else {
        // Log data during walk
        if (now - lastLogTime >= LOG_INTERVAL) {
            logWalkData();
            lastLogTime = now;
        }

        // Check if walk should end (inactivity timeout)
        if (now - lastActivityTime > WALK_END_TIMEOUT) {
            endWalk();
        }
    }

    // Periodically check WiFi and upload pending data
    static unsigned long lastWiFiCheck = 0;
    if (now - lastWiFiCheck > 60000) { // Check every minute
        if (WiFi.status() != WL_CONNECTED) {
            initWiFi();
        }
        if (WiFi.status() == WL_CONNECTED) {
            uploadPendingWalks();
        }
        lastWiFiCheck = now;
    }

    // Small delay to prevent watchdog issues
    delay(10);
}

// ============================================================================
// Initialization Functions
// ============================================================================

void initModem() {
    DEBUG_PRINTLN("Initializing A7670G modem...");

    // Configure modem power pins
    pinMode(MODEM_POWER_ON_PIN, OUTPUT);
    pinMode(MODEM_PWRKEY_PIN, OUTPUT);
    pinMode(MODEM_RESET_PIN, OUTPUT);

    // Power on sequence
    digitalWrite(MODEM_POWER_ON_PIN, HIGH);
    delay(100);

    // Power key pulse
    digitalWrite(MODEM_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(MODEM_PWRKEY_PIN, HIGH);
    delay(1000);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);

    // Initialize modem serial
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(3000);

    // Wait for modem to be ready
    int retries = 10;
    while (retries > 0) {
        if (sendATCommand("AT", "OK", 1000)) {
            DEBUG_PRINTLN("Modem responded!");
            modemReady = true;
            break;
        }
        retries--;
        delay(500);
    }

    if (modemReady) {
        // Basic modem configuration
        sendATCommand("ATE0", "OK", 1000);      // Disable echo
        sendATCommand("AT+CMEE=2", "OK", 1000); // Verbose error messages

        DEBUG_PRINTLN("Modem initialized successfully!");
    } else {
        DEBUG_PRINTLN("ERROR: Modem not responding!");
    }
}

void initGPS() {
    if (!modemReady) {
        DEBUG_PRINTLN("Cannot init GPS - modem not ready");
        return;
    }

    DEBUG_PRINTLN("Initializing GPS...");

    // Enable GPS on A7670G
    // The A7670G has integrated GPS (L76K) accessible via AT commands

    // Power on GPS
    if (sendATCommand("AT+CGNSPWR=1", "OK", 2000)) {
        DEBUG_PRINTLN("GPS powered on");
        gpsEnabled = true;
    } else {
        // Try alternate command for some firmware versions
        if (sendATCommand("AT+CGPS=1", "OK", 2000)) {
            DEBUG_PRINTLN("GPS enabled (alternate command)");
            gpsEnabled = true;
        }
    }

    if (gpsEnabled) {
        // Set GPS mode - standalone
        sendATCommand("AT+CGPSINFO", "OK", 1000);
        DEBUG_PRINTLN("GPS initialized successfully!");
    } else {
        DEBUG_PRINTLN("WARNING: GPS initialization failed");
    }
}

void initAccelerometer() {
    DEBUG_PRINTLN("Initializing ADXL345 accelerometer...");

    if (!accel.begin(ADXL345_ADDRESS)) {
        DEBUG_PRINTLN("ERROR: ADXL345 not found at address 0x53!");
        DEBUG_PRINTLN("Check wiring: SDA->GPIO21, SCL->GPIO22, CS->3.3V, SDO->GND");
        return;
    }

    // Configure accelerometer
    accel.setRange(ADXL345_RANGE_4_G);  // +/- 4G range
    accel.setDataRate(ADXL345_DATARATE_50_HZ);  // 50Hz update rate

    DEBUG_PRINTLN("ADXL345 initialized successfully!");

    // Print sensor details
    sensor_t sensor;
    accel.getSensor(&sensor);
    DEBUG_PRINTF("Sensor: %s, Range: +/-%dG\n", sensor.name, 4);
}

void initSDCard() {
    DEBUG_PRINTLN("Initializing SD card...");

    // Initialize SPI with custom pins
    sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN, sdSPI)) {
        DEBUG_PRINTLN("ERROR: SD card initialization failed!");
        DEBUG_PRINTLN("Check: Card inserted? Pins correct?");
        return;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        DEBUG_PRINTLN("ERROR: No SD card attached!");
        return;
    }

    DEBUG_PRINT("SD Card Type: ");
    switch (cardType) {
        case CARD_MMC:  DEBUG_PRINTLN("MMC"); break;
        case CARD_SD:   DEBUG_PRINTLN("SDSC"); break;
        case CARD_SDHC: DEBUG_PRINTLN("SDHC"); break;
        default:        DEBUG_PRINTLN("UNKNOWN"); break;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    DEBUG_PRINTF("SD Card Size: %lluMB\n", cardSize);

    // Create walks directory if it doesn't exist
    if (!SD.exists("/walks")) {
        SD.mkdir("/walks");
        DEBUG_PRINTLN("Created /walks directory");
    }

    if (!SD.exists("/pending")) {
        SD.mkdir("/pending");
        DEBUG_PRINTLN("Created /pending directory");
    }

    sdCardReady = true;
    DEBUG_PRINTLN("SD card initialized successfully!");
}

void initWiFi() {
    DEBUG_PRINTLN("Connecting to WiFi...");
    DEBUG_PRINTF("SSID: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        DEBUG_PRINT(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTLN("\nWiFi connected!");
        DEBUG_PRINTF("IP: %s\n", WiFi.localIP().toString().c_str());

        // Sync time via NTP
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        DEBUG_PRINTLN("Time synced via NTP");
    } else {
        DEBUG_PRINTLN("\nWiFi connection failed - will retry later");
    }
}

// ============================================================================
// Sensor Reading Functions
// ============================================================================

void readGPS() {
    if (!gpsEnabled) return;

    // Request GPS info from modem
    String response = sendATCommandGetResponse("AT+CGNSINF", 2000);

    // Parse CGNSINF response
    // Format: +CGNSINF: <GNSS run status>,<Fix status>,<UTC>,<lat>,<lon>,<alt>,<speed>,<course>,...
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

        if (partCount >= 8) {
            int fixStatus = parts[1].toInt();

            if (fixStatus == 1) {
                currentGPS.valid = true;
                currentGPS.latitude = parts[3].toDouble();
                currentGPS.longitude = parts[4].toDouble();
                currentGPS.altitude = parts[5].toDouble();
                currentGPS.speed = parts[6].toDouble() * 1.852; // knots to km/h
                currentGPS.course = parts[7].toDouble();
                currentGPS.timestamp = millis();

                // Store as last valid position
                lastValidGPS = currentGPS;

                DEBUG_PRINTF("GPS: %.6f, %.6f | Speed: %.1f km/h | Alt: %.1fm\n",
                    currentGPS.latitude, currentGPS.longitude,
                    currentGPS.speed, currentGPS.altitude);
            } else {
                currentGPS.valid = false;
                DEBUG_PRINTLN("GPS: No fix yet...");
            }
        }
    }

    // Alternative: Try CGPSINFO command
    if (!currentGPS.valid) {
        response = sendATCommandGetResponse("AT+CGPSINFO", 2000);
        if (response.indexOf("+CGPSINFO:") >= 0 && response.indexOf(",,,,") == -1) {
            // Parse CGPSINFO format
            // +CGPSINFO: [lat],[N/S],[lon],[E/W],[date],[UTC],[alt],[speed],[course]
            int colonIdx = response.indexOf(":");
            if (colonIdx < 0) return;

            int start = colonIdx + 2;
            String data = response.substring(start);

            // Split by commas into parts
            String cgpsParts[10];
            int cgpsPartCount = 0;
            while (data.length() > 0 && cgpsPartCount < 10) {
                int commaIdx = data.indexOf(",");
                if (commaIdx == -1) {
                    cgpsParts[cgpsPartCount++] = data;
                    break;
                }
                cgpsParts[cgpsPartCount++] = data.substring(0, commaIdx);
                data = data.substring(commaIdx + 1);
            }

            // Need at least lat, N/S, lon, E/W
            if (cgpsPartCount >= 4 && cgpsParts[0].length() > 0 && cgpsParts[2].length() > 0) {
                // Parse latitude (NMEA format: DDMM.MMMM)
                String latStr = cgpsParts[0];
                double latDeg = latStr.substring(0, 2).toDouble();
                double latMin = latStr.substring(2).toDouble();
                currentGPS.latitude = latDeg + (latMin / 60.0);
                if (cgpsParts[1] == "S") currentGPS.latitude = -currentGPS.latitude;

                // Parse longitude (NMEA format: DDDMM.MMMM)
                String lonStr = cgpsParts[2];
                double lonDeg = lonStr.substring(0, 3).toDouble();
                double lonMin = lonStr.substring(3).toDouble();
                currentGPS.longitude = lonDeg + (lonMin / 60.0);
                if (cgpsParts[3] == "W") currentGPS.longitude = -currentGPS.longitude;

                // Parse altitude (index 6) and speed (index 7) if available
                if (cgpsPartCount > 6 && cgpsParts[6].length() > 0) {
                    currentGPS.altitude = cgpsParts[6].toDouble();
                }
                if (cgpsPartCount > 7 && cgpsParts[7].length() > 0) {
                    currentGPS.speed = cgpsParts[7].toDouble() * 1.852; // knots to km/h
                }
                if (cgpsPartCount > 8 && cgpsParts[8].length() > 0) {
                    currentGPS.course = cgpsParts[8].toDouble();
                }

                currentGPS.valid = true;
                currentGPS.timestamp = millis();
                lastValidGPS = currentGPS;

                DEBUG_PRINTF("GPS (CGPSINFO): %.6f, %.6f | Speed: %.1f km/h\n",
                    currentGPS.latitude, currentGPS.longitude, currentGPS.speed);
            }
        }
    }
}

void readAccelerometer() {
    sensors_event_t event;
    accel.getEvent(&event);

    currentAccel.x = event.acceleration.x;
    currentAccel.y = event.acceleration.y;
    currentAccel.z = event.acceleration.z;

    // Calculate magnitude (removing gravity ~9.8)
    float rawMag = sqrt(pow(currentAccel.x, 2) + pow(currentAccel.y, 2) + pow(currentAccel.z, 2));
    currentAccel.magnitude = abs(rawMag - 9.8);

    // Determine if moving
    currentAccel.isMoving = currentAccel.magnitude > ACTIVITY_THRESHOLD;

    if (currentAccel.isMoving) {
        DEBUG_PRINTF("Accel: X=%.2f Y=%.2f Z=%.2f Mag=%.2f MOVING\n",
            currentAccel.x, currentAccel.y, currentAccel.z, currentAccel.magnitude);
    }
}

// ============================================================================
// Walk Session Functions
// ============================================================================

void startWalk() {
    DEBUG_PRINTLN("\n*** WALK STARTED ***\n");

    currentWalk.isActive = true;
    currentWalk.startTime = millis();
    currentWalk.endTime = 0;
    currentWalk.totalDistance = 0;
    currentWalk.maxSpeed = 0;
    currentWalk.avgSpeed = 0;
    currentWalk.dataPoints = 0;

    // Create filename based on timestamp
    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    char filename[64];
    snprintf(filename, sizeof(filename), "/walks/walk_%04d%02d%02d_%02d%02d%02d.json",
        timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
        timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    currentWalk.filename = String(filename);

    // Create initial file structure
    if (sdCardReady) {
        File file = SD.open(currentWalk.filename, FILE_WRITE);
        if (file) {
            JsonDocument doc;
            doc["deviceId"] = DEVICE_ID;
            doc["startTime"] = now;
            doc["startLat"] = currentGPS.latitude;
            doc["startLon"] = currentGPS.longitude;
            doc["points"] = JsonArray();

            serializeJson(doc, file);
            file.close();

            DEBUG_PRINTF("Created walk file: %s\n", currentWalk.filename.c_str());
        }
    }

    lastLogTime = millis();
    lastActivityTime = millis();
}

void endWalk() {
    if (!currentWalk.isActive) return;

    currentWalk.endTime = millis();
    unsigned long duration = currentWalk.endTime - currentWalk.startTime;

    DEBUG_PRINTLN("\n*** WALK ENDED ***");
    DEBUG_PRINTF("Duration: %lu seconds\n", duration / 1000);
    DEBUG_PRINTF("Distance: %.2f meters\n", currentWalk.totalDistance);
    DEBUG_PRINTF("Avg Speed: %.2f km/h\n", currentWalk.avgSpeed);
    DEBUG_PRINTF("Max Speed: %.2f km/h\n", currentWalk.maxSpeed);
    DEBUG_PRINTF("Data Points: %d\n\n", currentWalk.dataPoints);

    // Check minimum walk duration
    if (duration < MIN_WALK_DURATION) {
        DEBUG_PRINTLN("Walk too short - discarding");
        if (sdCardReady && currentWalk.filename.length() > 0) {
            SD.remove(currentWalk.filename.c_str());
        }
    } else {
        // Finalize the walk file
        finalizeWalkFile();

        // Move to pending uploads
        if (sdCardReady) {
            String pendingPath = "/pending/" + currentWalk.filename.substring(7);
            SD.rename(currentWalk.filename.c_str(), pendingPath.c_str());
            DEBUG_PRINTF("Moved to pending: %s\n", pendingPath.c_str());
        }
    }

    currentWalk.isActive = false;
}

void logWalkData() {
    if (!currentWalk.isActive || !sdCardReady) return;

    // Only log if we have valid GPS
    if (!currentGPS.valid) return;

    // Calculate distance from last point
    if (lastValidGPS.valid && currentWalk.dataPoints > 0) {
        double dist = calculateDistance(
            lastValidGPS.latitude, lastValidGPS.longitude,
            currentGPS.latitude, currentGPS.longitude
        );
        currentWalk.totalDistance += dist;
    }

    // Update max speed
    if (currentGPS.speed > currentWalk.maxSpeed) {
        currentWalk.maxSpeed = currentGPS.speed;
    }

    // Calculate running average speed
    currentWalk.dataPoints++;
    currentWalk.avgSpeed = ((currentWalk.avgSpeed * (currentWalk.dataPoints - 1)) + currentGPS.speed) / currentWalk.dataPoints;

    // Append data point to file
    File file = SD.open(currentWalk.filename, FILE_APPEND);
    if (file) {
        // Write as NDJSON (newline-delimited JSON) for efficiency
        JsonDocument point;
        point["t"] = (millis() - currentWalk.startTime) / 1000.0;  // Time offset in seconds
        point["lat"] = currentGPS.latitude;
        point["lon"] = currentGPS.longitude;
        point["alt"] = currentGPS.altitude;
        point["spd"] = currentGPS.speed;
        point["ax"] = currentAccel.x;
        point["ay"] = currentAccel.y;
        point["az"] = currentAccel.z;

        size_t written = file.print("\n");
        written += serializeJson(point, file);
        file.close();

        if (written == 0) {
            DEBUG_PRINTLN("WARNING: Failed to write GPS point to SD card!");
        }
    } else {
        DEBUG_PRINTLN("ERROR: Could not open file for writing!");
    }

    DEBUG_PRINTF("Logged point #%d: %.6f, %.6f @ %.1f km/h\n",
        currentWalk.dataPoints, currentGPS.latitude, currentGPS.longitude, currentGPS.speed);
}

void finalizeWalkFile() {
    // The file format will be:
    // Line 1: Initial JSON with metadata
    // Lines 2+: Individual point data (NDJSON format)
    // This is efficient for both writing and parsing

    DEBUG_PRINTLN("Walk file finalized");
}

// ============================================================================
// Upload Functions
// ============================================================================

void uploadPendingWalks() {
    if (!sdCardReady || WiFi.status() != WL_CONNECTED) return;

    DEBUG_PRINTLN("Checking for pending uploads...");

    File dir = SD.open("/pending");
    if (!dir || !dir.isDirectory()) {
        DEBUG_PRINTLN("No pending directory");
        return;
    }

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            DEBUG_PRINTF("Uploading: %s\n", filename.c_str());

            // Read file content
            String content = "";
            while (file.available()) {
                content += (char)file.read();
            }
            file.close();

            // Parse and restructure for upload
            if (uploadWalkData(filename, content)) {
                // Delete uploaded file
                String fullPath = "/pending/" + filename;
                SD.remove(fullPath.c_str());
                DEBUG_PRINTLN("Upload successful, file deleted");
            } else {
                DEBUG_PRINTLN("Upload failed, will retry later");
            }
        }
        file = dir.openNextFile();
    }
    dir.close();
}

bool uploadWalkData(String filename, String content) {
    // Build upload URL
    String url = String(API_BASE_URL) + String(API_ENDPOINT);

    // Parse the content and create proper JSON
    JsonDocument uploadDoc;
    uploadDoc["deviceId"] = DEVICE_ID;
    uploadDoc["filename"] = filename;

    // Parse first line as metadata
    int firstNewline = content.indexOf('\n');
    if (firstNewline > 0) {
        String metaJson = content.substring(0, firstNewline);
        JsonDocument metaDoc;
        DeserializationError err = deserializeJson(metaDoc, metaJson);
        if (err) {
            DEBUG_PRINTF("Failed to parse metadata: %s\n", err.c_str());
            return false;
        }

        uploadDoc["startTime"] = metaDoc["startTime"];
        uploadDoc["startLat"] = metaDoc["startLat"];
        uploadDoc["startLon"] = metaDoc["startLon"];
    }

    // Parse remaining lines as points
    JsonArray points = uploadDoc["points"].to<JsonArray>();
    String remaining = content.substring(firstNewline + 1);

    while (remaining.length() > 0) {
        int lineEnd = remaining.indexOf('\n');
        String line;
        if (lineEnd == -1) {
            line = remaining;
            remaining = "";
        } else {
            line = remaining.substring(0, lineEnd);
            remaining = remaining.substring(lineEnd + 1);
        }

        if (line.length() > 0) {
            JsonDocument pointDoc;
            DeserializationError err = deserializeJson(pointDoc, line);
            if (!err && pointDoc.containsKey("lat") && pointDoc.containsKey("lon")) {
                points.add(pointDoc);
            }
        }
    }

    // Serialize for upload
    String uploadJson;
    serializeJson(uploadDoc, uploadJson);

    // Retry loop with exponential backoff
    for (int attempt = 0; attempt < UPLOAD_RETRY_COUNT; attempt++) {
        if (attempt > 0) {
            unsigned long backoffMs = UPLOAD_RETRY_DELAY * (1 << (attempt - 1)); // 5s, 10s, 20s
            DEBUG_PRINTF("Retry attempt %d after %lu ms...\n", attempt + 1, backoffMs);
            delay(backoffMs);
        }

        HTTPClient http;
        http.begin(url);
        http.setTimeout(30000); // 30 second timeout
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Device-ID", DEVICE_ID);

        int httpCode = http.POST(uploadJson);

        if (httpCode == 200 || httpCode == 201) {
            DEBUG_PRINTF("Upload successful: HTTP %d\n", httpCode);
            http.end();
            return true;
        }

        DEBUG_PRINTF("Upload attempt %d failed: HTTP %d\n", attempt + 1, httpCode);
        http.end();
    }

    DEBUG_PRINTLN("All upload attempts failed");
    return false;
}

// ============================================================================
// Utility Functions
// ============================================================================

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    // Haversine formula
    const double R = 6371000; // Earth's radius in meters

    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLon = (lon2 - lon1) * PI / 180.0;

    double a = sin(dLat/2) * sin(dLat/2) +
               cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
               sin(dLon/2) * sin(dLon/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));

    return R * c;
}

bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout) {
    // Clear any pending data
    while (SerialAT.available()) {
        SerialAT.read();
    }

    // Send command
    SerialAT.println(cmd);
    DEBUG_PRINTF("AT> %s\n", cmd);

    // Wait for response
    String response = "";
    unsigned long start = millis();

    while (millis() - start < timeout) {
        while (SerialAT.available()) {
            char c = SerialAT.read();
            response += c;
        }

        if (response.indexOf(expected) >= 0) {
            DEBUG_PRINTF("AT< %s\n", response.c_str());
            return true;
        }

        if (response.indexOf("ERROR") >= 0) {
            DEBUG_PRINTF("AT< ERROR: %s\n", response.c_str());
            return false;
        }

        delay(10);
    }

    DEBUG_PRINTF("AT< TIMEOUT: %s\n", response.c_str());
    return false;
}

String sendATCommandGetResponse(const char* cmd, unsigned long timeout) {
    // Clear any pending data
    while (SerialAT.available()) {
        SerialAT.read();
    }

    // Send command
    SerialAT.println(cmd);

    // Wait for response
    String response = "";
    unsigned long start = millis();

    while (millis() - start < timeout) {
        while (SerialAT.available()) {
            char c = SerialAT.read();
            response += c;
        }

        if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
            break;
        }

        delay(10);
    }

    return response;
}
