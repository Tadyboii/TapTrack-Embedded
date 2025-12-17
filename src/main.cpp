/*
 * TapTrack - ESP32 RFID Attendance System
 * Main Application Entry Point - FSM Architecture
 * 
 * State Machine:
 * INITIALIZE → IDLE → PROCESS_CARD → UPLOAD_DATA/QUEUE_DATA → IDLE
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
    STATE_QUEUE_DATA
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
static SystemState previousState = STATE_INITIALIZE;

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
    unsigned long syncStartTime;
    int uploadRetries;
    
    void reset() {
        cardUID = "";
        userName = "";
        timestamp = "";
        attendanceStatus = "";
        registrationStatus = "";
        isRegistered = false;
        syncId = "";
        syncStartTime = 0;
        uploadRetries = 0;
    }
};

static StateContext stateContext;

// Timing
static unsigned long lastWifiCheck = 0;
static unsigned long lastIndicatorUpdate = 0;
static unsigned long lastButtonCheck = 0;
static unsigned long lastQueueSyncAttempt = 0;

// Duplicate tap prevention
static String lastTapUID = "";
static unsigned long lastTapTime = 0;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void transitionTo(SystemState newState);
void handleInitialize();
void handleIdle();
void handleProcessCard();
void handleUploadData();
void handleQueueData();

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
            indicateSyncing(false);
            break;
        default:
            break;
    }
    
    previousState = currentState;
    currentState = newState;
    
    // Debug output
    const char* stateNames[] = {"INITIALIZE", "IDLE", "PROCESS_CARD", "UPLOAD_DATA", "QUEUE_DATA"};
    Serial.printf("[STATE] %s -> %s\n", stateNames[previousState], stateNames[currentState]);
    
    // State entry actions
    switch (newState) {
        case STATE_IDLE:
            clearIndicators();
            break;
        case STATE_PROCESS_CARD:
            indicateProcessing(true);
            break;
        case STATE_UPLOAD_DATA:
            indicateSyncing(true);
            break;
        default:
            break;
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
            Serial.println(F("\n[BUTTON] Long press - Clearing WiFi credentials"));
            clearWiFiCredentials();
            beepLong();
            delay(1000);
            ESP.restart();
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
            Serial.println(F("[MODE] FORCE ONLINE"));
            break;
        case MODE_FORCE_ONLINE:
            currentMode = MODE_FORCE_OFFLINE;
            Serial.println(F("[MODE] FORCE OFFLINE"));
            break;
        case MODE_FORCE_OFFLINE:
            currentMode = MODE_AUTO;
            Serial.println(F("[MODE] AUTO"));
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
    
    if (lastTapUID == uid && (now - lastTapTime) < TAP_COOLDOWN_MS) {
        Serial.printf("[WARN] Duplicate tap (wait %lu sec)\n", 
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
            Serial.println(F("[WIFI] Reconnected"));
            isOnline = true;
            
            if (!firebaseInitialized && currentMode != MODE_FORCE_OFFLINE) {
                Serial.println(F("[FIREBASE] Reinitializing..."));
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
            Serial.println(F("[WIFI] Disconnected"));
            isOnline = false;
        }
        
        if (currentMode == MODE_FORCE_ONLINE) {
            Serial.println(F("[WIFI] Attempting reconnection..."));
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
// USER CHANGE CALLBACK
// =============================================================================

void onUserChange(String uid, String name, bool added) {
    if (added) {
        Serial.printf("[STREAM] User added: %s (%s)\n", name.c_str(), uid.c_str());
        beepSuccess();
    } else {
        Serial.printf("[STREAM] User removed: %s\n", uid.c_str());
        beepDouble();
    }
}

// =============================================================================
// STATE HANDLERS
// =============================================================================

void handleInitialize() {
    Serial.println(F("\n╔════════════════════════════════════════╗"));
    Serial.println(F("║     TapTrack Attendance System         ║"));
    Serial.println(F("║        [FSM Architecture]              ║"));
    Serial.println(F("╚════════════════════════════════════════╝\n"));
    
    // Initialize SPI
    SPI.begin();
    
    // Startup LED sequence
    initIndicator();
    startupSequence();
    
    // Initialize SPIFFS
    Serial.println(F("[SPIFFS] Initializing storage..."));
    if (!SPIFFS.begin(true)) {
        Serial.println(F("[SPIFFS] Mount failed, formatting..."));
        SPIFFS.format();
        SPIFFS.begin(true);
    }
    Serial.println(F("[SPIFFS] Ready"));
    
    // Initialize User Database
    Serial.println(F("[DB] Loading user database..."));
    userDB.init();
    Serial.printf("[DB] %d users loaded\n", userDB.getUserCount());
    
    // Initialize Attendance Queue
    Serial.println(F("[QUEUE] Loading attendance queue..."));
    attendanceQueue.init();
    if (!attendanceQueue.isEmpty()) {
        Serial.printf("[QUEUE] %d queued records found\n", attendanceQueue.size());
    }
    
    // Load saved mode
    currentMode = loadSystemMode();
    Serial.printf("[MODE] %s\n", 
                 currentMode == MODE_AUTO ? "AUTO" :
                 currentMode == MODE_FORCE_ONLINE ? "FORCE ONLINE" : "FORCE OFFLINE");
    
    // Initialize WiFi
    Serial.println(F("\n[WIFI] Initializing..."));
    bool wifiConnected = initWiFiManager();
    isOnline = wifiConnected && (currentMode != MODE_FORCE_OFFLINE);
    
    if (isOnline) {
        Serial.println(F("[WIFI] Connected"));
        Serial.print(F("[WIFI] IP: "));
        Serial.println(WiFi.localIP());
    } else {
        Serial.println(F("[WIFI] Running in OFFLINE mode"));
    }
    
    // Initialize RFID
    Serial.println(F("\n[RFID] Initializing..."));
    initRFID();
    if (isRFIDHealthy()) {
        Serial.println(F("[RFID] Ready"));
    } else {
        Serial.println(F("[RFID] Module not detected!"));
    }
    
    // Initialize RTC
    Serial.println(F("\n[RTC] Initializing..."));
    if (isOnline) {
        setupAndSyncRTC();
    } else {
        rtc.begin();
        DateTime now = getCurrentTime();
        Serial.printf("[RTC] Time: %02d/%02d/%04d %02d:%02d:%02d\n",
                     now.month, now.day, now.year,
                     now.hour, now.minute, now.second);
        if (!isRTCValid(now)) {
            Serial.println(F("[RTC] Time may be invalid!"));
        }
    }
    Serial.println(F("[RTC] Ready"));
    
    // Initialize Firebase if online
    if (isOnline && currentMode != MODE_FORCE_OFFLINE) {
        Serial.println(F("\n[FIREBASE] Initializing..."));
        initFirebase();
        
        int attempts = 0;
        while (!app.ready() && attempts < 100) {
            app.loop();
            delay(50);
            attempts++;
        }
        
        if (app.ready()) {
            firebaseInitialized = true;
            Serial.println(F("[FIREBASE] Connected"));
            
            setUserChangeCallback(onUserChange);
            fetchAllUsersFromFirebase();
            delay(2000);
            app.loop();
            streamUsers();
            
            if (!attendanceQueue.isEmpty()) {
                Serial.printf("[SYNC] %d queued records ready for sync\n", attendanceQueue.size());
            }
        } else {
            Serial.println(F("[FIREBASE] Connection timeout"));
        }
    }
    
    // Setup mode button
    gpio_pin_init_pullup(MODE_BUTTON_PIN, GPIO_INPUT_MODE, GPIO_PULL_UP);
    
    // Setup RFID interrupt
    gpio_pin_init_pullup(RFID_IRQ_PIN, GPIO_INPUT_MODE, GPIO_PULL_UP);
    enableInterrupt();
    cardDetected = false;
    attachInterrupt(digitalPinToInterrupt(RFID_IRQ_PIN), readCardISR, FALLING);
    
    // Show status
    userDB.printAllUsers();
    if (!attendanceQueue.isEmpty()) {
        attendanceQueue.printQueue();
    }
    
    Serial.println(F("\n╔════════════════════════════════════════╗"));
    Serial.println(F("║           System Ready!                ║"));
    Serial.println(F("╚════════════════════════════════════════╝"));
    Serial.printf("Mode: %s | Online: %s\n",
                 currentMode == MODE_AUTO ? "AUTO" :
                 currentMode == MODE_FORCE_ONLINE ? "ONLINE" : "OFFLINE",
                 isOnline ? "Yes" : "No");
    Serial.println(F("\nTap RFID card to record attendance"));
    Serial.println(F("Type 'help' for commands\n"));
    
    indicateMode(currentMode);
    
    // Transition to IDLE
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
    
    // Process Firebase events
    if (isOnline && firebaseInitialized) {
        app.loop();
    }
    
    // Periodic queue sync (only if not currently processing a card)
    if (isOnline && !attendanceQueue.isEmpty() && 
        currentMode != MODE_FORCE_OFFLINE &&
        (now - lastQueueSyncAttempt > SYNC_INTERVAL_MS)) {
        lastQueueSyncAttempt = now;
        
        // Check if we should transition to UPLOAD_DATA for queue processing
        AttendanceRecord* record = attendanceQueue.peek();
        if (record && record->retryCount <= 5) {
            stateContext.reset();
            stateContext.cardUID = record->uid;
            stateContext.userName = record->name;
            stateContext.timestamp = record->timestamp;
            stateContext.attendanceStatus = record->attendanceStatus;
            stateContext.registrationStatus = record->registrationStatus;
            stateContext.isRegistered = true;
            
            Serial.println(F("[QUEUE] Processing queued record..."));
            transitionTo(STATE_UPLOAD_DATA);
            return;
        }
    }
    
    // Check for card detection
    if (cardDetected) {
        delay(50); // Card stabilization
        transitionTo(STATE_PROCESS_CARD);
        return;
    }
    
    activateRec();
}

void handleProcessCard() {
    // Read card UID
    String uid = readCardUID();
    
    if (uid.length() == 0) {
        Serial.println(F("[ERROR] Failed to read card. Try again."));
        indicateError();
        clearInt();
        cardDetected = false;
        transitionTo(STATE_IDLE);
        return;
    }
    
    // Get current time
    DateTime time = getCurrentTime();
    
    // Validate RTC
    if (!isRTCValid(time)) {
        Serial.println(F("[ERROR] RTC time invalid!"));
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
    
    // Lookup user
    UserInfo userInfo = userDB.getUserInfo(uid);
    stateContext.userName = userInfo.name;
    stateContext.isRegistered = userInfo.isRegistered;
    stateContext.registrationStatus = userInfo.isRegistered ? "registered" : "unregistered";
    
    // Print info
    Serial.println(F("\n========================================"));
    Serial.printf("Card UID: %s\n", uid.c_str());
    Serial.printf("Time: %02d/%02d/%04d %02d:%02d:%02d\n",
                 time.month, time.day, time.year,
                 time.hour, time.minute, time.second);
    
    if (stateContext.isRegistered) {
        Serial.printf("User: %s (Registered)\n", stateContext.userName.c_str());
        userDB.recordTap(uid);
    } else {
        Serial.println(F("User: Unknown (Unregistered)"));
    }
    Serial.println(F("========================================\n"));
    
    // Clear interrupt
    clearInt();
    cardDetected = false;
    
    // Decide next state based on connectivity and registration
    if (isOnline && currentMode != MODE_FORCE_OFFLINE) {
        // ONLINE PATH
        if (stateContext.isRegistered) {
            transitionTo(STATE_UPLOAD_DATA);
        } else {
            // Unregistered - send to pending users
            Serial.println(F("[PENDING] Sending to pending users"));
            sendPendingUser(uid, stateContext.timestamp);
            fetchUserFromFirebase(uid);
            indicateSuccessOnline();
            transitionTo(STATE_IDLE);
        }
    } else {
        // OFFLINE PATH
        if (stateContext.isRegistered) {
            transitionTo(STATE_QUEUE_DATA);
        } else {
            Serial.println(F("[ERROR] Offline + Unregistered - Cannot process"));
            indicateErrorUnregistered();
            transitionTo(STATE_IDLE);
        }
    }
}

void handleUploadData() {
    // Check if still online
    if (!isOnline || !isFirebaseReady()) {
        Serial.println(F("[WARN] Lost connection during upload"));
        transitionTo(STATE_QUEUE_DATA);
        return;
    }
    
    // Attempt upload
    stateContext.syncId = sendToFirebase(
        stateContext.cardUID,
        stateContext.userName,
        stateContext.timestamp,
        stateContext.attendanceStatus,
        stateContext.registrationStatus
    );
    
    if (stateContext.syncId.length() > 0) {
        // Upload initiated - wait for confirmation
        stateContext.syncStartTime = millis();
        
        // Poll for confirmation (non-blocking with timeout)
        int maxWait = 50; // 50 * 100ms = 5 seconds max
        int waitCount = 0;
        
        while (waitCount < maxWait) {
            app.loop();
            
            if (isSyncConfirmed(stateContext.syncId)) {
                Serial.println(F("[SYNC] Upload confirmed"));
                
                // If this was from the queue, remove it
                if (!attendanceQueue.isEmpty()) {
                    AttendanceRecord* record = attendanceQueue.peek();
                    if (record && record->uid == stateContext.cardUID) {
                        attendanceQueue.dequeueBySyncId(stateContext.syncId);
                    }
                }
                
                indicateSuccessOnline();
                transitionTo(STATE_IDLE);
                return;
            }
            
            delay(100);
            waitCount++;
        }
        
        // Timeout - queue it
        Serial.println(F("[WARN] Upload timeout"));
        transitionTo(STATE_QUEUE_DATA);
    } else {
        // Upload failed immediately
        Serial.println(F("[ERROR] Upload failed"));
        stateContext.uploadRetries++;
        
        if (stateContext.uploadRetries > 2) {
            transitionTo(STATE_QUEUE_DATA);
        } else {
            delay(500);
            // Retry
        }
    }
}

void handleQueueData() {
    if (attendanceQueue.isFull()) {
        Serial.println(F("[ERROR] Queue full! Cannot record attendance."));
        indicateErrorQueueFull();
        transitionTo(STATE_IDLE);
        return;
    }
    
    Serial.println(F("[QUEUE] Queuing locally"));
    attendanceQueue.enqueue(
        stateContext.cardUID,
        stateContext.userName,
        stateContext.timestamp,
        stateContext.attendanceStatus,
        stateContext.registrationStatus
    );
    
    indicateSuccessOffline();
    transitionTo(STATE_IDLE);
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
        const char* stateNames[] = {"INITIALIZE", "IDLE", "PROCESS_CARD", "UPLOAD_DATA", "QUEUE_DATA"};
        Serial.println(F("\n=== System Status ==="));
        Serial.printf("State: %s\n", stateNames[currentState]);
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
// SETUP & MAIN LOOP
// =============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    // Start in INITIALIZE state
    currentState = STATE_INITIALIZE;
}

void loop() {
    // Process serial commands (available in all states)
    processSerialCommand();
    
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
    }
    
    delay(10);
}