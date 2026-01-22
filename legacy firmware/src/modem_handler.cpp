#include "modem_handler.h"
#include "config.h"

// Select Modem
#define TINY_GSM_MODEM_SIM7600
#include <TinyGSM.h>

// Modem Serial
// Note: legacy firmware/src/config.h defines:
// PIN_MODEM_TX 26, PIN_MODEM_RX 27
// PIN_MODEM_PWR 4, PIN_MODEM_PON 12
HardwareSerial SerialAT(1);

// Debugger
#define TINY_GSM_DEBUG Serial
// #include <StreamDebugger.h>
// StreamDebugger debugger(SerialAT, Serial);

TinyGsm modem(SerialAT);
TinyGsmClientSecure client(modem);

void setupModem() {
  Serial.println("Initializing Modem...");

  // 1. Power On Sequence
  pinMode(PIN_MODEM_PWR, OUTPUT);
  pinMode(PIN_MODEM_PON, OUTPUT);

  // PWRKEY Low to High to Low usually
  // PON (Power On) usually High to turn on LDO
  digitalWrite(PIN_MODEM_PON, HIGH);
  delay(100);

  // PWRKEY Pulse
  digitalWrite(PIN_MODEM_PWR, LOW);
  delay(100);
  digitalWrite(PIN_MODEM_PWR, HIGH);
  delay(1000); // 1s active pulse
  digitalWrite(PIN_MODEM_PWR, LOW);

  Serial.println("Waiting for modem to boot...");
  delay(3000);

  SerialAT.begin(115200, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);

  // 2. Restart & Init
  if (!modem.restart()) {
    Serial.println("Modem Failed to Restart");
    // Try just init
    if (!modem.init()) {
      Serial.println("Modem Init Failed");
      return;
    }
  }

  String modemInfo = modem.getModemInfo();
  Serial.print("Modem Info: ");
  Serial.println(modemInfo);

  // 3. Network Mode & SSL
  // Unlock SIM if needed (Pin) - assuming no pin
  // Use "internet" or "airtelgprs.com" or "jionet" etc.
  // We'll trust auto-detect or "internet" generic

  Serial.println("Waiting for Network...");
  if (!modem.waitForNetwork()) {
    Serial.println("Network Failed");
    return;
  }
  Serial.println("Network Connected");

  if (!modem.gprsConnect("internet")) { // Generic APN
    Serial.println("GPRS Connect Failed");
    return;
  }
  Serial.println("GPRS Connected");

  // 4. Critical SSL Configuration for Railway
  // A7670G/SIM7600 AT Commands for "Ignore Verify"
  // TinyGSM might not expose this directly, so we use direct AT
  // CSSLCFG="sslversion",0,3 (TLS 1.2) is standard, often default.
  // CSSLCFG="ignoretold",0,1 (Ignore time)
  // CSSLCFG="verify",0,0 (No verify)

  modem.sendAT("+CSSLCFG=\"sslversion\",0,3");
  modem.waitResponse();

  modem.sendAT("+CSSLCFG=\"verify\",0,0"); // NO Verification
  modem.waitResponse();

  modem.sendAT("+CSSLCFG=\"ignoretold\",0,1"); // Ignore time check
  modem.waitResponse();

  // Set client to use SSL context 0
  client.setInsecure(); // TinyGSM helper
}

void modemLoop() {
  // Keep connection alive if needed
  modem.maintain();
}

bool modemConnected() { return modem.isNetworkConnected(); }

void postDataRealtime(double lat, double lon, double speed, double activity) {
  if (!modem.isNetworkConnected())
    return;

  Serial.println("Posting Realtime Data via LTE...");

  // Construct CSV line or JSON
  // Format: timestamp,lat,lon,speed,activity
  String data = String(millis()) + "," + String(lat, 6) + "," + String(lon, 6) +
                "," + String(speed, 2) + "," + String(activity, 2);

  // Railway Endpoint
  // We need to parse valid host from API_URL
  // Or just hardcode for test: user said "https issue".
  // Assuming API_URL is https://... defined in config
  // We need to extract domain and path.

  // Quick Hack: Parse it or assume constant
  // char server[] = "your-app.up.railway.app";
  // char path[] = "/api/upload/realtime";

  // For now, let's use the implementation that parses or just string manip
  String url = String(API_URL);
  int protocolEnd = url.indexOf("://");
  int pathStart = url.indexOf("/", protocolEnd + 3);
  String host = url.substring(protocolEnd + 3, pathStart);
  String path = url.substring(pathStart);

  if (client.connect(host.c_str(), 443)) {
    client.print(String("POST ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print("Connection: close\r\n");
    client.print("Content-Type: text/csv\r\n");
    client.print("Content-Length: " + String(data.length()) + "\r\n");
    client.print("\r\n");
    client.print(data);

    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 10000) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r")
          break; // Headers done
      }
    }
    client.stop();
    Serial.println("LTE Upload Done");
  } else {
    Serial.println("LTE Connect Failed");
  }
}
