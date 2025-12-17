/*
 * TapTrack - ESP32 RFID Attendance System
 * FSM Architecture - Fixed & Optimized Version
 * 
 * FIXES:
 * - Non-blocking uploads with async callbacks
 * - State timeout watchdog (10s max per state)
 * - Atomic queue operations with confirmation tracking
 * - Separate queue sync state to prevent card blocking
 * - Critical-only serial output for speed
 * - Interrupt-safe card detection
 * - Firebase connection retry logic
 * - Memory-efficient string handling
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
// STATE MACHINE DEFINITION
// =============================================================================

enum SystemState {
    STATE_INITIALIZE,
    STATE_IDLE,
    STATE_PROCESS_CARD,
    STATE_UPLOAD_DATA,
    STATE_QUEUE_DATA,
    STATE_SYNC_QUEUE      // New: dedicated queue sync state
};

// =============================================================================
// GLOBAL INSTANCES
// =============================================================================

UserDatabase userDB;
AttendanceQueue attendanceQueue;

// =============================================================================
// SYSTEM STATE
// =============================================================================

static SystemState currentState = STATE_INITIALIZE;
static SystemMode currentMode = DEFAULT_SYSTEM_MODE;
static bool isOnline = false;
bool firebaseInitialized = false;

// State machine data context
struct StateContext {
    String cardUID;
    String userName;
    String timestamp;
    String attendanceStatus;
    String registrationStatus;
    bool isRegistered;
    
    String syncId;
    unsigned long stateEntryTime;
    unsigned long syncStartTime;
    uint8_t uploadRetries;
    bool fromQueue;  // Track if this is from queue or live card
    
    void reset() {
        cardUID = "";
        userName = "";
        timestamp = "";
        attendanceStatus = "";
        registrationStatus = "";
        isRegistered = false;
        syncId = "";
        stateEntryTime = millis();
        syncStartTime = 0;
        uploadRetries = 0;
        fromQueue = false;
    }
    
    void updateEntryTime() {
        stateEntryTime = millis();
    }
};

static StateContext stateContext;

// Timing
static unsigned long lastWifiCheck = 0;
static unsigned long lastIndicatorUpdate = 0;
static unsigned long lastButtonCheck = 0;
static unsigned long lastQueueSyncAttempt = 0;
static unsigned long lastFirebaseRetry = 0;
// Card debounce
static unsigned long cardFirstDetectedAt = 0;
static bool cardDetectPending = false;

// Duplicate tap prevention - Enhanced for upload blocking
static String lastTapUID = "";
static unsigned long lastTapTime = 0;
static bool tapInProgress = false;  // Lock for in-progress uploads

// Upload tracking for atomic operations
struct UploadTracker {
    String syncId;
    String uid;
    unsigned long startTime;
    bool active;
    
    void start(String id, String cardUID) {
        syncId = id;
        uid = cardUID;
        startTime = millis();
        active = true;
    }
    
    void clear() {
        syncId = "";
        uid = "";
        active = false;
    }
    
    bool isTimeout() {
        return active && (millis() - startTime > 10000);
    }
};

static UploadTracker uploadTracker;

// =============================================================================
// FIREBASE NON-BLOCKING INIT/RETRY STATE + RESTART SCHEDULING
// =============================================================================

static bool firebaseInitInProgress = false;
static int firebaseInitAttempts = 0;
static unsigned long firebaseInitLastTick = 0;
static const int FIREBASE_INIT_MAX_ATTEMPTS = 100;

static bool firebaseRetryInProgress = false;
static int firebaseRetryAttempts = 0;
static unsigned long firebaseRetryLastTick = 0;
static const int FIREBASE_RETRY_MAX_ATTEMPTS = 50;

static bool firebaseStreamPending = false;
static unsigned long firebaseStreamAt = 0;

// Restart scheduling for long-press action
static bool restartPending = false;
static unsigned long restartAt = 0;

// =============================================================================
// CONFIGURATION
// =============================================================================

#define STATE_TIMEOUT_MS 10000
#define UPLOAD_TIMEOUT_MS 8000
#define FIREBASE_RETRY_INTERVAL_MS 30000
#define SERIAL_OUTPUT_MINIMAL  // Comment out for verbose logging

// =============================================================================
// LOGGING MACROS
// =============================================================================

#ifdef SERIAL_OUTPUT_MINIMAL
    #define LOG_INFO(...)
    #define LOG_WARN(...)
    #define LOG_ERROR(...) Serial.printf(__VA_ARGS__)
    #define LOG_STATE(...)
    #define LOG_CARD(...) Serial.printf(__VA_ARGS__)
#else
    #define LOG_INFO(...) Serial.printf(__VA_ARGS__)
    #define LOG_WARN(...) Serial.printf(__VA_ARGS__)
    #define LOG_ERROR(...) Serial.printf(__VA_ARGS__)
    #define LOG_STATE(...) Serial.printf(__VA_ARGS__)
    #define LOG_CARD(...) Serial.printf(__VA_ARGS__)
#endif

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void transitionTo(SystemState newState);
void handleInitialize();
void handleIdle();
void handleProcessCard();
void handleUploadData();
void handleQueueData();
void handleSyncQueue();
void checkStateTimeout();
void onUserChange(String uid, String name, bool added);

// =============================================================================
// STATE TRANSITION
// =============================================================================

void transitionTo(SystemState newState) {
    if (currentState == newState) return;
    
    // State exit actions
    switch (currentState) {
        case STATE_PROCESS_CARD:
            indicateProcessing(false);
            break;
        case STATE_UPLOAD_DATA:
        case STATE_SYNC_QUEUE:
            indicateSyncing(false);
            break;
        default:
            break;
    }
    
    SystemState oldState = currentState;
    currentState = newState;
    stateContext.updateEntryTime();
    
    #ifndef SERIAL_OUTPUT_MINIMAL
    const char* stateNames[] = {"INIT", "IDLE", "PROCESS", "UPLOAD", "QUEUE", "SYNC"};
    LOG_STATE("[%s->%s]\n", stateNames[oldState], stateNames[currentState]);
    #endif
    
    // State entry actions
    switch (newState) {
        case STATE_IDLE:
            clearIndicators();
            tapInProgress = false;  // Release lock
            break;
        case STATE_PROCESS_CARD:
            indicateProcessing(true);
            tapInProgress = true;  // Set lock
            break;
        case STATE_UPLOAD_DATA:
        case STATE_SYNC_QUEUE:
            indicateSyncing(true);
            break;
        default:
            break;
    }
}

// =============================================================================
// STATE TIMEOUT WATCHDOG
// =============================================================================

void checkStateTimeout() {
    if (currentState == STATE_IDLE || currentState == STATE_INITIALIZE) {
        return;  // These states can be indefinite
    }
    
    unsigned long elapsed = millis() - stateContext.stateEntryTime;
    
    if (elapsed > STATE_TIMEOUT_MS) {
        LOG_ERROR("[TIMEOUT] State hung for %lums, forcing IDLE\n", elapsed);
        
        // Clean up based on state
        if (currentState == STATE_UPLOAD_DATA || currentState == STATE_SYNC_QUEUE) {
            // Upload timed out - queue it
            if (!stateContext.fromQueue && stateContext.isRegistered) {
                attendanceQueue.enqueue(
                    stateContext.cardUID,
                    stateContext.userName,
                    stateContext.timestamp,
                    stateContext.attendanceStatus,
                    stateContext.registrationStatus
                );
            }
        }
        
        uploadTracker.clear();
        transitionTo(STATE_IDLE);
    }
}

// =============================================================================
// BUTTON HANDLING
// =============================================================================

static bool modeButtonPressed = false;
static unsigned long modeButtonPressTime = 0;

void toggleMode();

void checkModeButton() {
    static bool lastButtonState = HIGH;
    bool currentButtonState = gpio_read(MODE_BUTTON_PIN);
    
    if (currentButtonState != lastButtonState) {
        delay(BUTTON_DEBOUNCE_MS);
        currentButtonState = gpio_read(MODE_BUTTON_PIN);
    }
    
    if (currentButtonState == LOW && lastButtonState == HIGH) {
        modeButtonPressTime = millis();
        modeButtonPressed = true;
    }
    
    if (currentButtonState == HIGH && lastButtonState == LOW && modeButtonPressed) {
        unsigned long pressDuration = millis() - modeButtonPressTime;
        modeButtonPressed = false;
        
        if (pressDuration > 3000) {
            LOG_INFO("[BTN] Clear WiFi\n");
            clearWiFiCredentials();
            // Non-blocking restart: start long beep and schedule restart
            beepLong();
            restartPending = true;
            restartAt = millis() + 1000;
        } else if (pressDuration > 100) {
            toggleMode();
        }
    }
    
    lastButtonState = currentButtonState;
}

void toggleMode() {
    switch (currentMode) {
        case MODE_AUTO:
            currentMode = MODE_FORCE_ONLINE;
            LOG_INFO("[MODE] ONLINE\n");
            break;
        case MODE_FORCE_ONLINE:
            currentMode = MODE_FORCE_OFFLINE;
            LOG_INFO("[MODE] OFFLINE\n");
            break;
        case MODE_FORCE_OFFLINE:
            currentMode = MODE_AUTO;
            LOG_INFO("[MODE] AUTO\n");
            break;
    }
    
    saveSystemMode(currentMode);
    indicateMode(currentMode);
    beepSuccess();
    
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
    return "present";
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
    
    // Enhanced: Check both cooldown AND in-progress flag
    if (lastTapUID == uid && (now - lastTapTime) < TAP_COOLDOWN_MS) {
        return true;
    }
    
    // Block taps while upload in progress
    if (tapInProgress && lastTapUID == uid) {
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
            LOG_INFO("[WIFI] Reconnected\n");
            isOnline = true;
            
            if (!firebaseInitialized && currentMode != MODE_FORCE_OFFLINE) {
                LOG_INFO("[FB] Reinit\n");
                initFirebase();
                firebaseInitialized = true;
                
                if (!isUserStreamActive()) {
                    streamUsers();
                }
            }
        }
        return true;
    } else {
        if (isOnline) {
            LOG_WARN("[WIFI] Disconnected\n");
            isOnline = false;
        }
        
        if (currentMode == MODE_FORCE_ONLINE) {
            indicateConnecting(true);
            bool connected = reconnectWiFi();
            indicateConnecting(false);
            isOnline = connected;
            return connected;
        }
        
        return false;
    }
}

// Firebase connection retry logic
void checkFirebaseConnection() {
    if (!isOnline || currentMode == MODE_FORCE_OFFLINE) {
        return;
    }
    
    if (!firebaseInitialized || !app.ready()) {
        unsigned long now = millis();
        if (now - lastFirebaseRetry > FIREBASE_RETRY_INTERVAL_MS) {
            lastFirebaseRetry = now;
            LOG_INFO("[FB] Retry connect\n");

            initFirebase();
            firebaseRetryInProgress = true;
            firebaseRetryAttempts = 0;
            firebaseRetryLastTick = millis();
        }
    }
}

// Service Firebase init/retry tasks (non-blocking). Call from main loop.
void serviceFirebaseTasks() {
    unsigned long now = millis();

    // Service initial init sequence
    if (firebaseInitInProgress) {
        if (app.ready()) {
            firebaseInitialized = true;
            LOG_INFO("[FB] Connected (init)\n");
            setUserChangeCallback(onUserChange);
            fetchAllUsersFromFirebase();
            // schedule streaming shortly after to let app settle
            firebaseStreamPending = true;
            firebaseStreamAt = now + 2000;
            firebaseInitInProgress = false;
        } else if (now - firebaseInitLastTick >= 50) {
            app.loop();
            firebaseInitAttempts++;
            firebaseInitLastTick = now;
            if (firebaseInitAttempts >= FIREBASE_INIT_MAX_ATTEMPTS) {
                LOG_WARN("[FB] Init attempts exhausted\n");
                firebaseInitInProgress = false;
            }
        }
    }

    // Service retry sequence
    if (firebaseRetryInProgress) {
        if (app.ready()) {
            firebaseInitialized = true;
            LOG_INFO("[FB] Connected (retry)\n");
            if (!isUserStreamActive()) {
                streamUsers();
            }
            firebaseRetryInProgress = false;
        } else if (now - firebaseRetryLastTick >= 50) {
            app.loop();
            firebaseRetryAttempts++;
            firebaseRetryLastTick = now;
            if (firebaseRetryAttempts >= FIREBASE_RETRY_MAX_ATTEMPTS) {
                LOG_WARN("[FB] Retry attempts exhausted\n");
                firebaseRetryInProgress = false;
            }
        }
    }

    // Service delayed streaming after init
    if (firebaseStreamPending && now >= firebaseStreamAt) {
        app.loop();
        streamUsers();
        firebaseStreamPending = false;
    }
}

// =============================================================================
// USER CHANGE CALLBACK
// =============================================================================

void onUserChange(String uid, String name, bool added) {
    if (added) {
        LOG_INFO("[USER+] %s\n", name.c_str());
        beepSuccess();
    } else {
        LOG_INFO("[USER-] %s\n", uid.c_str());
        beepDouble();
    }
}

// =============================================================================
// STATE HANDLERS
// =============================================================================

void handleInitialize() {
    Serial.println(F("\n=== TapTrack FSM v2.0 ===\n"));
    
    SPI.begin();
    initIndicator();
    startupSequence();
    
    // SPIFFS
    if (!SPIFFS.begin(true)) {
        SPIFFS.format();
        SPIFFS.begin(true);
    }
    
    // Load data
    userDB.init();
    attendanceQueue.init();
    currentMode = loadSystemMode();
    
    LOG_INFO("[DB] %d users\n", userDB.getUserCount());
    LOG_INFO("[QUEUE] %d records\n", attendanceQueue.size());
    
    // WiFi
    bool wifiConnected = initWiFiManager();
    isOnline = wifiConnected && (currentMode != MODE_FORCE_OFFLINE);
    
    if (isOnline) {
        LOG_INFO("[WIFI] %s\n", WiFi.localIP().toString().c_str());
    }
    
    // RFID
    initRFID();
    
    // RTC
    if (isOnline) {
        setupAndSyncRTC();
    } else {
        rtc.begin();
        DateTime now = getCurrentTime();
        if (!isRTCValid(now)) {
            LOG_ERROR("[RTC] Invalid time\n");
        }
    }
    
    // Firebase - start non-blocking init if online
    if (isOnline && currentMode != MODE_FORCE_OFFLINE) {
        initFirebase();
        firebaseInitInProgress = true;
        firebaseInitAttempts = 0;
        firebaseInitLastTick = millis();
    }
    
    // GPIO
    gpio_pin_init_pullup(MODE_BUTTON_PIN, GPIO_INPUT_MODE, GPIO_PULL_UP);
    gpio_pin_init_pullup(RFID_IRQ_PIN, GPIO_INPUT_MODE, GPIO_PULL_UP);
    enableInterrupt();
    cardDetected = false;
    attachInterrupt(digitalPinToInterrupt(RFID_IRQ_PIN), readCardISR, FALLING);
    
    Serial.println(F("=== READY ===\n"));
    indicateMode(currentMode);
    
    transitionTo(STATE_IDLE);
}

void handleIdle() {
    unsigned long now = millis();
    
    // Background tasks
    checkAndResetMFRC522();
    
    if (now - lastButtonCheck > 50) {
        lastButtonCheck = now;
        checkModeButton();
    }
    
    if (now - lastIndicatorUpdate > 50) {
        lastIndicatorUpdate = now;
        updateIndicator();
    }
    
    if (currentMode != MODE_FORCE_OFFLINE && (now - lastWifiCheck > WIFI_CHECK_INTERVAL_MS)) {
        lastWifiCheck = now;
        checkAndReconnectWiFi();
    }
    
    // Firebase connection check
    checkFirebaseConnection();
    
    // Process Firebase events
    if (isOnline && firebaseInitialized) {
        app.loop();
    }
    
    // Check for completed uploads
    if (uploadTracker.active) {
        if (isSyncConfirmed(uploadTracker.syncId)) {
            LOG_INFO("[SYNC] Confirmed %s\n", uploadTracker.uid.c_str());
            
            // Atomically remove from queue if this was queue sync
            if (!attendanceQueue.isEmpty()) {
                AttendanceRecord* record = attendanceQueue.peek();
                if (record && record->uid == uploadTracker.uid) {
                    attendanceQueue.dequeueBySyncId(uploadTracker.syncId);
                }
            }
            
            uploadTracker.clear();
            indicateSuccessOnline();
        } else if (uploadTracker.isTimeout()) {
            LOG_WARN("[SYNC] Timeout %s\n", uploadTracker.uid.c_str());
            uploadTracker.clear();
        }
    }
    
    // Periodic queue sync (only if no upload in progress)
    if (isOnline && !attendanceQueue.isEmpty() && 
        !uploadTracker.active &&
        currentMode != MODE_FORCE_OFFLINE &&
        (now - lastQueueSyncAttempt > SYNC_INTERVAL_MS)) {
        lastQueueSyncAttempt = now;
        
        AttendanceRecord* record = attendanceQueue.peek();
        if (record && record->retryCount <= 5) {
            LOG_INFO("[QUEUE] Syncing...\n");
            stateContext.reset();
            stateContext.cardUID = record->uid;
            stateContext.userName = record->name;
            stateContext.timestamp = record->timestamp;
            stateContext.attendanceStatus = record->attendanceStatus;
            stateContext.registrationStatus = record->registrationStatus;
            stateContext.isRegistered = true;
            stateContext.fromQueue = true;
            
            transitionTo(STATE_SYNC_QUEUE);
            return;
        }
    }
    
    // Check for card detection (interrupt-safe)
    noInterrupts();
    bool cardPresent = cardDetected;
    interrupts();
    
    if (cardPresent && !uploadTracker.active) {
        if (!cardDetectPending) {
            cardDetectPending = true;
            cardFirstDetectedAt = now;
        }

        // Require the card to be present for a short stabilization window
        if (cardDetectPending && (now - cardFirstDetectedAt >= 20)) {
            cardDetectPending = false;
            transitionTo(STATE_PROCESS_CARD);
            return;
        }
    } else {
        // No card present, reset pending state
        cardDetectPending = false;
    }
    
    activateRec();
}

void handleProcessCard() {
    // Read card UID
    String uid = readCardUID();
    
    if (uid.length() == 0) {
        LOG_ERROR("[CARD] Failed to read UID\n");
        clearInt();
        cardDetected = false;
        transitionTo(STATE_IDLE);
        return;
    }
    
    // Get current time
    DateTime time = getCurrentTime();
    
    // Validate RTC
    if (!isRTCValid(time)) {
        LOG_ERROR("[RTC] Invalid\n");
        indicateErrorRTC();
        clearInt();
        cardDetected = false;
        transitionTo(STATE_IDLE);
        return;
    }
    
    // Check duplicate tap
    if (isDuplicateTap(uid)) {
        clearInt();
        cardDetected = false;
        transitionTo(STATE_IDLE);
        return;
    }
    
    // Populate state context
    stateContext.reset();
    stateContext.cardUID = uid;
    stateContext.timestamp = formatTimestamp(time);
    stateContext.attendanceStatus = getAttendanceStatus(time);
    stateContext.fromQueue = false;
    
    // Lookup user
    UserInfo userInfo = userDB.getUserInfo(uid);
    stateContext.userName = userInfo.name;
    stateContext.isRegistered = userInfo.isRegistered;
    stateContext.registrationStatus = userInfo.isRegistered ? "registered" : "unregistered";
    
    // Compact output
    LOG_CARD("%s|%s|%02d:%02d\n", 
             uid.c_str(),
             stateContext.isRegistered ? stateContext.userName.c_str() : "?",
             time.hour, time.minute);
    
    if (stateContext.isRegistered) {
        userDB.recordTap(uid);
    }
    
    // Clear interrupt
    clearInt();
    cardDetected = false;
    
    // Decide next state
    if (isOnline && currentMode != MODE_FORCE_OFFLINE) {
        if (stateContext.isRegistered) {
            indicateSuccessOnline();
            transitionTo(STATE_UPLOAD_DATA);
        } else {
            // Unregistered - send to pending
            sendPendingUser(uid, stateContext.timestamp);
            fetchUserFromFirebase(uid);
            indicateSuccessOnline();
            transitionTo(STATE_IDLE);
        }
    } else {
        if (stateContext.isRegistered) {
            transitionTo(STATE_QUEUE_DATA);
        } else {
            indicateErrorUnregistered();
            transitionTo(STATE_IDLE);
        }
    }
}

void handleUploadData() {
    // Check connection
    if (!isOnline || !isFirebaseReady()) {
        transitionTo(STATE_QUEUE_DATA);
        return;
    }
    
    // Start upload (non-blocking)
    String syncId = sendToFirebase(
        stateContext.cardUID,
        stateContext.userName,
        stateContext.timestamp,
        stateContext.attendanceStatus,
        stateContext.registrationStatus
    );
    
    if (syncId.length() > 0) {
        // Track upload for async confirmation
        uploadTracker.start(syncId, stateContext.cardUID);
        stateContext.syncId = syncId;
        stateContext.syncStartTime = millis();
        
        // Return to IDLE immediately - confirmation happens in background
        transitionTo(STATE_IDLE);
    } else {
        // Upload failed immediately
        stateContext.uploadRetries++;
        
        if (stateContext.uploadRetries > 2) {
            transitionTo(STATE_QUEUE_DATA);
        } else {
            // Non-blocking throttle before retrying
            static unsigned long lastUploadAttempt = 0;
            unsigned long now = millis();
            if (now - lastUploadAttempt < 200) {
                return; // wait before retrying
            }
            lastUploadAttempt = now;
            // Retry will occur on next loop iteration by staying in this state
        }
    }
}

void handleQueueData() {
    if (attendanceQueue.isFull()) {
        LOG_ERROR("[QUEUE] Full!\n");
        indicateErrorQueueFull();
        transitionTo(STATE_IDLE);
        return;
    }
    
    attendanceQueue.enqueue(
        stateContext.cardUID,
        stateContext.userName,
        stateContext.timestamp,
        stateContext.attendanceStatus,
        stateContext.registrationStatus
    );
    
    LOG_INFO("[QUEUE] Saved %s\n", stateContext.cardUID.c_str());
    indicateSuccessOffline();
    transitionTo(STATE_IDLE);
}

void handleSyncQueue() {
    // Same as handleUploadData but for queued records
    if (!isOnline || !isFirebaseReady()) {
        // Move to back of queue and try again later
        attendanceQueue.moveToBack();
        transitionTo(STATE_IDLE);
        return;
    }
    
    String syncId = sendToFirebase(
        stateContext.cardUID,
        stateContext.userName,
        stateContext.timestamp,
        stateContext.attendanceStatus,
        stateContext.registrationStatus
    );
    
    if (syncId.length() > 0) {
        uploadTracker.start(syncId, stateContext.cardUID);
        attendanceQueue.setSyncId(syncId);
        transitionTo(STATE_IDLE);
    } else {
        // Failed - move to back and retry later
        AttendanceRecord* record = attendanceQueue.peek();
        if (record) {
            record->retryCount++;
            if (record->retryCount > 5) {
                LOG_ERROR("[QUEUE] Max retries %s\n", stateContext.cardUID.c_str());
            }
        }
        attendanceQueue.moveToBack();
        transitionTo(STATE_IDLE);
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
    
    if (cmd == "status") {
        const char* stateNames[] = {"INIT", "IDLE", "PROCESS", "UPLOAD", "QUEUE", "SYNC"};
        Serial.printf("\n=== Status ===\n");
        Serial.printf("State: %s\n", stateNames[currentState]);
        Serial.printf("Mode: %s\n", 
                     currentMode == MODE_AUTO ? "AUTO" :
                     currentMode == MODE_FORCE_ONLINE ? "ONLINE" : "OFFLINE");
        Serial.printf("Online: %s\n", isOnline ? "YES" : "NO");
        Serial.printf("WiFi: %s\n", isWiFiConnected() ? "OK" : "DISC");
        Serial.printf("Firebase: %s\n", firebaseInitialized ? "OK" : "NO");
        Serial.printf("Users: %d\n", userDB.getUserCount());
        Serial.printf("Queue: %d/%d\n", attendanceQueue.size(), MAX_QUEUE_SIZE);
        Serial.printf("Upload: %s\n", uploadTracker.active ? "ACTIVE" : "IDLE");
        Serial.println();
    }
    else if (cmd == "mode auto") {
        currentMode = MODE_AUTO;
        saveSystemMode(currentMode);
        Serial.println(F("Mode: AUTO"));
    }
    else if (cmd == "mode online") {
        currentMode = MODE_FORCE_ONLINE;
        saveSystemMode(currentMode);
        Serial.println(F("Mode: ONLINE"));
    }
    else if (cmd == "mode offline") {
        currentMode = MODE_FORCE_OFFLINE;
        saveSystemMode(currentMode);
        isOnline = false;
        Serial.println(F("Mode: OFFLINE"));
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
        Serial.println(F("WiFi cleared"));
    }
    else if (cmd == "clear users") {
        userDB.clearCache();
        Serial.println(F("Users cleared"));
    }
    else if (cmd == "restart") {
        ESP.restart();
    }
    else if (cmd == "verbose") {
        Serial.println(F("Verbose mode - rebuild with SERIAL_OUTPUT_MINIMAL undefined"));
    }
    else if (cmd == "help") {
        Serial.println(F("\n=== Commands ==="));
        Serial.println(F("status       - System status"));
        Serial.println(F("mode [auto|online|offline]"));
        Serial.println(F("users        - List users"));
        Serial.println(F("queue        - Show queue"));
        Serial.println(F("clear queue  - Clear queue"));
        Serial.println(F("clear wifi   - Clear WiFi"));
        Serial.println(F("clear users  - Clear users"));
        Serial.println(F("restart      - Restart"));
        Serial.println();
    }
}

// =============================================================================
// SETUP & MAIN LOOP
// =============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    currentState = STATE_INITIALIZE;
    stateContext.reset();
}

void loop() {
    // Watchdog
    checkStateTimeout();
    
    // Serial commands
    processSerialCommand();
    
    // Service indicators (LED + non-blocking buzzer)
    updateIndicator();

    // Service Firebase initialization / retry tasks
    serviceFirebaseTasks();

    // Handle scheduled restart (non-blocking long-press restart)
    if (restartPending && millis() >= restartAt) {
        restartPending = false;
        ESP.restart();
    }
    
    // Run state machine
    switch (currentState) {
        case STATE_INITIALIZE:
            handleInitialize();
            break;
            
        case STATE_IDLE:
            handleIdle();
            break;
            
        case STATE_PROCESS_CARD:
            handleProcessCard();
            break;
            
        case STATE_UPLOAD_DATA:
            handleUploadData();
            break;
            
        case STATE_QUEUE_DATA:
            handleQueueData();
            break;
            
        case STATE_SYNC_QUEUE:
            handleSyncQueue();
            break;
    }
    
    delay(10);
}