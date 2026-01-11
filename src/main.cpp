/*
 * TapTrack - ESP32 RFID Attendance System
 * Main Application Entry Point
 * 
 * Features:
 * - RFID card reading with interrupt
 * - Firebase real-time sync
 * - Offline mode with local queue
 * - Online/offline mode toggle
 * - User database with Firebase streaming
 * - LED and buzzer feedback
 */

#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h>

#include "config.h"
#include "secrets.h"
#include "RFID.h"
#include "Firebase.h"
#include "UserDatabase.h"
#include "AttendanceQueue.h"
#include "WifiManager.h"
#include "DS1302_RTC.h"
#include "indicator.h"
#include "gpio.h"

// =============================================================================
// GLOBAL INSTANCES
// =============================================================================

UserDatabase userDB;
AttendanceQueue attendanceQueue;

// =============================================================================
// SYSTEM STATE
// =============================================================================

static SystemMode currentMode = DEFAULT_SYSTEM_MODE;
static bool isOnline = false;
bool firebaseInitialized = false;  // Non-static - accessed by Firebase.cpp
static bool streamActive = false;

// Timing
static unsigned long lastSyncAttempt = 0;
static unsigned long lastWifiCheck = 0;
static unsigned long lastIndicatorUpdate = 0;
static unsigned long lastButtonCheck = 0;

// Duplicate tap prevention
static String lastTapUID = "";
static unsigned long lastTapTime = 0;

// Sync tracking
static String pendingSyncId = "";
static unsigned long syncStartTime = 0;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void toggleMode();

// =============================================================================
// BUTTON HANDLING
// =============================================================================

static bool modeButtonPressed = false;
static unsigned long modeButtonPressTime = 0;

void checkModeButton() {
    static bool lastButtonState = HIGH;
    bool currentState = gpio_read(MODE_BUTTON_PIN);
    
    // Debounce
    if (currentState != lastButtonState) {
        delay(BUTTON_DEBOUNCE_MS);
        currentState = gpio_read(MODE_BUTTON_PIN);
    }
    
    if (currentState == LOW && lastButtonState == HIGH) {
        // Button just pressed
        modeButtonPressTime = millis();
        modeButtonPressed = true;
    }
    
    if (currentState == HIGH && lastButtonState == LOW && modeButtonPressed) {
        // Button released
        unsigned long pressDuration = millis() - modeButtonPressTime;
        modeButtonPressed = false;
        
        if (pressDuration > 3000) {
            // Long press: Reset WiFi credentials
            Serial.println(F("\n🔄 Long press detected - Clearing WiFi credentials"));
            clearWiFiCredentials();
            beepLong();
            delay(1000);
            ESP.restart();
        } else if (pressDuration > 100) {
            // Short press: Toggle mode
            toggleMode();
        }
    }
    
    lastButtonState = currentState;
}

void toggleMode() {
    switch (currentMode) {
        case MODE_AUTO:
            currentMode = MODE_FORCE_ONLINE;
            Serial.println(F("📶 Mode: FORCE ONLINE"));
            break;
        case MODE_FORCE_ONLINE:
            currentMode = MODE_FORCE_OFFLINE;
            Serial.println(F("📴 Mode: FORCE OFFLINE"));
            break;
        case MODE_FORCE_OFFLINE:
            currentMode = MODE_AUTO;
            Serial.println(F("🔄 Mode: AUTO"));
            break;
    }
    
    saveSystemMode(currentMode);
    indicateMode(currentMode);
    beepSuccess();
    
    // React to mode change
    if (currentMode == MODE_FORCE_OFFLINE) {
        isOnline = false;
    } else if (currentMode == MODE_FORCE_ONLINE && isWiFiConnected()) {
        isOnline = true;
        if (!firebaseInitialized) {
            initFirebase();
            firebaseInitialized = true;
        }
    }
}

// =============================================================================
// TIME & ATTENDANCE HELPERS
// =============================================================================

bool isRTCValid(const DateTime& time) {
    return (time.year >= RTC_MIN_YEAR && 
            time.year <= RTC_MAX_YEAR &&
            time.month >= 1 && time.month <= 12 &&
            time.day >= 1 && time.day <= 31 &&
            time.hour <= 23 &&
            time.minute <= 59 &&
            time.second <= 59);
}

String getAttendanceStatus(const DateTime& time) {
    if (time.hour < ON_TIME_HOUR) {
        return "present";
    } else if (time.hour >= LATE_HOUR) {
        return "late";
    }
}

