#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SPIFFS.h>
#include "RFID.h"
#include <RtcDS1302.h>
#include "RTC.h"
#include "Firebase.h"
#include "UserDatabase.h"
#include "AttendanceQueue.h"
#include "WifiManager.h"

// Attendance time thresholds (24-hour format)
#define ON_TIME_HOUR 9    // Before 9:00 AM is on time
#define LATE_HOUR 9       // After 9:00 AM is late

// Duplicate tap prevention (milliseconds)
#define TAP_COOLDOWN 30000  // 30 seconds between taps

// Sync interval (milliseconds)
#define SYNC_INTERVAL 30000  // Try to sync every 30 seconds
#define WIFI_CHECK_INTERVAL 60000  // Check WiFi every 60 seconds

// User database instance (persisted to SPIFFS)
UserDatabase userDB;

// Attendance queue for offline storage
AttendanceQueue attendanceQueue;

// Last tap tracking for duplicate prevention
struct LastTap {
    String uid;
    unsigned long timestamp;
};
LastTap lastTap = {"", 0};

// System state
bool isOnline = false;
bool firebaseInitialized = false;
unsigned long lastSyncAttempt = 0;
unsigned long lastWifiCheck = 0;
bool syncInProgress = false;

/**
 * Determine attendance status based on time
 */
String getAttendanceStatus(const RtcDateTime& time) {
    int hour = time.Hour();
    
    if (hour < ON_TIME_HOUR) {
        return "present";  // On time
    } else if (hour >= LATE_HOUR) {
        return "late";
    }
    
    return "present";
}

/**
 * Check if this is a duplicate tap (same card within cooldown period)
 */
bool isDuplicateTap(String uid) {
    unsigned long now = millis();
    
    // Check if same card and within cooldown period
    if (lastTap.uid == uid && (now - lastTap.timestamp) < TAP_COOLDOWN) {
        Serial.print(F("⚠️ Duplicate tap (cooldown: "));
        Serial.print((TAP_COOLDOWN - (now - lastTap.timestamp)) / 1000);
        Serial.println(F(" sec)"));
        return true;
    }
    
    // Update last tap
    lastTap.uid = uid;
    lastTap.timestamp = now;
    return false;
}

/**
 * Check WiFi connectivity and attempt reconnection
 */
bool checkAndReconnectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!isOnline) {
            Serial.println(F("🌐 WiFi reconnected!"));
            isOnline = true;
            
            // Reinitialize Firebase if needed
            if (!firebaseInitialized) {
                Serial.println(F("🔥 Reinitializing Firebase..."));
                initFirebase();
                firebaseInitialized = true;
            }
        }
        return true;
    } else {
        if (isOnline) {
            Serial.println(F("⚠️ WiFi disconnected - switching to offline mode"));
            isOnline = false;
        }
        
        // Try to reconnect
        Serial.println(F("🔄 Attempting WiFi reconnection..."));
        WiFi.reconnect();
        delay(5000);  // Wait for connection
        
        return (WiFi.status() == WL_CONNECTED);
    }
}

/**
 * Sync queued attendance records to Firebase
 */
void syncQueuedAttendance() {
    if (!isOnline || attendanceQueue.isEmpty() || syncInProgress) {
        return;
    }
    
    syncInProgress = true;
    Serial.println(F("🔄 Syncing queued attendance..."));
    
    int syncedCount = 0;
    int failedCount = 0;
    
    while (!attendanceQueue.isEmpty() && isOnline) {
        AttendanceRecord* record = attendanceQueue.peek();
        if (record == nullptr) break;
        
        // Wait for Firebase to be ready
        if (!app.ready()) {
            app.loop();
            delay(100);
            if (!app.ready()) {
                Serial.println(F("⚠️ Firebase not ready, will retry later"));
                break;
            }
        }
        
        // Send to Firebase
        Serial.print(F("  ➤ Syncing: "));
        Serial.println(record->name.length() > 0 ? record->name : record->uid);
        
        sendToFirebase(record->uid, record->name, record->timestamp, 
                       record->attendanceStatus, record->registrationStatus);
        
        // Remove from queue after successful send
        attendanceQueue.dequeue();
        syncedCount++;
        
        // Small delay between sends
        delay(200);
        app.loop();
    }
    
    syncInProgress = false;
    
    if (syncedCount > 0) {
        Serial.print(F("✅ Synced "));
        Serial.print(syncedCount);
        Serial.println(F(" attendance records"));
    }
    
    if (!attendanceQueue.isEmpty()) {
        Serial.print(F("📝 "));
        Serial.print(attendanceQueue.size());
        Serial.println(F(" records still pending"));
    }
}

