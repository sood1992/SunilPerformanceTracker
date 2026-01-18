/**
 * ABSOLUTE MINIMAL TEST
 * Just blink LED and print to serial
 */

#include <Arduino.h>

// Built-in LED (if available) or use any GPIO
#define LED_PIN 2

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== MINIMAL BOOT TEST ===");
    Serial.println("If you see this, basic boot works!");
    Serial.println();

    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    Serial.println("Loop running...");

    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
    delay(500);
}