String formatTimestamp(const DateTime& time) {
    char buf[30];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.000Z",
             time.year, time.month, time.day,
             time.hour, time.minute, time.second);
    return String(buf);
}

bool isDuplicateTap(String uid) {
    unsigned long now = millis();
    
    if (lastTapUID == uid && (now - lastTapTime) < TAP_COOLDOWN_MS) {
        Serial.printf(" Duplicate tap (wait %lu sec)\n", 
                     (TAP_COOLDOWN_MS - (now - lastTapTime)) / 1000);
        return true;
    }
    
    lastTapUID = uid;
    lastTapTime = now;
    return false;
}

// =============================================================================
// CONNECTIVITY
// =============================================================================

bool checkAndReconnectWiFi() {
    if (currentMode == MODE_FORCE_OFFLINE) {
        isOnline = false;
        return false;
    }
    
    if (isWiFiConnected()) {
        if (!isOnline) {
            Serial.println(F("🌐 WiFi reconnected!"));
            isOnline = true;
            
            if (!firebaseInitialized && currentMode != MODE_FORCE_OFFLINE) {
                Serial.println(F("🔥 Reinitializing Firebase..."));
                initFirebase();
                firebaseInitialized = true;
                
                // Restart user stream
                if (!isUserStreamActive()) {
                    streamUsers();
                }
            }
        }
        return true;
    } else {
        if (isOnline) {
            Serial.println(F("⚠️ WiFi disconnected"));
            isOnline = false;
        }
        
        if (currentMode == MODE_FORCE_ONLINE) {
            Serial.println(F("📶 Attempting reconnection..."));
            indicateConnecting(true);
            bool connected = reconnectWiFi();
            indicateConnecting(false);
            isOnline = connected;
            return connected;
        }
        
        return false;
    }
}

// =============================================================================
// SYNC QUEUE
// =============================================================================

void syncQueuedAttendance() {
    if (!isOnline || attendanceQueue.isEmpty()) {
        return;
    }
    
    if (currentMode == MODE_FORCE_OFFLINE) {
        return;
    }
    
    // Check if previous sync completed
    if (pendingSyncId.length() > 0) {
        if (isSyncConfirmed(pendingSyncId)) {
            // Previous sync confirmed - remove from queue
            attendanceQueue.dequeueBySyncId(pendingSyncId);
            pendingSyncId = "";
        } else if (millis() - syncStartTime > 10000) {
            // Timeout - retry later
            Serial.println(F("⚠️ Sync timeout, will retry"));
            attendanceQueue.moveToBack();
            pendingSyncId = "";
        } else {
            // Still waiting for confirmation
            return;
        }
    }
    
    // Process next record
    AttendanceRecord* record = attendanceQueue.peek();
    if (!record) return;
    
    // Check retry limit
    if (record->retryCount > 5) {
        Serial.println(F("❌ Max retries reached, moving to back"));
        attendanceQueue.moveToBack();
        return;
    }
    
    if (!isFirebaseReady()) {
        app.loop();
        return;
    }
    
    indicateSyncing(true);
    
    Serial.printf("📤 Syncing: %s\n", 
                 record->name.length() > 0 ? record->name.c_str() : record->uid.c_str());
    
    pendingSyncId = sendToFirebase(
        record->uid,
        record->name,
        record->timestamp,
        record->attendanceStatus,
        record->registrationStatus
    );
    
    if (pendingSyncId.length() > 0) {
        attendanceQueue.setSyncId(pendingSyncId);
        syncStartTime = millis();
    } else {
        indicateSyncing(false);
    }
}

// =============================================================================
// CARD PROCESSING
// =============================================================================

