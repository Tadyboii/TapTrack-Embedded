/*
 * TapTrack - RFID Module Implementation
 * MFRC522 RFID Reader with interrupt-driven card detection
 */

#include "RFID.h"

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

volatile bool cardDetected = false;
static byte regVal = 0x7F;
static unsigned long lastReset = 0;

// MFRC522 instance
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// =============================================================================
// INITIALIZATION
// =============================================================================

void initRFID() {
    mfrc522.PCD_Init();
    
    // Verify module is responding
    byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    if (version == 0x00 || version == 0xFF) {
        Serial.println(F("⚠️ WARNING: MFRC522 not detected!"));
    } else {
        Serial.print(F("✓ MFRC522 firmware version: 0x"));
        Serial.println(version, HEX);
    }
    
    lastReset = millis();
}

// =============================================================================
// INTERRUPT HANDLING
// =============================================================================

void IRAM_ATTR readCardISR() {
    cardDetected = true;
}

void activateRec() {
    mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg, mfrc522.PICC_CMD_REQA);
    mfrc522.PCD_WriteRegister(mfrc522.CommandReg, mfrc522.PCD_Transceive);
    mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);
}

void clearInt() {
    mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);
}

void enableInterrupt() {
    regVal = 0xA0;  // RX IRQ
    mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, regVal);
}

// =============================================================================
// CARD READING
// =============================================================================

String readCardUID() {
    // Attempt to read the card serial
    if (!mfrc522.PICC_ReadCardSerial()) {
        return "";
    }
    
    String uidStr = "";
    
    // Build UID string from bytes
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) {
            uidStr += "0";
        }
        uidStr += String(mfrc522.uid.uidByte[i], HEX);
    }
    uidStr.toUpperCase();
    
    // Halt the card
    mfrc522.PICC_HaltA();
    
    // Clear buffer for next read
    clearUIDBuffer();
    
    return uidStr;
}

void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

/**
 * Clear the UID buffer - FIXED VERSION
 * Bug fix: Must clear bytes BEFORE setting size to 0
 */
void clearUIDBuffer() {
    // Clear all possible UID bytes first (max 10 bytes for double-size UID)
    for (byte i = 0; i < 10; i++) {
        mfrc522.uid.uidByte[i] = 0;
    }
    
    // Now set size to 0
    mfrc522.uid.size = 0;
    mfrc522.uid.sak = 0;
    
    // Optional: Flush the FIFO buffer
    mfrc522.PCD_WriteRegister(mfrc522.FIFOLevelReg, 0x80);
}

// =============================================================================
// HEALTH CHECK & RESET
// =============================================================================

void checkAndResetMFRC522() {
    // Reset module periodically to prevent lockups
    if (millis() - lastReset > RFID_RESET_INTERVAL_MS) {
        mfrc522.PCD_Init();
        enableInterrupt();
        lastReset = millis();
        
        #if DEBUG_RFID
        Serial.println(F("🔄 MFRC522 reset"));
        #endif
    }
}

bool isRFIDHealthy() {
    byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    
    // Known valid versions: 0x91 (v1.0), 0x92 (v2.0), 0x88 (clone)
    if (version == 0x00 || version == 0xFF) {
        return false;
    }
    
    return true;
}
