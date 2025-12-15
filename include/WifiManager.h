/*
 * TapTrack - WiFi Manager
 * Captive portal for WiFi configuration with offline mode support
 */

#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "config.h"
#include "secrets.h"

// =============================================================================
// CONFIGURATION STRUCTURE
// =============================================================================

struct WiFiConfig {
    String ssid;
    String password;
    SystemMode defaultMode;
    bool configured;
};

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * Initialize WiFi Manager
 * Attempts to connect with saved credentials, or starts portal
 * @return true if connected to WiFi
 */
bool initWiFiManager();

/**
 * Start captive portal for WiFi configuration
 */
void startCaptivePortal();

/**
 * Handle captive portal requests (call in loop while portal active)
 */
void handlePortal();

/**
 * Check if captive portal is currently active
 */
bool isPortalActive();

/**
 * Stop captive portal
 */
void stopCaptivePortal();

/**
 * Connect to WiFi with given credentials
 * @param ssid - Network SSID
 * @param password - Network password
 * @param timeout - Connection timeout in ms
 * @return true if connected
 */
bool connectToWiFi(String ssid, String password, uint32_t timeout = 20000);

/**
 * Save WiFi credentials to persistent storage
 */
void saveWiFiCredentials(String ssid, String password);

/**
 * Load WiFi credentials from persistent storage
 * @return true if credentials were found
 */
bool loadWiFiCredentials(String &ssid, String &password);

/**
 * Clear stored WiFi credentials
 */
void clearWiFiCredentials();

/**
 * Save system mode preference
 */
void saveSystemMode(SystemMode mode);

/**
 * Load system mode preference
 */
SystemMode loadSystemMode();

/**
 * Get WiFi signal strength as percentage
 * @return Signal strength 0-100
 */
int getWiFiSignalPercent();

/**
 * Get WiFi signal bars (1-4)
 */
int getWiFiSignalBars();

/**
 * Check if WiFi is connected
 */
bool isWiFiConnected();

/**
 * Disconnect from WiFi
 */
void disconnectWiFi();

/**
 * Attempt to reconnect to WiFi
 * @return true if reconnected
 */
bool reconnectWiFi();

#endif // WIFIMANAGER_H