void processCard(String uid) {
    // Get current time
    DateTime time = getCurrentTime();
    
    // Validate RTC
    if (!isRTCValid(time)) {
        Serial.println(F("❌ RTC time invalid!"));
        indicateErrorRTC();
        return;
    }
    
    // Check duplicate tap
    if (isDuplicateTap(uid)) {
        return;
    }
    
    // Format timestamp
    String timestamp = formatTimestamp(time);
    
    // Lookup user
    UserInfo userInfo = userDB.getUserInfo(uid);
    String name = userInfo.name;
    String registrationStatus = userInfo.isRegistered ? "registered" : "unregistered";
    String attendanceStatus = getAttendanceStatus(time);
    
    // Print info
    Serial.println(F("\n========================================"));
    Serial.printf("Card UID: %s\n", uid.c_str());
    Serial.printf("Time: %02d/%02d/%04d %02d:%02d:%02d\n",
                 time.month, time.day, time.year,
                 time.hour, time.minute, time.second);
    
    if (userInfo.isRegistered) {
        Serial.printf("User: %s (Registered)\n", name.c_str());
        userDB.recordTap(uid);
    } else {
        Serial.println(F("User: Unknown (Unregistered)"));
    }
    Serial.println(F("========================================\n"));
    
    // Process based on mode and connectivity
    if (isOnline && currentMode != MODE_FORCE_OFFLINE) {
        if (userInfo.isRegistered) {
            // Send attendance directly
            String syncId = sendToFirebase(uid, name, timestamp, 
                                          attendanceStatus, registrationStatus);
            if (syncId.length() > 0) {
                indicateSuccessOnline();
            } else {
                // Firebase send failed - queue locally
                attendanceQueue.enqueue(uid, name, timestamp, 
                                       attendanceStatus, registrationStatus);
                indicateSuccessQueued();
            }
        } else {
            // Unregistered - send to pending users
            Serial.println(F("➤ Sending to pending users"));
            sendPendingUser(uid, timestamp);
            fetchUserFromFirebase(uid);  // Check if recently registered
            indicateSuccessOnline();
        }
    } else {
        // OFFLINE MODE
        if (userInfo.isRegistered) {
            if (attendanceQueue.isFull()) {
                Serial.println(F("❌ Queue full! Cannot record attendance."));
                indicateErrorQueueFull();
            } else {
                Serial.println(F("📴 Offline - Queuing locally"));
                attendanceQueue.enqueue(uid, name, timestamp, 
                                       attendanceStatus, registrationStatus);
                indicateSuccessOffline();
            }
        } else {
            Serial.println(F("⚠️ Offline + Unregistered - Cannot process"));
            indicateErrorUnregistered();
        }
    }
}

// =============================================================================
// SERIAL COMMANDS
// =============================================================================

void processSerialCommand() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    Serial.printf("\n> %s\n", cmd.c_str());
    
    if (cmd == "status") {
        Serial.println(F("\n=== System Status ==="));
        Serial.printf("Mode: %s\n", 
                     currentMode == MODE_AUTO ? "AUTO" :
                     currentMode == MODE_FORCE_ONLINE ? "FORCE ONLINE" : "FORCE OFFLINE");
        Serial.printf("Online: %s\n", isOnline ? "Yes" : "No");
        Serial.printf("WiFi: %s\n", isWiFiConnected() ? "Connected" : "Disconnected");
        Serial.printf("Firebase: %s\n", firebaseInitialized ? "Initialized" : "Not initialized");
        Serial.printf("Stream: %s\n", isUserStreamActive() ? "Active" : "Inactive");
        Serial.printf("Users: %d\n", userDB.getUserCount());
        Serial.printf("Queue: %d/%d\n", attendanceQueue.size(), MAX_QUEUE_SIZE);
        Serial.println(F("=====================\n"));
    }
    else if (cmd == "sync") {
        Serial.println(F("Forcing sync..."));
        syncQueuedAttendance();
    }
    else if (cmd == "mode auto") {
        currentMode = MODE_AUTO;
        saveSystemMode(currentMode);
        Serial.println(F("Mode set to AUTO"));
    }
    else if (cmd == "mode online") {
        currentMode = MODE_FORCE_ONLINE;
        saveSystemMode(currentMode);
        Serial.println(F("Mode set to FORCE ONLINE"));
    }
    else if (cmd == "mode offline") {
        currentMode = MODE_FORCE_OFFLINE;
        saveSystemMode(currentMode);
        isOnline = false;
        Serial.println(F("Mode set to FORCE OFFLINE"));
    }
    else if (cmd == "users") {
        userDB.printAllUsers();
    }
    else if (cmd == "queue") {
        attendanceQueue.printQueue();
    }
    else if (cmd == "clear queue") {
        attendanceQueue.clear();
        Serial.println(F("Queue cleared"));
    }
    else if (cmd == "clear wifi") {
        clearWiFiCredentials();
        Serial.println(F("WiFi credentials cleared. Restart to reconfigure."));
    }
    else if (cmd == "clear users") {
        userDB.clearCache();
        Serial.println(F("User cache cleared"));
    }
    else if (cmd == "fetch users") {
        if (isOnline) {
            fetchAllUsersFromFirebase();
        } else {
            Serial.println(F("Not online"));
        }
    }
    else if (cmd == "restart") {
        Serial.println(F("Restarting..."));
        delay(500);
        ESP.restart();
    }
    else if (cmd == "test") {
        testIndicators();
    }
    else if (cmd == "help") {
        Serial.println(F("\n=== Commands ==="));
        Serial.println(F("status      - Show system status"));
        Serial.println(F("sync        - Force queue sync"));
        Serial.println(F("mode auto   - Set auto mode"));
        Serial.println(F("mode online - Set force online mode"));
        Serial.println(F("mode offline- Set force offline mode"));
        Serial.println(F("users       - List registered users"));
        Serial.println(F("queue       - Show attendance queue"));
        Serial.println(F("clear queue - Clear attendance queue"));
        Serial.println(F("clear wifi  - Clear WiFi credentials"));
        Serial.println(F("clear users - Clear user cache"));
        Serial.println(F("fetch users - Fetch users from Firebase"));
        Serial.println(F("restart     - Restart device"));
        Serial.println(F("test        - Test indicators"));
        Serial.println(F("================\n"));
    }
    else {
        Serial.println(F("Unknown command. Type 'help' for list."));
    }
}

