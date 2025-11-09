#ifndef RFID_H
#define RFID_H

#include <Arduino.h>
#include <MFRC522.h>

// Pin definitions
#define RFID_RST_PIN   21          // Reset pin
#define RFID_SS_PIN    5           // Slave select pin
#define RFID_IRQ_PIN   4           // Interrupt pin

// Global flag used by ISR
extern volatile bool cardDetected;

// Function declarations
void IRAM_ATTR readCardISR();
void activateRec();
void clearInt();
void enableInterrupt();
String readCardUID();
void dump_byte_array(byte *buffer, byte bufferSize);
void clearUIDBuffer();
void checkAndResetMFRC522();
void initRFID(); 

#endif
