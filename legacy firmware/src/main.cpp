#include "config.h"
#include "gps_handler.h"
#include "sd_logger.h"
#include "wifi_uploader.h"
#include <Adafruit_ADXL345_U.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>
#include <Wire.h>

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASS = "YOUR_WIFI_PASS";
const char *API_URL = "http://your-railway-app.up.railway.app/api/upload";

enum WalkState { STATE_IDLE, STATE_WALKING };

WalkState currentState = STATE_IDLE;
unsigned long lastActivityTime = 0;
unsigned long walkStartTime = 0;
String currentWalkId = "";

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BOARD_PWR, OUTPUT);
  digitalWrite(PIN_BOARD_PWR, HIGH);

  pinMode(PIN_GPS_WAKEUP, OUTPUT);
  digitalWrite(PIN_GPS_WAKEUP, HIGH);

  delay(2000);
  Serial.println("System Initializing...");

  setupGPS();
  setupSD();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!accel.begin()) {
    Serial.println("ADXL345 Failed!");
  } else {
    accel.setRange(ADXL345_RANGE_16_G);
    Serial.println("ADXL345 Ready");
  }
}

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  if ((lat1 == 0 && lon1 == 0) || (lat2 == 0 && lon2 == 0))
    return 99999.0;

  double R = 6371e3;
  double phi1 = lat1 * PI / 180;
  double phi2 = lat2 * PI / 180;
  double dphi = (lat2 - lat1) * PI / 180;
  double dlam = (lon2 - lon1) * PI / 180;

  double a = sin(dphi / 2) * sin(dphi / 2) +
             cos(phi1) * cos(phi2) * sin(dlam / 2) * sin(dlam / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));

  return R * c;
}

void loop() {
  processGPS();

  sensors_event_t event;
  accel.getEvent(&event);

  float magnitude = sqrt(event.acceleration.x * event.acceleration.x +
                         event.acceleration.y * event.acceleration.y +
                         event.acceleration.z * event.acceleration.z);
  float deviation = abs(magnitude - 9.8);

  double speed = getGPSSpeed();
  bool hasFix = getGPSFix();
  double lat = getGPSLat();
  double lon = getGPSLon();

  bool isMoving = (speed > THRESHOLD_SPEED_KMPH) || (deviation > 1.5);

  if (currentState == STATE_IDLE) {
    if (isMoving && hasFix) {
      Serial.println("Movement detected! Starting Walk...");

      currentState = STATE_WALKING;
      walkStartTime = millis();
      lastActivityTime = millis();

      // USE GPS TIME FOR FILENAME to fix "Wrong Date"
      unsigned long gpsTime = getGPSEpochTime();
      if (gpsTime == 0)
        gpsTime = millis(); // Fallback

      currentWalkId = startNewLogSession(gpsTime);
    } else {
      static unsigned long lastSync = 0;
      if (millis() - lastSync > 60000) {
        lastSync = millis();
        syncData();
      }
    }
  } else if (currentState == STATE_WALKING) {
    if (isMoving) {
      lastActivityTime = millis();
    }

    static unsigned long lastLog = 0;
    if (millis() - lastLog > 1000) {
      lastLog = millis();
      logData(currentWalkId, lat, lon, speed, deviation, hasFix);

      Serial.printf("Walk Active: %.2f km/h | Act: %.2f\n", speed, deviation);
    }

    bool timeout = (millis() - lastActivityTime > TIMEOUT_WALK_END_MS);
    bool returnedHome =
        (calculateDistance(lat, lon, HOME_LAT, HOME_LON) < HOME_RADIUS_M) &&
        (millis() - walkStartTime > 60000);

    if (timeout || returnedHome) {
      Serial.printf("Walk Ended. Reason: %s\n",
                    timeout ? "Timeout" : "Returned Home");
      currentState = STATE_IDLE;
      currentWalkId = "";
      syncData();
    }
  }
}
