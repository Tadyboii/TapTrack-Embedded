/*
 * TapTrack - Attendance Queue
 * Offline storage with confirmation-based sync
 */

#ifndef ATTENDANCE_QUEUE_H
#define ATTENDANCE_QUEUE_H

#include <Arduino.h>
#include <vector>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"

// =============================================================================
// ATTENDANCE RECORD
// =============================================================================

struct AttendanceRecord {
    String uid;
    String name;
    String timestamp;
    String attendanceStatus;
    String registrationStatus;
    String syncId;          // Tracking ID for Firebase sync
    int retryCount;         // Number of sync attempts
    unsigned long queuedAt; // When record was queued
};

// =============================================================================
// ATTENDANCE QUEUE CLASS
// =============================================================================

class AttendanceQueue {
private:
    std::vector<AttendanceRecord> queue;
    bool initialized = false;
    
public:
    AttendanceQueue() {}
    
    /**
     * Initialize queue (call after SPIFFS is mounted)
     */
    bool init() {
        if (initialized) return true;
        initialized = true;
        loadFromSPIFFS();
        return true;
    }
    
    /**
     * Add attendance record to queue
     * @return true if added successfully
     */
    bool enqueue(String uid, String name, String timestamp,
                 String attendanceStatus, String registrationStatus) {
        
        if (queue.size() >= MAX_QUEUE_SIZE) {
            Serial.println(F("⚠️ Queue full! Cannot add more records."));
            return false;
        }
        
        AttendanceRecord record;
        record.uid = uid;
        record.name = name;
        record.timestamp = timestamp;
        record.attendanceStatus = attendanceStatus;
        record.registrationStatus = registrationStatus;
        record.syncId = "";
        record.retryCount = 0;
        record.queuedAt = millis();
        
        queue.push_back(record);
        
        Serial.printf("📝 Queued: %s (Queue: %d/%d)\n",
                     name.length() > 0 ? name.c_str() : uid.c_str(),
                     queue.size(), MAX_QUEUE_SIZE);
        
        // Check if approaching capacity
        if (queue.size() >= QUEUE_WARNING_THRESHOLD) {
            Serial.printf("⚠️ Queue at %d%% capacity!\n", 
                         (queue.size() * 100) / MAX_QUEUE_SIZE);
        }
        
        saveToSPIFFS();
        return true;
    }
    
    /**
     * Get pointer to first record (for processing)
     */
    AttendanceRecord* peek() {
        if (queue.empty()) return nullptr;
        return &queue[0];
    }
    
    /**
     * Get record at index
     */
    AttendanceRecord* getAt(int index) {
        if (index < 0 || index >= queue.size()) return nullptr;
        return &queue[index];
    }
    
    /**
     * Update sync ID for first record
     */
    void setSyncId(String syncId) {
        if (!queue.empty()) {
            queue[0].syncId = syncId;
            queue[0].retryCount++;
            saveToSPIFFS();
        }
    }
    
    /**
     * Remove first record (call after confirmed sync)
     */
    bool dequeue() {
        if (queue.empty()) return false;
        
        String name = queue[0].name.length() > 0 ? queue[0].name : queue[0].uid;
        queue.erase(queue.begin());
        
        Serial.printf("✅ Dequeued: %s (Remaining: %d)\n", 
                     name.c_str(), queue.size());
        
        saveToSPIFFS();
        return true;
    }
    
