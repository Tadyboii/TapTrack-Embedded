#include "RFID.h"

volatile bool cardDetected = false;
byte regVal = 0x7F;
unsigned long lastReset = 0; 

// MFRC522 instance
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

void initRFID(){
    mfrc522.PCD_Init();
}

/**
 * MFRC522 interrupt serving routine
 */
void IRAM_ATTR readCardISR() {
    cardDetected = true;
}

/*
 * The function sending to the MFRC522 the needed commands to activate the reception
 */
void activateRec() {
    mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg, mfrc522.PICC_CMD_REQA);
    mfrc522.PCD_WriteRegister(mfrc522.CommandReg, mfrc522.PCD_Transceive);
    mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);
}

/*
 * The function to clear the pending interrupt bits after interrupt serving routine
 */
void clearInt() {
    mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);
}

/*
 * Allow the ... irq to be propagated to the IRQ pin
 * For test purposes propagate the IdleIrq and loAlert
 */
void enableInterrupt() {
    regVal = 0xA0; //rx irq
    mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, regVal);
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

/**
 * Function to read RFID card data and return UID as String
 */
String readCardUID() {
    mfrc522.PICC_ReadCardSerial(); // read the tag data
    String uidStr = "";

    //   Show some details of the PICC (that is: the tag/card)
    //   Serial.print(F("Card UID:"));
    //   dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
    //   Serial.println();

    // Build UID string
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
        uidStr += String(mfrc522.uid.uidByte[i], HEX);
    }
    uidStr.toUpperCase();

    mfrc522.PICC_HaltA();

    clearUIDBuffer();

    return uidStr;
}

/**
 * Function to clear the UID buffer
 */
void clearUIDBuffer() {
    // Method 1: Direct clearing
    mfrc522.uid.size = 0;
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        mfrc522.uid.uidByte[i] = 0;
    }

    // Method 2: Reset the FIFO buffer (optional)
    //   mfrc522.PCD_WriteRegister(mfrc522.FIFOLevelReg, 0x80); // Set FlushBuffer bit
}

void checkAndResetMFRC522() {
// Reset module every 5 seconds
    if (millis() - lastReset > 5000) {
        // Serial.println(F("Resetting MFRC522..."));
        mfrc522.PCD_Init(); // Simple re-initialization
        enableInterrupt(); // Re-enable interrupts
        lastReset = millis();
    }
}