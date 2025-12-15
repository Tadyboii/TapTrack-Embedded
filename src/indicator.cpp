#include "indicator.h"

static void setLED(uint8_t pin) {
    // Turn off all LEDs first
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    
    // Turn on the specified LED
    digitalWrite(pin, HIGH);
}

static void beep(uint16_t duration) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
}

static void beepSuccess() {
    beep(SUCCESS_BEEP_DURATION);
}

static void beepError() {
    // First beep
    beep(ERROR_BEEP_DURATION);
    
    // Pause between beeps
    delay(ERROR_BEEP_PAUSE);
    
    // Second beep
    beep(ERROR_BEEP_DURATION);
}

void initIndicator() {
    // Configure LED pins as outputs
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(YELLOW_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    
    // Ensure all outputs are off initially
    clearIndicators();
    
    Serial.println(F("✓ Indicator module initialized"));
}

void indicateSuccessOnline() {
    setLED(GREEN_LED_PIN);
    // beepSuccess();
    
    Serial.println(F("✅ Indicator: Success (Online) - Green LED"));
}

void indicateSuccessOffline() {
    setLED(YELLOW_LED_PIN);
    // beepSuccess();
    
    Serial.println(F("⚠️ Indicator: Success (Offline) - Yellow LED"));
}

void indicateError() {
    setLED(RED_LED_PIN);
    // beepError();
    
    Serial.println(F("❌ Indicator: Error - Red LED"));
}

void clearIndicators() {
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
}

void testIndicators() {
    Serial.println(F("\n🔍 Testing Indicator Module..."));
    
    // Test Green LED
    Serial.println(F("  Testing Green LED..."));
    digitalWrite(GREEN_LED_PIN, HIGH);
    delay(500);
    digitalWrite(GREEN_LED_PIN, LOW);
    delay(200);
    
    // Test Yellow LED
    Serial.println(F("  Testing Yellow LED..."));
    digitalWrite(YELLOW_LED_PIN, HIGH);
    delay(500);
    digitalWrite(YELLOW_LED_PIN, LOW);
    delay(200);
    
    // Test Red LED
    Serial.println(F("  Testing Red LED..."));
    digitalWrite(RED_LED_PIN, HIGH);
    delay(500);
    digitalWrite(RED_LED_PIN, LOW);
    delay(200);
    
    // Test Buzzer
    Serial.println(F("  Testing Buzzer..."));
    beep(300);
    delay(200);
    beep(300);
    
    Serial.println(F("✓ Indicator test complete\n"));
}