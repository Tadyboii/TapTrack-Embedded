#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>
#include <WiFi.h>

// IMPORTANT: These must be defined BEFORE including FirebaseClient.h
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

// Firebase credentials - UPDATE THESE WITH YOUR VALUES
#define Web_API_KEY "AIzaSyBLtOgdEJKVre8tbbd3iEO8fN5PE30ZNm4"
#define DATABASE_URL "https://taptrackapp-38817-default-rtdb.firebaseio.com"
#define USER_EMAIL "thaddeus.rosales@g.msuiit.edu.ph"
#define USER_PASS "123456789"

// External declarations
extern UserAuth user_auth;
extern FirebaseApp app;
extern WiFiClientSecure ssl_client;
extern AsyncClientClass aClient;
extern RealtimeDatabase Database;
extern object_t jsonData, obj1, obj2, obj3, obj4, obj5;
extern JsonWriter writer;

// Function declarations
void initFirebase();
void processData(AsyncResult &aResult);
void sendToFirebase(String uid, String name, String timestamp, String attendanceStatus, String registrationStatus);

// New functions for user registration
void sendPendingUser(String uid, String timestamp);
void sendRegisteredUser(String uid, String name, String timestamp);

void fetchAllUsersFromFirebase(); 
void fetchUserFromFirebase(String uid);
void streamUsers();

#endif