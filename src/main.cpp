#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include "RFID.h"
#include <RtcDS1302.h>
#include "RTC.h"

void setup() {
  Serial.begin(9600);               // Initialize serial communications with the PC
  while (!Serial);                  // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
  SPI.begin();                      // Init SPI bus

  initRFID();                   // Init MFRC522 card
  setupRTC();                   // Init RTC module

  /* setup the IRQ pin*/
  pinMode(RFID_IRQ_PIN, INPUT_PULLUP);

  enableInterrupt(); // Enable IRQ propagation

  cardDetected = false; // interrupt flagq

  /* Activate the interrupt */
  attachInterrupt(digitalPinToInterrupt(RFID_IRQ_PIN), readCardISR, FALLING);

  Serial.println(F("End setup"));

  delay(1000);
}

void loop() {
  // Reset rfid module every 5 seconds
  checkAndResetMFRC522();

  if (cardDetected) { // new read interrupt
    Serial.print(F("Card Detected."));

    // UID AND TIME DATA
    String uid = readCardUID();
    RtcDateTime time = getCurrentTime();

    if (uid.length() > 0) {
      Serial.println();
      Serial.print(F("Card UID: "));
      Serial.print(uid);
      Serial.print(", Time: ");
      printDateTime(time);
      Serial.println();
    } else {
      Serial.print(F(" Reading UID... Place card again."));
      Serial.println();
    }

    clearInt();
    cardDetected = false;
  }

  // The receiving block needs regular retriggering (tell the tag it should transmit??)
  // (mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg,mfrc522.PICC_CMD_REQA);)
  activateRec();
  delay(100);
}