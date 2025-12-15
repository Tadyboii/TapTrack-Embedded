/*
 * TapTrack - RFID Module
 * MFRC522 RFID Reader Interface
 */

#ifndef RFID_H
#define RFID_H

#include <Arduino.h>
#include <MFRC522.h>
#include "config.h"

// Global flag used by ISR
extern volatile bool cardDetected;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * Initialize RFID module
 */
void initRFID();

/**
 * Interrupt service routine for card detection
 */
void IRAM_ATTR readCardISR();

/**
 * Activate receiver for card detection
 */
void activateRec();

/**
 * Clear interrupt flags
 */
void clearInt();

/**
 * Enable interrupt for card detection
 */
void enableInterrupt();

/**
 * Read card UID as hex string
 * @return UID string (uppercase hex) or empty string on failure
 */
String readCardUID();

/**
 * Debug: dump byte array to serial
 */
void dump_byte_array(byte *buffer, byte bufferSize);

/**
 * Clear the UID buffer (FIXED VERSION)
 */
void clearUIDBuffer();

/**
 * Periodic RFID module health check and reset
 */
void checkAndResetMFRC522();

/**
 * Check if RFID module is responsive
 * @return true if module responds correctly
 */
bool isRFIDHealthy();

#endif // RFID_H