void setup() {
  Serial.begin(115200);               
  while (!Serial);                  
  SPI.begin();

  Serial.println(F("\n========================================"));
  Serial.println(F("   TapTrack Attendance System"));
  Serial.println(F("        [Hybrid Offline Mode]"));
  Serial.println(F("========================================\n"));

  // Initialize SPIFFS once for all components
  Serial.println(F("💾 Initializing SPIFFS..."));
  if (!SPIFFS.begin(true)) {
    Serial.println(F("⚠️ SPIFFS mount failed, formatting..."));
    SPIFFS.format();
    if (!SPIFFS.begin(true)) {
      Serial.println(F("❌ SPIFFS format failed!"));
    } else {
      Serial.println(F("✓ SPIFFS formatted and mounted"));
    }
  } else {
    Serial.println(F("✓ SPIFFS mounted"));
  }

  // Initialize User Database (uses SPIFFS)
  Serial.println(F("📂 Initializing User Database..."));
  if (userDB.init()) {
    Serial.println(F("✓ User database ready"));
  } else {
    Serial.println(F("⚠️ User database init failed (will use RAM only)"));
  }

  // Initialize Attendance Queue (uses SPIFFS)
  Serial.println(F("📝 Initializing Attendance Queue..."));
  if (attendanceQueue.init()) {
    Serial.println(F("✓ Attendance queue ready"));
  } else {
    Serial.println(F("⚠️ Attendance queue init failed"));
  }

  // Initialize WiFi Manager
  Serial.println(F("🌐 Initializing WiFi..."));
  bool wifiConnected = initWiFiManager();
  Serial.println();
  
  isOnline = wifiConnected;
  
  if (isOnline) {
    Serial.println(F("✅ WiFi Connected"));
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("⚠️ WiFi Not Connected - Running in OFFLINE mode"));
    Serial.println(F("   Attendance will be queued locally"));
  }
  
  Serial.println();

  // Initialize RFID
  Serial.println(F("📡 Initializing RFID..."));
  initRFID();
  Serial.println(F("✓ RFID ready"));

  // Initialize RTC
  Serial.println(F("🕐 Initializing RTC..."));
  setupAndSyncRTC();
  Serial.println(F("✓ RTC ready"));

  // Initialize Firebase if online
  if (isOnline) {
    Serial.println(F("🔥 Initializing Firebase..."));
    initFirebase();
    while (!app.ready()) { app.loop(); delay(10); }
    fetchAllUsersFromFirebase();
    streamUsers();  // Start streaming /users for realtime updates
    firebaseInitialized = true;
    Serial.println(F("✓ Firebase initialized, syncing users..."));
    
    // Sync any queued attendance from previous offline session
    if (!attendanceQueue.isEmpty()) {
      Serial.print(F("📤 Found "));
      Serial.print(attendanceQueue.size());
      Serial.println(F(" queued records, syncing..."));
      delay(2000);  // Wait for Firebase to stabilize
      syncQueuedAttendance();
    }
  } else {
    Serial.println(F("⏭️ Skipping Firebase (offline)"));
    firebaseInitialized = false;
    Serial.println(F("📂 Using cached user database"));
  }

  // Setup the IRQ pin (CRITICAL - same as original!)
  pinMode(RFID_IRQ_PIN, INPUT_PULLUP);
  enableInterrupt();
  cardDetected = false;
  
  // Activate the interrupt (CRITICAL - same as original!)
  attachInterrupt(digitalPinToInterrupt(RFID_IRQ_PIN), readCardISR, FALLING);
  
  Serial.println();
  
  // Show registered users
  userDB.printAllUsers();
  
  // Show queue status
  if (!attendanceQueue.isEmpty()) {
    attendanceQueue.printQueue();
  }
  
  Serial.println(F("\n========================================"));
  Serial.println(F("         System Ready!"));
  Serial.println(F("========================================"));
  Serial.print(F("Mode: "));
  Serial.println(isOnline ? "ONLINE ✅" : "OFFLINE 📴");
  if (!isOnline) {
    Serial.println(F("  → Attendance stored locally"));
    Serial.println(F("  → Will sync when online"));
  }
  Serial.println(F("\n🎫 Tap RFID card to record attendance"));
  Serial.println(F("========================================\n"));
  
  Serial.println(F("End setup"));
  delay(1000);
}

