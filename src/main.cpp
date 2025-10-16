#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <RtcDS1302.h>

#define RST_PIN 21
#define SS_PIN 5
#define VALID_UID "F3A3401E"
#define IRQ_PIN 34  // Button or RFID interrupt pin

MFRC522 mfrc522(SS_PIN, RST_PIN);
ThreeWire myWire(26, 25, 27); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);

// Task handles
TaskHandle_t TaskRFIDHandle;
TaskHandle_t TaskLoggerHandle;

// Shared data
volatile bool rfidTrigger = false;
String lastUID = "";

// ISR
void IRAM_ATTR onRFIDInterrupt() {
  rfidTrigger = true;
}

// Print helper
void printDateTime(const RtcDateTime& dt) {
  char buf[26];
  snprintf(buf, sizeof(buf), "%02u/%02u/%04u %02u:%02u:%02u",
           dt.Month(), dt.Day(), dt.Year(),
           dt.Hour(), dt.Minute(), dt.Second());
  Serial.print(buf);
}

// RFID Reading Task
void TaskRFID(void *pvParameters) {
  for (;;) {
    if (rfidTrigger) {
      rfidTrigger = false;

      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String uidStr = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
          uidStr += String(mfrc522.uid.uidByte[i], HEX);
        }
        uidStr.toUpperCase();
        lastUID = uidStr;
        // Notify Logger Task
        xTaskNotifyGive(TaskLoggerHandle);

        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay to yield CPU
  }
}

// Logger Task
void TaskLogger(void *pvParameters) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Wait for notification from RFID task

    RtcDateTime now = Rtc.GetDateTime();
    Serial.println("Card Detected!");
    Serial.print("UID: ");
    Serial.println(lastUID);
    Serial.print("Timestamp: ");
    printDateTime(now);
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  // RTC init
  Rtc.Begin();
  if (!Rtc.IsDateTimeValid()) {
    Rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
  }

  // Interrupt pin setup
  pinMode(IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(IRQ_PIN), onRFIDInterrupt, FALLING);

  // Create FreeRTOS tasks
  xTaskCreatePinnedToCore(TaskRFID, "RFID Task", 4096, NULL, 2, &TaskRFIDHandle, 0);
  xTaskCreatePinnedToCore(TaskLogger, "Logger Task", 4096, NULL, 1, &TaskLoggerHandle, 1);
}

void loop() {
  // Nothing here! Everything handled by tasks and interrupts.
}
