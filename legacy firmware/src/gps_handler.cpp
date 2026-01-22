#include "gps_handler.h"
#include "config.h"
#include <TinyGPSPlus.h>

TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // Use Serial2

void setupGPS() {
  gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);
  Serial.println("GPS Serial Initialized");
}

void processGPS() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
}

double getGPSLat() {
  if (gps.location.isValid())
    return gps.location.lat();
  return 0.0;
}

double getGPSLon() {
  if (gps.location.isValid())
    return gps.location.lng();
  return 0.0;
}

double getGPSSpeed() {
  if (gps.speed.isValid())
    return gps.speed.kmph();
  return 0.0;
}

bool getGPSFix() { return gps.location.isValid(); }

unsigned long getGPSEpochTime() {
  if (gps.date.isValid() && gps.time.isValid()) {
    if (gps.date.year() < 2024)
      return 0; // Filter invalid dates

    struct tm t = {0};
    t.tm_year = gps.date.year() - 1900;
    t.tm_mon = gps.date.month() - 1;
    t.tm_mday = gps.date.day();
    t.tm_hour = gps.time.hour();
    t.tm_min = gps.time.minute();
    t.tm_sec = gps.time.second();
    return mktime(&t);
  }
  return 0;
}
