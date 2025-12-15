/*
 * TapTrack - User Database
 * Local cache of registered users with SPIFFS persistence
 */

#ifndef USER_DATABASE_H
#define USER_DATABASE_H

#include <Arduino.h>
#include <map>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"

// =============================================================================
// USER INFO STRUCTURE
// =============================================================================

struct UserInfo {
    String name;
    bool isRegistered;
    unsigned long lastSeen;  // Last tap timestamp
    int tapCount;            // Total taps
};

// =============================================================================
// USER DATABASE CLASS
// =============================================================================

class UserDatabase {
private:
    std::map<String, UserInfo> users;
    bool spiffsInitialized = false;
    bool dirty = false;  // Track if changes need to be saved
    
public:
    UserDatabase() {}
    
    /**
     * Initialize database (call after SPIFFS is mounted)
     */
    bool init() {
        if (spiffsInitialized) return true;
        spiffsInitialized = true;
        return loadFromSPIFFS();
    }
    
    /**
     * Register or update a user
     */
    void registerUser(String uid, String name) {
        uid.toUpperCase();
        
        UserInfo info;
        
        // Preserve existing data if updating
        if (users.count(uid)) {
            info = users[uid];
            info.name = name;
        } else {
            info.name = name;
            info.isRegistered = true;
            info.lastSeen = 0;
            info.tapCount = 0;
        }
        
        info.isRegistered = true;
        users[uid] = info;
        dirty = true;
        
        Serial.printf("✓ Registered: %s (%s)\n", name.c_str(), uid.c_str());
    }
    
    /**
     * Check if UID is registered
     */
    bool isRegistered(String uid) {
        uid.toUpperCase();
        return users.count(uid) > 0 && users[uid].isRegistered;
    }
    
    /**
     * Get user name
     */
    String getName(String uid) {
        uid.toUpperCase();
        if (isRegistered(uid)) {
            return users[uid].name;
        }
        return "";
    }
    
    /**
     * Get full user info
     */
    UserInfo getUserInfo(String uid) {
        uid.toUpperCase();
        if (users.count(uid)) {
            return users[uid];
        }
        
        UserInfo empty;
        empty.name = "";
        empty.isRegistered = false;
        empty.lastSeen = 0;
        empty.tapCount = 0;
        return empty;
    }
    
    /**
     * Update last seen and tap count
     */
    void recordTap(String uid) {
        uid.toUpperCase();
        if (users.count(uid)) {
            users[uid].lastSeen = millis();
            users[uid].tapCount++;
            dirty = true;
        }
    }
    
    /**
     * Get user count
     */
    int getUserCount() {
        return users.size();
    }
    
    /**
     * Remove a user
     */
    void unregisterUser(String uid) {
        uid.toUpperCase();
        if (users.count(uid)) {
            users.erase(uid);
            dirty = true;
            Serial.printf("🗑️ Unregistered: %s\n", uid.c_str());
        }
    }
    
    /**
     * Clear all users (but keep the file)
     */
    void clearAll() {
        users.clear();
        dirty = true;
        Serial.println(F("🗑️ All users cleared"));
    }
    
    /**
     * Print all registered users
     */
    void printAllUsers() {
        Serial.println(F("\n=== Registered Users ==="));
        
        if (users.empty()) {
            Serial.println(F("No users registered"));
        } else {
            int i = 1;
            for (const auto& pair : users) {
                Serial.printf("%d. %s (%s)\n", 
                             i++, 
                             pair.second.name.c_str(), 
                             pair.first.c_str());
            }
        }
        
        Serial.printf("Total: %d users\n", users.size());
        Serial.println(F("========================\n"));
    }
    
    /**
     * Get all UIDs as a vector
     */
    std::vector<String> getAllUIDs() {
        std::vector<String> uids;
        for (const auto& pair : users) {
            uids.push_back(pair.first);
        }
        return uids;
    }
    
    /**
     * Save to SPIFFS
     */
    bool saveToSPIFFS() {
        if (!spiffsInitialized) return false;
        
        File file = SPIFFS.open(USER_DB_FILE_PATH, FILE_WRITE);
        if (!file) {
            Serial.println(F("❌ Failed to open user DB for writing"));
            return false;
        }
        
        DynamicJsonDocument doc(JSON_BUFFER_LARGE);
        JsonObject root = doc.to<JsonObject>();
        
        for (const auto& pair : users) {
            JsonObject userObj = root.createNestedObject(pair.first);
            userObj["name"] = pair.second.name;
            userObj["isRegistered"] = pair.second.isRegistered;
            userObj["lastSeen"] = pair.second.lastSeen;
            userObj["tapCount"] = pair.second.tapCount;
        }
        
        size_t written = serializeJson(doc, file);
        file.close();
        
        if (written == 0) {
            Serial.println(F("❌ Failed to write user DB"));
            return false;
        }
        
        dirty = false;
        Serial.printf("💾 Saved %d users to SPIFFS\n", users.size());
        return true;
    }
    
    /**
     * Load from SPIFFS
     */
    bool loadFromSPIFFS() {
        if (!spiffsInitialized) return false;
        
        if (!SPIFFS.exists(USER_DB_FILE_PATH)) {
            Serial.println(F("📂 No cached user DB found"));
            return false;
        }
        
        File file = SPIFFS.open(USER_DB_FILE_PATH, FILE_READ);
        if (!file) {
            Serial.println(F("❌ Failed to open user DB"));
            return false;
        }
        
        DynamicJsonDocument doc(JSON_BUFFER_LARGE);
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        
        if (err) {
            Serial.printf("❌ User DB parse error: %s\n", err.c_str());
            return false;
        }
        
        users.clear();
        JsonObject root = doc.as<JsonObject>();
        
        for (JsonPair kv : root) {
            String uid = String(kv.key().c_str());
            uid.toUpperCase();
            
            JsonObject userObj = kv.value().as<JsonObject>();
            
            UserInfo info;
            info.name = userObj["name"] | "";
            info.isRegistered = userObj["isRegistered"] | true;
            info.lastSeen = userObj["lastSeen"] | 0;
            info.tapCount = userObj["tapCount"] | 0;
            
            users[uid] = info;
        }
        
        dirty = false;
        Serial.printf("📂 Loaded %d users from cache\n", users.size());
        return true;
    }
    
    /**
     * Save if dirty
     */
    bool saveIfNeeded() {
        if (dirty) {
            return saveToSPIFFS();
        }
        return true;
    }
    
    /**
     * Delete cache file
     */
    void clearCache() {
        if (spiffsInitialized && SPIFFS.exists(USER_DB_FILE_PATH)) {
            SPIFFS.remove(USER_DB_FILE_PATH);
        }
        users.clear();
        dirty = false;
        Serial.println(F("🗑️ User cache cleared"));
    }
    
    /**
     * Check if database has unsaved changes
     */
    bool isDirty() {
        return dirty;
    }
};

// Global instance declaration
extern UserDatabase userDB;

#endif // USER_DATABASE_H
