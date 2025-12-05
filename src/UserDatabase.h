#ifndef USER_DATABASE_H
#define USER_DATABASE_H

#include <Arduino.h>
#include <map>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Structure to hold user information
struct UserInfo {
    String name;
    bool isRegistered;
};

class UserDatabase {
private:
    // Map of UID to UserInfo (cached in RAM, persisted to SPIFFS)
    std::map<String, UserInfo> users;
    const char* USER_DB_FILE = "/user_database.json";
    bool spiffsInitialized = false;
    
public:
    UserDatabase() {
        // SPIFFS init will be called explicitly
    }
    
    /**
     * Initialize user database (call after SPIFFS is mounted)
     */
    bool init() {
        if (spiffsInitialized) return true;
        spiffsInitialized = true;
        loadFromSPIFFS();
        return true;
    }
    
    /**
     * Register a new user
     */
    void registerUser(String uid, String name) {
        UserInfo info;
        info.name = name;
        info.isRegistered = true;
        users[uid] = info;
        
        Serial.print(F("✓ Registered user: "));
        Serial.print(name);
        Serial.print(F(" (UID: "));
        Serial.print(uid);
        Serial.println(F(")"));
    }
    
    /**
     * Check if UID is registered
     */
    bool isRegistered(String uid) {
        return users.find(uid) != users.end();
    }
    
    /**
     * Get user name from UID
     * Returns empty string if not found
     */
    String getName(String uid) {
        if (isRegistered(uid)) {
            return users[uid].name;
        }
        return "";
    }
    
    /**
     * Get user info from UID
     */
    UserInfo getUserInfo(String uid) {
        if (isRegistered(uid)) {
            return users[uid];
        }
        
        // Return unregistered user info
        UserInfo info;
        info.name = "";
        info.isRegistered = false;
        return info;
    }
    
    /**
     * Get total number of registered users
     */
    int getUserCount() {
        return users.size();
    }
    
    /**
     * Remove a user
     */
    void unregisterUser(String uid) {
        users.erase(uid);
        Serial.print(F("Unregistered UID: "));
        Serial.println(uid);
    }
    
    /**
     * Print all registered users
     */
    void printAllUsers() {
        Serial.println(F("\n=== Registered Users ==="));
        if (users.empty()) {
            Serial.println(F("No users registered"));
        } else {
            for (auto const& pair : users) {
                Serial.print(F("UID: "));
                Serial.print(pair.first);
                Serial.print(F(" -> Name: "));
                Serial.println(pair.second.name);
            }
        }
        Serial.print(F("Total: "));
        Serial.println(users.size());
        Serial.println(F("========================\n"));
    }
    
    /**
     * Save user database to SPIFFS
     */
    bool saveToSPIFFS() {
        if (!spiffsInitialized) return false;
        
        File file = SPIFFS.open(USER_DB_FILE, FILE_WRITE);
        if (!file) {
            Serial.println(F("❌ Failed to open user DB file for writing"));
            return false;
        }
        
        DynamicJsonDocument doc(8192);
        JsonObject root = doc.to<JsonObject>();
        
        for (const auto& pair : users) {
            JsonObject userObj = root.createNestedObject(pair.first);
            userObj["name"] = pair.second.name;
            userObj["isRegistered"] = pair.second.isRegistered;
        }
        
        if (serializeJson(doc, file) == 0) {
            Serial.println(F("❌ Failed to write user DB"));
            file.close();
            return false;
        }
        
        file.close();
        Serial.print(F("💾 Saved "));
        Serial.print(users.size());
        Serial.println(F(" users to SPIFFS"));
        return true;
    }
    
    /**
     * Load user database from SPIFFS
     */
    bool loadFromSPIFFS() {
        if (!spiffsInitialized) return false;
        
        if (!SPIFFS.exists(USER_DB_FILE)) {
            Serial.println(F("📂 No cached user DB found"));
            return false;
        }
        
        File file = SPIFFS.open(USER_DB_FILE, FILE_READ);
        if (!file) {
            Serial.println(F("❌ Failed to open user DB file"));
            return false;
        }
        
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) {
            Serial.print(F("❌ User DB parse error: "));
            Serial.println(error.c_str());
            return false;
        }
        
        users.clear();
        JsonObject root = doc.as<JsonObject>();
        
        for (JsonPair kv : root) {
            String uid = String(kv.key().c_str());
            JsonObject userObj = kv.value().as<JsonObject>();
            
            UserInfo info;
            info.name = userObj["name"].as<String>();
            info.isRegistered = userObj["isRegistered"] | true;
            users[uid] = info;
        }
        
        Serial.print(F("📂 Loaded "));
        Serial.print(users.size());
        Serial.println(F(" cached users from SPIFFS"));
        return true;
    }
    
    /**
     * Clear cached user database
     */
    void clearCache() {
        if (spiffsInitialized && SPIFFS.exists(USER_DB_FILE)) {
            SPIFFS.remove(USER_DB_FILE);
        }
        users.clear();
        Serial.println(F("🗑️ User cache cleared"));
    }
};

#ifdef __cplusplus
// Declare the global instance defined in main.cpp so other translation units can use it
extern UserDatabase userDB;
#endif

#endif