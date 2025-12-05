#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// Configuration
#define AP_SSID "TapTrack-Setup"
#define AP_PASSWORD ""  // Open network for easier setup
#define PORTAL_TIMEOUT 300000  // 5 minutes timeout

// Function declarations
bool initWiFiManager();
void startCaptivePortal();
bool connectToWiFi(String ssid, String password);
void saveWiFiCredentials(String ssid, String password);
bool loadWiFiCredentials(String &ssid, String &password);
void clearWiFiCredentials();

#endif