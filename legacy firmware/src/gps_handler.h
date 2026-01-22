#ifndef GPS_HANDLER_H
#define GPS_HANDLER_H

#include <Arduino.h>

void setupGPS();
void processGPS();

double getGPSLat();
double getGPSLon();
double getGPSSpeed(); // Speed in km/h or m/s
bool getGPSFix();
unsigned long getGPSEpochTime(); // Returns epoch time from GPS

#endif
