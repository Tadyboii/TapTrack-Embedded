#ifndef ATTENDANCE_QUEUE_H
#define ATTENDANCE_QUEUE_H

#include <Arduino.h>
#include <vector>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Structure for attendance record
struct AttendanceRecord {
    String uid;
    String name;
    String timestamp;
    String attendanceStatus;
    String registrationStatus;
};

class AttendanceQueue {
private:
    std::vector<AttendanceRecord> queue;
    const char* QUEUE_FILE = "/attendance_queue.json";
    const int MAX_QUEUE_SIZE = 100;  // Maximum records to store offline
    bool initialized = false;
    
public:
    AttendanceQueue() {
        // Don't init SPIFFS here - will be done in init()
    }
    
    /**
     * Initialize queue (call after SPIFFS is mounted)
     */
    bool init() {
        if (initialized) return true;
        initialized = true;
        // Load pending records from storage
        loadFromSPIFFS();
        return true;
    }
    
    /**
     * Add attendance record to queue
     */
    bool enqueue(String uid, String name, String timestamp, 
                 String attendanceStatus, String registrationStatus) {
        
        if (queue.size() >= MAX_QUEUE_SIZE) {
            Serial.println(F("⚠️ Attendance queue full!"));
            return false;
        }
        
        AttendanceRecord record;
        record.uid = uid;
        record.name = name;
        record.timestamp = timestamp;
        record.attendanceStatus = attendanceStatus;
        record.registrationStatus = registrationStatus;
        
        queue.push_back(record);
        
        Serial.print(F("📝 Queued attendance: "));
        Serial.print(name.length() > 0 ? name : "Unknown");
        Serial.print(F(" (Queue size: "));
        Serial.print(queue.size());
        Serial.println(F(")"));
        
        // Save to persistent storage
        saveToSPIFFS();
        
        return true;
    }
    
    /**
     * Get next record from queue without removing
     */
    AttendanceRecord* peek() {
        if (queue.empty()) {
            return nullptr;
        }
        return &queue[0];
    }
    
    /**
     * Remove first record from queue
     */
    bool dequeue() {
        if (queue.empty()) {
            return false;
        }
        
        queue.erase(queue.begin());
        saveToSPIFFS();
        return true;
    }
    
    /**
     * Check if queue is empty
     */
    bool isEmpty() {
        return queue.empty();
    }
    
    /**
     * Get queue size
     */
    int size() {
        return queue.size();
    }
    
    /**
     * Clear all records
     */
    void clear() {
        queue.clear();
        SPIFFS.remove(QUEUE_FILE);
        Serial.println(F("Attendance queue cleared"));
    }
    
    /**
     * Save queue to SPIFFS
     */
    bool saveToSPIFFS() {
        File file = SPIFFS.open(QUEUE_FILE, FILE_WRITE);
        if (!file) {
            Serial.println(F("Failed to open queue file for writing"));
            return false;
        }
        
        // Create JSON array
        DynamicJsonDocument doc(8192);  // Larger buffer for multiple records
        JsonArray array = doc.to<JsonArray>();
        
        // Add all records to array
        for (const auto& record : queue) {
            JsonObject obj = array.createNestedObject();
            obj["uid"] = record.uid;
            obj["name"] = record.name;
            obj["timestamp"] = record.timestamp;
            obj["attendanceStatus"] = record.attendanceStatus;
            obj["registrationStatus"] = record.registrationStatus;
        }
        
        // Serialize to file
        if (serializeJson(doc, file) == 0) {
            Serial.println(F("Failed to write to queue file"));
            file.close();
            return false;
        }
        
        file.close();
        return true;
    }
    
    /**
     * Load queue from SPIFFS
     */
    bool loadFromSPIFFS() {
        if (!SPIFFS.exists(QUEUE_FILE)) {
            Serial.println(F("Queue file does not exist"));
            return false;
        }
        
        File file = SPIFFS.open(QUEUE_FILE, FILE_READ);
        if (!file) {
            Serial.println(F("Failed to open queue file for reading"));
            return false;
        }
        
        // Parse JSON document
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) {
            Serial.print(F("Failed to parse queue file: "));
            Serial.println(error.c_str());
            return false;
        }
        
        // Clear existing queue
        queue.clear();
        
        // Load records from JSON array
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject obj : array) {
            AttendanceRecord record;
            record.uid = obj["uid"].as<String>();
            record.name = obj["name"].as<String>();
            record.timestamp = obj["timestamp"].as<String>();
            record.attendanceStatus = obj["attendanceStatus"].as<String>();
            record.registrationStatus = obj["registrationStatus"].as<String>();
            
            queue.push_back(record);
        }
        
        if (queue.size() > 0) {
            Serial.print(F("📂 Loaded "));
            Serial.print(queue.size());
            Serial.println(F(" pending attendance records from SPIFFS"));
        }
        
        return true;
    }
    
    /**
     * Print queue contents
     */
    void printQueue() {
        Serial.println(F("\n=== Attendance Queue ==="));
        if (queue.empty()) {
            Serial.println(F("Queue is empty"));
        } else {
            Serial.print(F("Total records: "));
            Serial.println(queue.size());
            Serial.println(F("------------------------"));
            for (size_t i = 0; i < queue.size() && i < 5; i++) {  // Show first 5
                Serial.print(i + 1);
                Serial.print(F(". "));
                Serial.print(queue[i].name.length() > 0 ? queue[i].name : "Unknown");
                Serial.print(F(" - "));
                Serial.println(queue[i].timestamp);
            }
            if (queue.size() > 5) {
                Serial.print(F("... and "));
                Serial.print(queue.size() - 5);
                Serial.println(F(" more"));
            }
        }
        Serial.println(F("========================\n"));
    }
};

#endif