#include "sd_logger.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>

SPIClass sdSPI(HSPI);

void setupSD() {
  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, sdSPI)) {
    Serial.println("SD Card Mount Failed");
    return;
  }

  if (!SD.exists("/walks"))
    SD.mkdir("/walks");
  Serial.println("SD Card Initialized");
}

String startNewLogSession(unsigned long timestamp) {
  // timestamp is either GPS epoch or millis fallback
  String filename = "/walks/walk_" + String(timestamp) + ".csv";

  File f = SD.open(filename, FILE_WRITE);
  if (f) {
    f.println("timestamp_ms,lat,lon,speed_kmph,activity,fix");
    f.close();
  }
  return filename;
}

void logData(String filename, double lat, double lon, double speed,
             double activity, bool fix) {
  if (filename == "")
    return;

  // Open in append mode
  File f = SD.open(filename, FILE_APPEND);
  if (f) {
    f.printf("%lu,%.6f,%.6f,%.2f,%.2f,%d\n", millis(), lat, lon, speed,
             activity, fix ? 1 : 0);
    f.close();
  }
}
