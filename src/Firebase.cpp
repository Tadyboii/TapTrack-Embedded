/*
 * TapTrack - Firebase Module Implementation
 * Real-time sync with confirmation tracking
 */

#include "Firebase.h"
#include "UserDatabase.h"
#include <ArduinoJson.h>
#include <map>

// =============================================================================
// EXTERNAL REFERENCES
// =============================================================================

extern UserDatabase userDB;
extern bool firebaseInitialized;

// =============================================================================
// FIREBASE OBJECTS
// =============================================================================

UserAuth user_auth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD);
FirebaseApp app;
WiFiClientSecure ssl_client;
DefaultNetwork network;
AsyncClientClass aClient(ssl_client, getNetwork(network));
RealtimeDatabase Database;

// JSON tools
static object_t jsonData, obj1, obj2, obj3, obj4, obj5;
static JsonWriter writer;

// =============================================================================
// SYNC TRACKING
// =============================================================================

SyncState syncState = {
    .status = SYNC_IDLE,
    .lastError = "",
    .lastSyncTime = 0,
    .pendingCount = 0,
    .successCount = 0,
    .failCount = 0
};

// Track pending operations by their task ID
static std::map<String, bool> pendingOperations;
static std::map<String, bool> confirmedOperations;

// User change callback
static UserChangeCallback userChangeCallback = nullptr;

// Stream state
static bool userStreamActive = false;
static unsigned long lastStreamActivity = 0;

// =============================================================================
// INITIALIZATION
// =============================================================================

void initFirebase() {
    Serial.println(F("🔥 Initializing Firebase..."));
    
    ssl_client.setInsecure();
    ssl_client.setTimeout(1000);
    ssl_client.setHandshakeTimeout(5);
    
    initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(FIREBASE_DATABASE_URL);
    
    syncState.status = SYNC_IDLE;
    Serial.println(F("✓ Firebase initialized"));
}

bool isFirebaseReady() {
    return app.ready();
}

bool isFirebaseAuthenticated() {
    return app.ready() && app.isAuthenticated();
}

// =============================================================================
// ASYNC RESULT HANDLER
// =============================================================================

