/*
 * Indicator Module - LED and Buzzer Feedback
 * For ESP32 Attendance System
 * 
 * Pin Configuration:
 * - Pin 13: Green LED (Success/Registered - Online)
 * - Pin 12: Yellow LED (Success/Registered - Offline)
 * - Pin 14: Red LED (Failed/Error)
 * - Pin 15: Buzzer
 */

#ifndef INDICATOR_H
#define INDICATOR_H

#include <Arduino.h>

// Pin definitions
#define GREEN_LED_PIN   13
#define YELLOW_LED_PIN  12
#define RED_LED_PIN     14
#define BUZZER_PIN      15

// Timing definitions (milliseconds)
#define SUCCESS_BEEP_DURATION   1000  // 1 second beep for success
#define ERROR_BEEP_DURATION     200   // 200ms per beep for error
#define ERROR_BEEP_PAUSE        100   // 100ms pause between error beeps


void initIndicator();
void indicateSuccessOnline();
void indicateSuccessOffline();
void indicateError();
void clearIndicators();
void testIndicators();

#endif // INDICATOR_H