    /**
     * Remove record by sync ID (for confirmation-based dequeue)
     */
    bool dequeueBySyncId(String syncId) {
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->syncId == syncId) {
                String name = it->name.length() > 0 ? it->name : it->uid;
                queue.erase(it);
                
                Serial.printf("✅ Confirmed & dequeued: %s\n", name.c_str());
                saveToSPIFFS();
                return true;
            }
        }
        return false;
    }
    
    /**
     * Move failed record to end of queue
     */
    void moveToBack() {
        if (queue.size() > 1) {
            AttendanceRecord record = queue[0];
            queue.erase(queue.begin());
            queue.push_back(record);
            saveToSPIFFS();
        }
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
     * Check if queue is at capacity
     */
    bool isFull() {
        return queue.size() >= MAX_QUEUE_SIZE;
    }
    
    /**
     * Get capacity percentage
     */
    int getCapacityPercent() {
        return (queue.size() * 100) / MAX_QUEUE_SIZE;
    }
    
    /**
     * Clear all records
     */
    void clear() {
        queue.clear();
        if (initialized) {
            SPIFFS.remove(QUEUE_FILE_PATH);
        }
        Serial.println(F("🗑️ Queue cleared"));
    }
    
    /**
     * Get total retry count for current record
     */
    int getCurrentRetryCount() {
        if (queue.empty()) return 0;
        return queue[0].retryCount;
    }
    
    /**
     * Save queue to SPIFFS
     */
    bool saveToSPIFFS() {
        if (!initialized) return false;
        
        File file = SPIFFS.open(QUEUE_FILE_PATH, FILE_WRITE);
        if (!file) {
            Serial.println(F("❌ Failed to open queue file"));
            return false;
        }
        
        DynamicJsonDocument doc(JSON_BUFFER_LARGE);
        JsonArray array = doc.to<JsonArray>();
        
        for (const auto& record : queue) {
            JsonObject obj = array.createNestedObject();
            obj["uid"] = record.uid;
            obj["name"] = record.name;
            obj["timestamp"] = record.timestamp;
            obj["attendanceStatus"] = record.attendanceStatus;
            obj["registrationStatus"] = record.registrationStatus;
            obj["syncId"] = record.syncId;
            obj["retryCount"] = record.retryCount;
            obj["queuedAt"] = record.queuedAt;
        }
        
        size_t written = serializeJson(doc, file);
        file.close();
        
        return written > 0;
    }
    
    /**
     * Load queue from SPIFFS
     */
    bool loadFromSPIFFS() {
        if (!initialized) return false;
        
        if (!SPIFFS.exists(QUEUE_FILE_PATH)) {
            return false;
        }
        
        File file = SPIFFS.open(QUEUE_FILE_PATH, FILE_READ);
        if (!file) {
            Serial.println(F("❌ Failed to open queue file"));
            return false;
        }
        
        DynamicJsonDocument doc(JSON_BUFFER_LARGE);
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        
        if (err) {
            Serial.printf("❌ Queue parse error: %s\n", err.c_str());
            return false;
        }
        
        queue.clear();
        JsonArray array = doc.as<JsonArray>();
        
        for (JsonObject obj : array) {
            AttendanceRecord record;
            record.uid = obj["uid"] | "";
            record.name = obj["name"] | "";
            record.timestamp = obj["timestamp"] | "";
            record.attendanceStatus = obj["attendanceStatus"] | "present";
            record.registrationStatus = obj["registrationStatus"] | "registered";
            record.syncId = obj["syncId"] | "";
            record.retryCount = obj["retryCount"] | 0;
            record.queuedAt = obj["queuedAt"] | 0;
            
            queue.push_back(record);
        }
        
        if (!queue.empty()) {
            Serial.printf("📂 Loaded %d queued records\n", queue.size());
        }
        
        return true;
    }
    
    /**
     * Print queue summary
     */
    void printQueue() {
        Serial.println(F("\n=== Attendance Queue ==="));
        
        if (queue.empty()) {
            Serial.println(F("Queue is empty"));
        } else {
            Serial.printf("Total: %d/%d records\n", queue.size(), MAX_QUEUE_SIZE);
            Serial.println(F("------------------------"));
            
            int shown = 0;
            for (const auto& record : queue) {
                if (shown >= 5) {
                    Serial.printf("... and %d more\n", queue.size() - 5);
                    break;
                }
                
                Serial.printf("%d. %s - %s [%s]\n",
                             shown + 1,
                             record.name.length() > 0 ? record.name.c_str() : record.uid.c_str(),
                             record.timestamp.c_str(),
                             record.syncId.length() > 0 ? "pending" : "queued");
                shown++;
            }
        }
        
        Serial.println(F("========================\n"));
    }
    
    /**
     * Get statistics
     */
    void getStats(int& total, int& pending, int& failed) {
        total = queue.size();
        pending = 0;
        failed = 0;
        
        for (const auto& record : queue) {
            if (record.syncId.length() > 0) {
                pending++;
            }
            if (record.retryCount > 3) {
                failed++;
            }
        }
    }
};

// Global instance declaration
extern AttendanceQueue attendanceQueue;

#endif // ATTENDANCE_QUEUE_H
