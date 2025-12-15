/*
 * TapTrack - Secrets Configuration
 * IMPORTANT: Add this file to .gitignore!
 * 
 * Copy this file to secrets.h and fill in your credentials
 */

#ifndef SECRETS_H
#define SECRETS_H

// =============================================================================
// FIREBASE CREDENTIALS
// =============================================================================

#define FIREBASE_API_KEY        "AIzaSyBLtOgdEJKVre8tbbd3iEO8fN5PE30ZNm4"
#define FIREBASE_DATABASE_URL   "https://taptrackapp-38817-default-rtdb.firebaseio.com"
#define FIREBASE_USER_EMAIL     "thaddeus.rosales@g.msuiit.edu.ph"
#define FIREBASE_USER_PASSWORD  "123456789"

// =============================================================================
// WIFI CREDENTIALS (Optional - for hardcoded fallback)
// =============================================================================

// Leave empty to always use captive portal
#define DEFAULT_WIFI_SSID       ""
#define DEFAULT_WIFI_PASSWORD   ""

// =============================================================================
// ACCESS POINT CREDENTIALS
// =============================================================================

#define AP_SSID                 "TapTrack-Setup"
#define AP_PASSWORD             ""  // Empty for open network

#endif // SECRETS_H