void processData(AsyncResult &aResult) {
    String tag = String(aResult.uid().c_str());
    
    // Handle events
    if (aResult.isEvent()) {
        #if DEBUG_FIREBASE
        Serial.printf("Event [%s]: %s (code: %d)\n", 
                      tag.c_str(),
                      aResult.eventLog().message().c_str(),
                      aResult.eventLog().code());
        #endif
    }
    
    // Handle errors
    if (aResult.isError()) {
        Serial.printf("❌ Firebase error [%s]: %s (code: %d)\n",
                      tag.c_str(),
                      aResult.error().message().c_str(),
                      aResult.error().code());
        
        syncState.lastError = aResult.error().message().c_str();
        syncState.failCount++;
        
        // Mark operation as failed
        if (pendingOperations.count(tag)) {
            pendingOperations.erase(tag);
        }
        
        // Handle attendance push failures
        if (tag.startsWith("Push_Attendance_")) {
            syncState.status = SYNC_FAILED;
        }
        
        return;
    }
    
    // Handle successful responses
    if (aResult.available()) {
        const char* payload = aResult.c_str();
        
        #if DEBUG_FIREBASE
        Serial.printf("Response [%s]: %s\n", tag.c_str(), payload);
        #endif
        
        // =========================================
        // Handle attendance push confirmation
        // =========================================
        if (tag.startsWith("Push_Attendance_")) {
            confirmedOperations[tag] = true;
            pendingOperations.erase(tag);
            syncState.successCount++;
            syncState.lastSyncTime = millis();
            syncState.status = SYNC_SUCCESS;
            
            Serial.print(F("✅ Attendance confirmed: "));
            Serial.println(tag);
            return;
        }
        
        // =========================================
        // Handle Get_Users response
        // =========================================
        if (tag == "Get_Users") {
            if (!payload || strcmp(payload, "null") == 0) {
                Serial.println(F("ℹ️ No users in Firebase"));
                firebaseInitialized = true;
                return;
            }
            
            DynamicJsonDocument doc(JSON_BUFFER_LARGE);
            DeserializationError err = deserializeJson(doc, payload);
            
            if (err) {
                Serial.printf("❌ JSON parse error (Get_Users): %s\n", err.c_str());
                return;
            }
            
            JsonObject usersObj = doc.as<JsonObject>();
            int count = 0;
            
            for (JsonPair kv : usersObj) {
                String uid = String(kv.key().c_str());
                uid.toUpperCase();
                String name = "";
                
                if (kv.value().is<JsonObject>()) {
                    JsonObject userObj = kv.value().as<JsonObject>();
                    if (userObj.containsKey("name")) {
                        name = userObj["name"].as<String>();
                    }
                    // Prefer inner uid if present
                    if (userObj.containsKey("uid")) {
                        uid = userObj["uid"].as<String>();
                        uid.toUpperCase();
                    }
                }
                
                if (name.length() > 0) {
                    userDB.registerUser(uid, name);
                    count++;
                }
            }
            
            Serial.printf("✅ Synced %d users from Firebase\n", count);
            userDB.saveToSPIFFS();
            userDB.printAllUsers();
            firebaseInitialized = true;
            return;
        }
        
        // =========================================
        // Handle Get_User_<UID> response
        // =========================================
        if (tag.startsWith("Get_User_")) {
            String uid = tag.substring(9);
            uid.toUpperCase();
            
            if (!payload || strcmp(payload, "null") == 0) {
                Serial.printf("ℹ️ User not found: %s\n", uid.c_str());
                return;
            }
            
            DynamicJsonDocument doc(JSON_BUFFER_SMALL);
            DeserializationError err = deserializeJson(doc, payload);
            
            if (err) {
                Serial.printf("❌ JSON parse error (Get_User): %s\n", err.c_str());
                return;
            }
            
            JsonObject obj = doc.as<JsonObject>();
            String name = obj.containsKey("name") ? obj["name"].as<String>() : "";
            
            if (name.length() > 0) {
                userDB.registerUser(uid, name);
                userDB.saveToSPIFFS();
                Serial.printf("✅ Registered user from Firebase: %s (%s)\n", 
                             name.c_str(), uid.c_str());
                
                if (userChangeCallback) {
                    userChangeCallback(uid, name, true);
                }
            }
            return;
        }
        
        // =========================================
        // Handle stream payloads (UserStream)
        // =========================================
        if (tag.startsWith("UserStream") || tag.startsWith("task_")) {
            lastStreamActivity = millis();
            userStreamActive = true;
            
            // Find JSON in stream payload
            const char* jsonStart = strchr(payload, '{');
            if (!jsonStart) return;
            
            DynamicJsonDocument doc(JSON_BUFFER_LARGE);
            DeserializationError err = deserializeJson(doc, jsonStart);
            
            if (err) {
                #if DEBUG_FIREBASE
                Serial.printf("JSON parse error (stream): %s\n", err.c_str());
                #endif
                return;
            }
            
            JsonObject root = doc.as<JsonObject>();
            
            if (root.containsKey("path") && root.containsKey("data")) {
                String path = root["path"].as<String>();
                JsonVariant data = root["data"];
                
                if (path == "/") {
                    // Full payload - iterate all users
                    if (data.is<JsonObject>()) {
                        for (JsonPair kv : data.as<JsonObject>()) {
                            String uid = String(kv.key().c_str());
                            uid.toUpperCase();
                            String name = "";
                            
                            JsonObject userObj = kv.value().as<JsonObject>();
                            if (!userObj.isNull()) {
                                if (userObj.containsKey("uid")) {
                                    uid = userObj["uid"].as<String>();
                                    uid.toUpperCase();
                                }
                                if (userObj.containsKey("name")) {
                                    name = userObj["name"].as<String>();
                                }
                            }
                            
                            if (name.length() > 0) {
                                userDB.registerUser(uid, name);
                                Serial.printf("📥 Stream: user %s (%s)\n", 
                                             name.c_str(), uid.c_str());
                                
                                if (userChangeCallback) {
                                    userChangeCallback(uid, name, true);
                                }
                            }
                        }
                        userDB.saveToSPIFFS();
                    }
                } else {
                    // Single user change - path like "/2048C51A"
                    String uid = path.substring(1);
                    uid.toUpperCase();
                    
                    if (data.isNull()) {
                        // User deleted
                        Serial.printf("📤 Stream: user removed %s\n", uid.c_str());
                        userDB.unregisterUser(uid);
                        userDB.saveToSPIFFS();
                        
                        if (userChangeCallback) {
                            userChangeCallback(uid, "", false);
                        }
                    } else {
                        // User added/modified
                        JsonObject userObj = data.as<JsonObject>();
                        String name = "";
                        
                        if (!userObj.isNull()) {
                            if (userObj.containsKey("uid")) {
                                uid = userObj["uid"].as<String>();
                                uid.toUpperCase();
                            }
                            if (userObj.containsKey("name")) {
                                name = userObj["name"].as<String>();
                            }
                        }
                        
                        if (name.length() > 0) {
                            userDB.registerUser(uid, name);
                            userDB.saveToSPIFFS();
                            Serial.printf("📥 Stream: registered %s (%s)\n", 
                                         name.c_str(), uid.c_str());
                            
                            if (userChangeCallback) {
                                userChangeCallback(uid, name, true);
                            }
                        }
                    }
                }
            }
            return;
        }
        
        // =========================================
        // Handle other confirmations
        // =========================================
        if (tag.startsWith("Set_Pending") || tag.startsWith("Set_User")) {
            Serial.printf("✅ Operation confirmed: %s\n", tag.c_str());
            return;
        }
    }
}