void loop() {
  // Reset RFID module every 5 seconds (CRITICAL - same as original!)
  checkAndResetMFRC522();

  unsigned long now = millis();

  // Periodic WiFi check and reconnection
  if (!isOnline && (now - lastWifiCheck > WIFI_CHECK_INTERVAL)) {
    lastWifiCheck = now;
    checkAndReconnectWiFi();
  }

  // Process Firebase events if online (needed for async callbacks)
  if (isOnline) {
    app.loop();
    
    // Periodic sync of queued attendance
    if (!attendanceQueue.isEmpty() && (now - lastSyncAttempt > SYNC_INTERVAL)) {
      lastSyncAttempt = now;
      syncQueuedAttendance();
    }
  }
  
  if (cardDetected) { // New read interrupt
    unsigned long start = millis();
    while (millis() - start < 50); // Wait 50ms
    Serial.print(F("Card Detected."));

    // Read UID (CRITICAL - same as original!)
    String uid = readCardUID();
    Serial.print(F("Raw UID string: '"));
    Serial.print(uid);
    Serial.print(F("'"));
    
    if (uid.length() > 0) {
      // Get timestamp
      RtcDateTime time = getCurrentTime();
      Serial.println();
      Serial.print(F("Card UID: "));
      Serial.print(uid);
      Serial.print(F(", Time: "));
      printDateTime(time);
      Serial.println();
      
      // Check for duplicate tap
      if (isDuplicateTap(uid)) {
        clearInt();
        cardDetected = false;
        activateRec();
        delay(100);
        return;  // Skip processing
      }
      
      // Format timestamp in ISO 8601 format
      char timestamp[30];
      snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.000Z",
               time.Year(), time.Month(), time.Day(),
               time.Hour(), time.Minute(), time.Second());

      // Look up user information
      UserInfo userInfo = userDB.getUserInfo(uid);
      String name = userInfo.name;
      String registrationStatus = userInfo.isRegistered ? "registered" : "unregistered";
      
      // Determine attendance status based on time
      String attendanceStatus = getAttendanceStatus(time);
      
      // Print user info
      if (userInfo.isRegistered) {
        Serial.print(F("User: "));
        Serial.print(name);
        Serial.println(F(" (Registered)"));
      } else {
        Serial.println(F("User: Unknown (Unregistered)"));
      }
      
      // Handle attendance based on online/offline status
      if (isOnline) {
        if (userInfo.isRegistered) {
          // Registered user - send attendance directly
          sendToFirebase(uid, name, String(timestamp), attendanceStatus, registrationStatus);
        } else {
          // Unknown/unregistered user - send to pendingUsers only (no attendance)
          Serial.println(F("➤ Sending to pending users (no attendance for unregistered)"));
          fetchUserFromFirebase(uid);  // Try fetching in background
          sendPendingUser(uid, String(timestamp));
          // DO NOT send to attendance - wait for approval and re-scan
        }
      } else {
        // OFFLINE MODE - Queue attendance locally
        if (userInfo.isRegistered) {
          Serial.println(F("📴 Offline - Queuing attendance locally"));
          attendanceQueue.enqueue(uid, name, String(timestamp), attendanceStatus, registrationStatus);
        } else {
          Serial.println(F("⚠️ Offline + Unregistered - Cannot process"));
          Serial.println(F("   (User must be registered when online first)"));
        }
      }

    } else {
      Serial.print(F(" Reading UID... Place card again."));
      Serial.println();
    }

    clearInt();
    cardDetected = false;
  }

  activateRec();
  delay(100);
}