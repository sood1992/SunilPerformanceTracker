#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>

void setupSD();
String startNewLogSession(unsigned long timestamp);
void logData(String filename, double lat, double lon, double speed,
             double activity, bool fix);

#endif
