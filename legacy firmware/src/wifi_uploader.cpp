#include "wifi_uploader.h"
#include "config.h"
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

extern String currentWalkId;

void syncData() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 10) {
      delay(500);
      Serial.print(".");
      retries++;
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected. Syncing walks...");

    // Secure Client for Railway HTTPS
    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate validation for stability

    File dir = SD.open("/walks");
    if (!dir || !dir.isDirectory()) {
      Serial.println("Failed to open /walks directory");
      return;
    }

    File file = dir.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String fileName = String(file.name());
        String fullPath = "/walks/" + fileName;

        if (currentWalkId != "" && fullPath == currentWalkId) {
          // Skip active
        } else if (fileName.endsWith(".csv")) {
          Serial.print("Uploading (SSL): ");
          Serial.println(fileName);

          HTTPClient http;
          if (http.begin(client, API_URL)) { // Use Secure Client
            http.addHeader("Content-Type", "text/csv");
            http.addHeader("X-Filename", fileName);

            int httpResponseCode = http.sendRequest("POST", &file, file.size());

            if (httpResponseCode == 200) {
              Serial.println("Upload Success!");
              file.close();
              SD.remove(fullPath);
            } else {
              Serial.printf("Upload Failed: %d\n", httpResponseCode);
              String payload = http.getString();
              Serial.println(payload);
              file.close();
            }
            http.end();
          } else {
            Serial.println("Unable to connect via HTTPS");
            file.close();
          }
        }
      }
      file = dir.openNextFile();
    }
    dir.close();
  } else {
    Serial.println("WiFi Connection Failed");
  }
}
