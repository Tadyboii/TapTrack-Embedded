/*
 * TapTrack - Firebase Module
 * Real-time database integration with bidirectional sync
 */

#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>
#include <WiFi.h>

// Required before FirebaseClient.h
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include "config.h"
#include "secrets.h"

// =============================================================================
// SYNC STATUS
// =============================================================================

typedef enum {
    SYNC_IDLE,
    SYNC_PENDING,
    SYNC_IN_PROGRESS,
    SYNC_SUCCESS,
    SYNC_FAILED
} SyncStatus;

typedef struct {
    SyncStatus status;
    String lastError;
    unsigned long lastSyncTime;
    int pendingCount;
    int successCount;
    int failCount;
} SyncState;

// =============================================================================
// EXTERNAL DECLARATIONS
// =============================================================================

extern UserAuth user_auth;
extern FirebaseApp app;
extern WiFiClientSecure ssl_client;
extern DefaultNetwork network;
extern AsyncClientClass aClient;
extern RealtimeDatabase Database;

extern SyncState syncState;

// =============================================================================
// CORE FUNCTIONS
// =============================================================================

/**
 * Initialize Firebase connection
 */
void initFirebase();

/**
 * Process Firebase async results (call in loop)
 */
void processData(AsyncResult &aResult);

/**
 * Check if Firebase is ready
 */
bool isFirebaseReady();

/**
 * Check if Firebase is authenticated
 */
bool isFirebaseAuthenticated();

// =============================================================================
// ATTENDANCE FUNCTIONS
// =============================================================================

/**
 * Send attendance to Firebase
 * @return Sync ID for tracking (empty on immediate failure)
 */
String sendToFirebase(String uid, String name, String timestamp, 
                      String attendanceStatus, String registrationStatus);

/**
 * Check if a specific sync completed successfully
 * @param syncId - ID returned from sendToFirebase
 * @return true if confirmed, false if pending or failed
 */
bool isSyncConfirmed(String syncId);

/**
 * Get last sync error message
 */
String getLastSyncError();

// =============================================================================
// USER MANAGEMENT
// =============================================================================

/**
 * Send pending user (unregistered card tap)
 */
void sendPendingUser(String uid, String timestamp);

/**
 * Send registered user to Firebase
 */
void sendRegisteredUser(String uid, String name, String timestamp);

/**
 * Fetch all users from Firebase
 */
void fetchAllUsersFromFirebase();

/**
 * Fetch single user from Firebase
 */
void fetchUserFromFirebase(String uid);

/**
 * Start streaming /users for realtime updates
 */
void streamUsers();

/**
 * Stop user stream
 */
void stopUserStream();

/**
 * Check if user stream is active
 */
bool isUserStreamActive();

// =============================================================================
// SYNC STATE
// =============================================================================

/**
 * Get current sync state
 */
SyncState getSyncState();

/**
 * Reset sync counters
 */
void resetSyncCounters();

/**
 * Register callback for user changes
 * @param callback - Function to call when user is added/removed
 */
typedef void (*UserChangeCallback)(String uid, String name, bool added);
void setUserChangeCallback(UserChangeCallback callback);

#endif // FIREBASE_H