// =============================================================================
// USER CHANGE CALLBACK
// =============================================================================

void onUserChange(String uid, String name, bool added) {
    if (added) {
        Serial.printf("📥 User added via stream: %s (%s)\n", name.c_str(), uid.c_str());
        beepSuccess();
    } else {
        Serial.printf("📤 User removed via stream: %s\n", uid.c_str());
        beepDouble();
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    Serial.println(F("\n"));
    Serial.println(F("╔════════════════════════════════════════╗"));
    Serial.println(F("║     TapTrack Attendance System         ║"));
    Serial.println(F("║        [Enhanced Edition]              ║"));
    Serial.println(F("╚════════════════════════════════════════╝"));
    Serial.println();
    
    // Initialize SPI
    SPI.begin();
    
    // Startup LED sequence
    initIndicator();
    startupSequence();
    
    // Initialize SPIFFS
    Serial.println(F("💾 Initializing storage..."));
    if (!SPIFFS.begin(true)) {
        Serial.println(F("⚠️ SPIFFS mount failed, formatting..."));
        SPIFFS.format();
        SPIFFS.begin(true);
    }
    Serial.println(F("✓ SPIFFS ready"));
    
    // Initialize User Database
    Serial.println(F("📂 Loading user database..."));
    userDB.init();
    Serial.printf("✓ %d users loaded\n", userDB.getUserCount());
    
    // Initialize Attendance Queue
    Serial.println(F("📝 Loading attendance queue..."));
    attendanceQueue.init();
    if (!attendanceQueue.isEmpty()) {
        Serial.printf("✓ %d queued records found\n", attendanceQueue.size());
    }
    
    // Load saved mode
    currentMode = loadSystemMode();
    Serial.printf("📱 Mode: %s\n", 
                 currentMode == MODE_AUTO ? "AUTO" :
                 currentMode == MODE_FORCE_ONLINE ? "FORCE ONLINE" : "FORCE OFFLINE");
    
    // Initialize WiFi
    Serial.println(F("\n🌐 Initializing WiFi..."));
    bool wifiConnected = initWiFiManager();
    isOnline = wifiConnected && (currentMode != MODE_FORCE_OFFLINE);
    
    if (isOnline) {
        Serial.println(F("✅ WiFi Connected"));
        Serial.print(F("   IP: "));
        Serial.println(WiFi.localIP());
    } else {
        Serial.println(F("📴 Running in OFFLINE mode"));
    }
    
    // Initialize RFID
    Serial.println(F("\n📡 Initializing RFID..."));
    initRFID();
    if (isRFIDHealthy()) {
        Serial.println(F("✓ RFID module ready"));
    } else {
        Serial.println(F("⚠️ RFID module not detected!"));
    }
    
    // Initialize RTC
    Serial.println(F("\n🕐 Initializing RTC..."));
    if (isOnline) {
        setupAndSyncRTC();
    } else {
        rtc.begin();
        DateTime now = getCurrentTime();
        Serial.printf("   Time: %02d/%02d/%04d %02d:%02d:%02d\n",
                     now.month, now.day, now.year,
                     now.hour, now.minute, now.second);
        if (!isRTCValid(now)) {
            Serial.println(F("⚠️ RTC time may be invalid!"));
        }
    }
    Serial.println(F("✓ RTC ready"));
    
    // Initialize Firebase if online
    if (isOnline && currentMode != MODE_FORCE_OFFLINE) {
        Serial.println(F("\n🔥 Initializing Firebase..."));
        initFirebase();
        
        // Wait for Firebase to be ready
        int attempts = 0;
        while (!app.ready() && attempts < 100) {
            app.loop();
            delay(50);
            attempts++;
        }
        
        if (app.ready()) {
            firebaseInitialized = true;
            Serial.println(F("✓ Firebase connected"));
            
            // Set user change callback
            setUserChangeCallback(onUserChange);
            
            // Fetch users and start streaming
            fetchAllUsersFromFirebase();
            
            // Wait a bit for users to load
            delay(2000);
            app.loop();
            
            // Start streaming for realtime updates
            streamUsers();
            
            // Sync any queued attendance
            if (!attendanceQueue.isEmpty()) {
                Serial.printf("📤 Syncing %d queued records...\n", attendanceQueue.size());
                delay(1000);
            }
        } else {
            Serial.println(F("⚠️ Firebase connection timeout"));
        }
    }
    
    // Setup mode button
    gpio_pin_init_pullup(MODE_BUTTON_PIN, GPIO_INPUT_MODE, GPIO_PULL_UP);
    
    // Setup RFID interrupt
    gpio_pin_init_pullup(RFID_IRQ_PIN, GPIO_INPUT_MODE, GPIO_PULL_UP);
    enableInterrupt();
    cardDetected = false;
    attachInterrupt(digitalPinToInterrupt(RFID_IRQ_PIN), readCardISR, FALLING);
    
    // Show registered users
    userDB.printAllUsers();
    
    // Show queue status
    if (!attendanceQueue.isEmpty()) {
        attendanceQueue.printQueue();
    }
    
    // Ready!
    Serial.println(F("\n╔════════════════════════════════════════╗"));
    Serial.println(F("║           System Ready!                ║"));
    Serial.println(F("╚════════════════════════════════════════╝"));
    Serial.printf("Mode: %s | Online: %s\n",
                 currentMode == MODE_AUTO ? "AUTO" :
                 currentMode == MODE_FORCE_ONLINE ? "ONLINE" : "OFFLINE",
                 isOnline ? "Yes" : "No");
    Serial.println(F("\n🎫 Tap RFID card to record attendance"));
    Serial.println(F("📝 Type 'help' for commands\n"));
    
    // Show mode indicator
    indicateMode(currentMode);
    
    delay(500);
}

// =============================================================================
// MAIN LOOP
// =============================================================================

void loop() {
    unsigned long now = millis();
    
    // Reset RFID module periodically
    checkAndResetMFRC522();
    
    // Check mode button
    if (now - lastButtonCheck > 50) {
        lastButtonCheck = now;
        checkModeButton();
    }
    
    // Update indicators (for blinking states)
    if (now - lastIndicatorUpdate > 50) {
        lastIndicatorUpdate = now;
        updateIndicator();
    }
    
    // Periodic WiFi check
    if (currentMode != MODE_FORCE_OFFLINE && (now - lastWifiCheck > WIFI_CHECK_INTERVAL_MS)) {
        lastWifiCheck = now;
        checkAndReconnectWiFi();
    }
    
    // Process Firebase events
    if (isOnline && firebaseInitialized) {
        app.loop();
        
        // Check if sync completed
        if (pendingSyncId.length() > 0 && isSyncConfirmed(pendingSyncId)) {
            attendanceQueue.dequeueBySyncId(pendingSyncId);
            pendingSyncId = "";
            indicateSyncing(false);
        }
        
        // Periodic queue sync
        if (!attendanceQueue.isEmpty() && (now - lastSyncAttempt > SYNC_INTERVAL_MS)) {
            lastSyncAttempt = now;
            syncQueuedAttendance();
        }
    }
    
    // Process serial commands
    processSerialCommand();
    
    // Process card detection
    if (cardDetected) {
        Serial.print("(Interrupt: Card Detected)"); 
        indicateProcessing(true);

        String uid = readCardUID();
    
        if (uid.length() > 0) {
            processCard(uid);
        } else {
            Serial.println(" Failed to read UID. Try again.");
            indicateError();
        }
        
        // Clear and re-enable interrupt
        clearInt();
        cardDetected = false;
        clearIndicators();
    }

    // Re-activate receiver
    activateRec();

    // Small delay
    delay(10);
}