// =============================================================================
// ATTENDANCE FUNCTIONS
// =============================================================================

String sendToFirebase(String uid, String name, String timestamp,
                      String attendanceStatus, String registrationStatus) {
    
    if (!app.ready()) {
        Serial.println(F("⚠️ Firebase not ready"));
        return "";
    }
    
    // Generate unique sync ID
    String syncId = "Push_Attendance_" + String(millis());
    
    // Build JSON
    writer.create(obj1, "uid", uid);
    writer.create(obj2, "name", name);
    writer.create(obj3, "timestamp", timestamp);
    writer.create(obj4, "attendanceStatus", attendanceStatus);
    writer.create(obj5, "registrationStatus", registrationStatus);
    writer.join(jsonData, 5, obj1, obj2, obj3, obj4, obj5);
    
    // Track pending operation
    pendingOperations[syncId] = true;
    syncState.pendingCount++;
    syncState.status = SYNC_IN_PROGRESS;
    
    // Push to Firebase
    Database.push<object_t>(aClient, "/attendance", jsonData, processData, syncId.c_str());
    
    Serial.print(F("📤 Sending attendance: "));
    Serial.println(syncId);
    
    return syncId;
}

bool isSyncConfirmed(String syncId) {
    if (confirmedOperations.count(syncId)) {
        confirmedOperations.erase(syncId);  // Clean up after checking
        return true;
    }
    return false;
}

String getLastSyncError() {
    return syncState.lastError;
}

// =============================================================================
// USER MANAGEMENT
// =============================================================================

void sendPendingUser(String uid, String timestamp) {
    if (!app.ready()) {
        app.loop();
        delay(100);
        if (!app.ready()) return;
    }
    
    writer.create(obj1, "uid", uid);
    writer.create(obj2, "status", "pending");
    writer.create(obj3, "firstScannedAt", timestamp);
    writer.create(obj4, "lastScannedAt", timestamp);
    writer.join(jsonData, 4, obj1, obj2, obj3, obj4);
    
    String path = "/pendingUsers/" + uid;
    Database.set<object_t>(aClient, path.c_str(), jsonData, processData, "Set_Pending");
    
    Serial.printf("📤 Pending user sent: %s\n", uid.c_str());
}

void sendRegisteredUser(String uid, String name, String timestamp) {
    if (!app.ready()) return;
    
    writer.create(obj1, "name", name);
    writer.create(obj2, "status", "registered");
    writer.create(obj3, "registeredAt", timestamp);
    writer.create(obj4, "uid", uid);
    writer.join(jsonData, 4, obj1, obj2, obj3, obj4);
    
    String path = "/users/" + uid;
    Database.set<object_t>(aClient, path.c_str(), jsonData, processData, "Set_User");
    
    Serial.printf("📤 User registered: %s (%s)\n", name.c_str(), uid.c_str());
}

void fetchAllUsersFromFirebase() {
    int attempts = 0;
    while (!app.ready() && attempts < 50) {
        app.loop();
        delay(10);
        attempts++;
    }
    
    if (!app.ready()) {
        Serial.println(F("⚠️ Firebase not ready for user fetch"));
        return;
    }
    
    Database.get(aClient, "/users", processData, "Get_Users");
    Serial.println(F("📥 Requested users from Firebase"));
}

void fetchUserFromFirebase(String uid) {
    if (!app.ready()) return;
    
    uid.toUpperCase();
    String path = "/users/" + uid;
    String tag = "Get_User_" + uid;
    
    Database.get(aClient, path.c_str(), processData, tag.c_str());
    Serial.printf("📥 Requested user: %s\n", uid.c_str());
}

void streamUsers() {
    int attempts = 0;
    while (!app.ready() && attempts < 50) {
        app.loop();
        delay(10);
        attempts++;
    }
    
    if (!app.ready()) {
        Serial.println(F("⚠️ Firebase not ready for streaming"));
        return;
    }
    
    Database.get(aClient, "/users", processData, true, "UserStream");
    userStreamActive = true;
    lastStreamActivity = millis();
    Serial.println(F("✓ Streaming /users for realtime updates"));
}

void stopUserStream() {
    // Note: FirebaseClient doesn't have explicit stream stop
    // The stream will timeout naturally
    userStreamActive = false;
    Serial.println(F("🛑 User stream stopped"));
}

bool isUserStreamActive() {
    // Consider stream inactive if no activity for 60 seconds
    if (userStreamActive && (millis() - lastStreamActivity > 60000)) {
        userStreamActive = false;
    }
    return userStreamActive;
}

// =============================================================================
// SYNC STATE
// =============================================================================

SyncState getSyncState() {
    return syncState;
}

void resetSyncCounters() {
    syncState.successCount = 0;
    syncState.failCount = 0;
    syncState.pendingCount = 0;
    confirmedOperations.clear();
    pendingOperations.clear();
}

void setUserChangeCallback(UserChangeCallback callback) {
    userChangeCallback = callback;
}
