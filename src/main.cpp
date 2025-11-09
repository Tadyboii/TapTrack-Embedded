#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include "RFID.h"
#include <RtcDS1302.h>
#include "RTC.h"

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

// Network and Firebase credentials
#define WIFI_SSID "SEVE&HIRU"
#define WIFI_PASSWORD "Adventure_112510"

#define Web_API_KEY "AIzaSyBLtOgdEJKVre8tbbd3iEO8fN5PE30ZNm4"
#define DATABASE_URL "https://taptrackapp-38817-default-rtdb.firebaseio.com"
#define USER_EMAIL "thaddeus.rosales@g.msuiit.edu.ph"
#define USER_PASS "123456789"

// Authentication
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

String uidValue = "";
String timeValue = "";

// JSON tools
object_t jsonData, obj1, obj2;
JsonWriter writer;

void processData(AsyncResult &aResult);
void sendToFirebase(String uid, String timestamp);
void initFirebase();

void setup() {
  Serial.begin(9600);               
  while (!Serial);                  
  SPI.begin();                    

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();

  initRFID();                   // Init MFRC522 card
  setupAndSyncRTC();                   // Init RTC module
  initFirebase();                     // Init Firebase

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

  app.loop(); 
  if (cardDetected) { // new read interrupt
    Serial.print(F("Card Detected."));

    // UID DATA
    String uid = readCardUID();

    if (uid.length() > 0) {
      // TIME DATA
      RtcDateTime time = getCurrentTime();
      Serial.println();
      Serial.print(F("Card UID: "));
      Serial.print(uid);
      Serial.print(", Time: ");
      printDateTime(time);
      Serial.println();
      char timestamp[25];
      snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u",
             time.Year(), time.Month(), time.Day(),
             time.Hour(), time.Minute(), time.Second());

      sendToFirebase(uid, String(timestamp));

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

void initFirebase() {
    // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setTimeout(1000);
  ssl_client.setHandshakeTimeout(5);

  // Initialize Firebase
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult())
    return;

  if (aResult.isEvent())
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

  if (aResult.isDebug())
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

  if (aResult.isError())
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

  if (aResult.available())
    Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
}

void sendToFirebase(String uid, String timestamp) {
  while (!app.ready()) {
    Serial.println(F("Firebase not ready, waiting..."));
    app.loop();
    delay(100);
  }

  // Build JSON using JsonWriter
  writer.create(obj1, "uid", uid);
  writer.create(obj2, "timestamp", timestamp);
  writer.join(jsonData, 2, obj1, obj2);

  // Push JSON to Firebase
  Database.push<object_t>(aClient, "/data", jsonData, processData, "Push_Data");

  Serial.println(F("✅ Data appended to Firebase."));
}