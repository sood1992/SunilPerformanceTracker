#ifndef MODEM_HANDLER_H
#define MODEM_HANDLER_H

#include <Arduino.h>

void setupModem();
void modemLoop();
bool modemConnected();
void postDataRealtime(double lat, double lon, double speed, double activity);

#endif
