#include "Firebase.h"
#include "UserDatabase.h"
#include <ArduinoJson.h>

// Reference to main's firebaseInitialized flag so we can mark ready after sync
extern bool firebaseInitialized;

// Authentication
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// JSON tools
object_t jsonData, obj1, obj2, obj3, obj4, obj5;
JsonWriter writer;

/**
 * Initialize Firebase connection
 */
void initFirebase() {
    Serial.println(F("Initializing Firebase..."));
    
    // Configure SSL client
    ssl_client.setInsecure();
    ssl_client.setTimeout(1000);
    ssl_client.setHandshakeTimeout(5);

    // Initialize Firebase   
    initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
    
    Serial.println(F("Firebase initialized"));
}

/**
 * Process Firebase async results
 */
void processData(AsyncResult &aResult) {
    if (!aResult.isResult())
        return;

    if (aResult.isEvent())
    {
        Firebase.printf("Event task: %s, msg: %s, code: %d\n", 
                        aResult.uid().c_str(), 
                        aResult.eventLog().message().c_str(), 
                        aResult.eventLog().code());
    }

    // Handle stream payloads separately - they appear in available() not just isEvent()
    if (aResult.available())
    {
        String tag = String(aResult.uid().c_str());
        
        // Check if this is a stream payload (task name contains "task_" for streams)
        if (tag.startsWith("task_")) {
            const char* raw = aResult.c_str();
            Serial.print(F("DEBUG: Stream raw payload: "));
            Serial.println(raw);
            // The payload from the stream often comes as text like:
            // "event: put\ndata: {\"path\":\"/2048C51A\",\"data\":{...}}"
            // Find the first JSON object in the payload (look for '{')
            const char* jsonStart = strchr(raw, '{');
            if (jsonStart) {
                Serial.println(F("DEBUG: Found JSON start, parsing..."));
                DynamicJsonDocument doc(8192);
                DeserializationError err = deserializeJson(doc, jsonStart);
                if (err) {
                    Serial.print(F("JSON parse error (stream event): "));
                    Serial.println(err.c_str());
                } else {
                    JsonObject root = doc.as<JsonObject>();
                    Serial.println(F("DEBUG: JSON parsed successfully"));
                    // Expecting keys "path" and "data"
                    if (root.containsKey("path") && root.containsKey("data")) {
                        const char* path = root["path"].as<const char*>();
                        JsonVariant data = root["data"];
                        Serial.print(F("DEBUG: Path="));
                        Serial.print(path);
                        Serial.print(F(", data.isNull="));
                        Serial.println(data.isNull() ? "true" : "false");

                        // Only handle changes under /users (path may be "/" for full payload)
                        // When streaming /users, the path refers to the child path under /users
                        // e.g. path: "/2048C51A" or path: "/" for full replace
                        if (path) {
                            String spath = String(path);
                            if (spath == "/") {
                                // Full payload: data is an object mapping uid -> user object
                                if (data.is<JsonObject>()) {
                                    for (JsonPair kv : data.as<JsonObject>()) {
                                        // The child key may be a Firebase push-key; prefer the inner "uid" field when present
                                        String childKey = String(kv.key().c_str());
                                        String uid = childKey;
                                        String name = "";
                                        JsonObject childObj = kv.value().as<JsonObject>();
                                        if (!childObj.isNull()) {
                                            if (childObj.containsKey("uid")) {
                                                uid = String(childObj["uid"].as<const char*>());
                                            }
                                            if (childObj.containsKey("name")) {
                                                name = String(childObj["name"].as<const char*>());
                                            }
                                        }
                                        uid.toUpperCase();
                                        if (name.length() > 0) {
                                            userDB.registerUser(uid, name);
                                            Serial.print(F("Stream: registered user: "));
                                            Serial.print(name);
                                            Serial.print(F(" ("));
                                            Serial.print(uid);
                                            Serial.println(F(")"));
                                        }
                                    }
                                    userDB.saveToSPIFFS();  // Persist stream updates
                                }
                            } else {
                                // Single-child change: path like "/2048C51A"
                                String uid = spath.substring(1);
                                uid.toUpperCase();
                                if (data.isNull()) {
                                    // deletion
                                    Serial.print(F("Stream: user removed: "));
                                    Serial.println(uid);
                                    userDB.unregisterUser(uid);
                                    userDB.saveToSPIFFS();  // Persist deletion
                                } else {
                                    JsonObject uobj = data.as<JsonObject>();
                                    if (!uobj.isNull()) {
                                        String name = "";
                                        // Prefer inner uid if present
                                        if (uobj.containsKey("uid")) {
                                            uid = String(uobj["uid"].as<const char*>());
                                            uid.toUpperCase();
                                        }
                                        if (uobj.containsKey("name")) name = String(uobj["name"].as<const char*>());
                                        if (name.length() > 0) {
                                            userDB.registerUser(uid, name);
                                            userDB.saveToSPIFFS();  // Persist new user from stream
                                            Serial.print(F("Stream: registered user: "));
                                            Serial.print(name);
                                            Serial.print(F(" ("));
                                            Serial.print(uid);
                                            Serial.println(F(")"));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (aResult.isDebug())
        Firebase.printf("Debug task: %s, msg: %s\n", 
                        aResult.uid().c_str(), 
                        aResult.debug().c_str());

    if (aResult.isError())
        Firebase.printf("Error task: %s, msg: %s, code: %d\n", 
                        aResult.uid().c_str(), 
                        aResult.error().message().c_str(), 
                        aResult.error().code());

    if (aResult.available())
        Firebase.printf("task: %s, payload: %s\n", 
                        aResult.uid().c_str(), 
                        aResult.c_str());

    // Process available payloads
    if (aResult.available()) {
        // If this was the async response for fetching all users, parse and populate userDB
        String tag = String(aResult.uid().c_str());
        
        // Handle stream events (tag starts with "task_")
        if (tag.startsWith("task_")) {
            const char* raw = aResult.c_str();
            Serial.print(F("DEBUG: Processing stream payload for tag: "));
            Serial.println(tag);
            // Find the JSON in the stream payload
            const char* jsonStart = strchr(raw, '{');
            if (jsonStart) {
                DynamicJsonDocument doc(8192);
                DeserializationError err = deserializeJson(doc, jsonStart);
                if (!err) {
                    JsonObject root = doc.as<JsonObject>();
                    if (root.containsKey("path") && root.containsKey("data")) {
                        const char* path = root["path"].as<const char*>();
                        JsonVariant data = root["data"];
                        
                        if (path && String(path) == "/" && data.is<JsonObject>()) {
                            // Full /users payload
                            for (JsonPair kv : data.as<JsonObject>()) {
                                JsonObject childObj = kv.value().as<JsonObject>();
                                if (!childObj.isNull() && childObj.containsKey("uid") && childObj.containsKey("name")) {
                                    String uid = String(childObj["uid"].as<const char*>());
                                    String name = String(childObj["name"].as<const char*>());
                                    uid.toUpperCase();
                                    userDB.registerUser(uid, name);
                                    Serial.print(F("✓ Stream registered: "));
                                    Serial.print(name);
                                    Serial.print(F(" ("));
                                    Serial.print(uid);
                                    Serial.println(F(")"));
                                }
                            }
                        }
                    }
                }
            }
        }
        
            if (tag == "Get_Users") {
            // Parse JSON payload and populate userDB
            DynamicJsonDocument doc(8192);
            DeserializationError err = deserializeJson(doc, aResult.c_str());
            if (err) {
                Serial.print(F("JSON parse error (Get_Users): "));
                Serial.println(err.c_str());
            } else {
                JsonObject usersObj = doc.as<JsonObject>();
                for (JsonPair kv : usersObj) {
                    String uid = String(kv.key().c_str());
                    String name = "";
                    if (kv.value().is<JsonObject>() && kv.value().as<JsonObject>().containsKey("name")) {
                        name = String(kv.value()["name"].as<const char*>());
                    }
                    uid.toUpperCase();
                    userDB.registerUser(uid, name);
                }
                Serial.println(F("Synced users from Firebase (async)"));
                // Save to SPIFFS for offline use
                userDB.saveToSPIFFS();
                // Show the users we just registered and mark Firebase as initialized
                userDB.printAllUsers();
                firebaseInitialized = true;
            }
        }        // Handle per-user async responses: tags like "Get_User_<UID>"
        else if (tag.startsWith("Get_User_")) {
            // Extract UID from tag
            String uid = tag.substring(strlen("Get_User_"));
            uid.toUpperCase();

            // aResult.c_str() may be "null" if user not found
            const char* payload = aResult.c_str();
            if (!payload || strcmp(payload, "null") == 0) {
                Serial.print(F("User not found on server: "));
                Serial.println(uid);
                return;
            }

            DynamicJsonDocument doc(1024);
            DeserializationError err = deserializeJson(doc, payload);
            if (err) {
                Serial.print(F("JSON parse error (Get_User): "));
                Serial.println(err.c_str());
                return;
            }

            JsonObject obj = doc.as<JsonObject>();
            String name = "";
            if (obj.containsKey("name")) {
                name = String(obj["name"].as<const char*>());
            }

                if (name.length() > 0) {
                    userDB.registerUser(uid, name);
                    userDB.saveToSPIFFS();  // Persist new user
                    Serial.print(F("Registered user from Firebase: "));
                    Serial.print(name);
                    Serial.print(F(" ("));
                    Serial.print(uid);
                    Serial.println(F(")"));
                } else {
                    Serial.print(F("User object fetched but no name field: "));
                    Serial.println(uid);
                }
        }
    }
}/**
 * Send attendance data to Firebase
 * Matches Flutter Attendance model structure
 * @param uid - RFID card UID
 * @param name - Person's name (empty for unregistered)
 * @param timestamp - Formatted timestamp string (ISO 8601 format)
 * @param attendanceStatus - "present", "late", "absent", etc.
 * @param registrationStatus - "registered" or "unregistered"
 */
void sendToFirebase(String uid, String name, String timestamp, String attendanceStatus, String registrationStatus) {
    // Wait for Firebase to be ready
    while (!app.ready()) {
        Serial.println(F("Firebase not ready, waiting..."));
        app.loop();
        delay(100);
    }

    // Build JSON matching Flutter model
    object_t obj3, obj4, obj5;
    
    writer.create(obj1, "uid", uid);
    writer.create(obj2, "name", name);
    writer.create(obj3, "timestamp", timestamp);
    writer.create(obj4, "attendanceStatus", attendanceStatus);
    writer.create(obj5, "registrationStatus", registrationStatus);
    
    writer.join(jsonData, 5, obj1, obj2, obj3, obj4, obj5);

    // Push JSON to Firebase
    Database.push<object_t>(aClient, "/attendance", jsonData, processData, "Push_Data");

    Serial.println(F("Attendance sent to Firebase"));
}

/**
 * Send pending user to Firebase for registration
 * Creates/updates entry in /pendingUsers/{uid}
 * @param uid - RFID card UID
 * @param timestamp - ISO 8601 timestamp
 */
void sendPendingUser(String uid, String timestamp) {
    while (!app.ready()) {
        Serial.println(F("Firebase not ready, waiting..."));
        app.loop();
        delay(100);
    }

    object_t obj3, obj4;
    
    writer.create(obj1, "uid", uid);
    writer.create(obj2, "status", "pending");
    writer.create(obj3, "firstScannedAt", timestamp);
    writer.create(obj4, "lastScannedAt", timestamp);
    
    writer.join(jsonData, 4, obj1, obj2, obj3, obj4);

    // Use set instead of push to avoid duplicates (uid as key)
    String path = "/pendingUsers/" + uid;
    Database.set<object_t>(aClient, path.c_str(), jsonData, processData, "Set_Pending");

    Serial.print(F("Pending user sent: "));
    Serial.println(uid);
}

/**
 * Send registered user to Firebase
 * Creates entry in /users/{uid}
 * @param uid - RFID card UID
 * @param name - User's name
 * @param timestamp - ISO 8601 timestamp of registration
 */
void sendRegisteredUser(String uid, String name, String timestamp) {
    while (!app.ready()) {
        Serial.println(F("Firebase not ready, waiting..."));
        app.loop();
        delay(100);
    }

    object_t obj3, obj4;
    
    writer.create(obj1, "name", name);
    writer.create(obj2, "status", "registered");
    writer.create(obj3, "registeredAt", timestamp);
    writer.create(obj4, "uid", uid);
    
    writer.join(jsonData, 4, obj1, obj2, obj3, obj4);

    String path = "/users/" + uid;
    Database.set<object_t>(aClient, path.c_str(), jsonData, processData, "Set_User");

    Serial.print(F("User registered: "));
    Serial.print(name);
    Serial.print(F(" ("));
    Serial.print(uid);
    Serial.println(F(")"));
}

void fetchAllUsersFromFirebase() {
    while (!app.ready()) { app.loop(); delay(10); }
    // Request the /users node asynchronously; the result will arrive in processData
    // with the task/tag "Get_Users" and the JSON payload.
    Database.get(aClient, "/users", processData, "Get_Users");
    Serial.println(F("Requested users from Firebase (async)"));
}

/**
 * Fetch a single user from Firebase (/users/{uid}) asynchronously.
 * The response will be handled in processData() with tag "Get_User_<UID>".
 */
void fetchUserFromFirebase(String uid) {
    while (!app.ready()) { app.loop(); delay(10); }
    String u = uid;
    u.toUpperCase();
    String path = "/users/" + u;
    String tag = "Get_User_" + u;
    Database.get(aClient, path.c_str(), processData, tag.c_str());
    Serial.print(F("Requested user from Firebase: "));
    Serial.println(u);
}

/**
 * Start streaming /users to detect realtime changes.
 * When a pending user is approved and added to /users, 
 * the ESP will receive the event and register them automatically.
 */
void streamUsers() {
    while (!app.ready()) { app.loop(); delay(10); }
    Database.get(aClient, "/users", processData, true, "UserStream");
    Serial.println(F("✓ Streaming /users for realtime updates"));
}

