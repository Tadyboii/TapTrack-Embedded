/*
 * TapTrack - System Configuration
 * All tuneable parameters in one place
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// PIN DEFINITIONS
// =============================================================================

// RFID Module (MFRC522)
#define RFID_RST_PIN            21
#define RFID_SS_PIN             5
#define RFID_IRQ_PIN            4

// DS1302 RTC
#define RTC_IO_PIN              26
#define RTC_SCLK_PIN            25
#define RTC_CE_PIN              27

// Indicator LEDs
#define LED_GREEN_PIN           13      // Online success
#define LED_YELLOW_PIN          12      // Offline/queued
#define LED_RED_PIN             14      // Error
#define LED_BLUE_PIN            2       // Mode indicator (optional, uses onboard LED)

// Buzzer
#define BUZZER_PIN              15

// Mode Toggle Button
#define MODE_BUTTON_PIN         33      // Pull-up, active LOW
#define SYNC_BUTTON_PIN         32      // Manual sync trigger (optional)

// =============================================================================
// TIMING CONFIGURATION
// =============================================================================

// Tap handling
#define TAP_COOLDOWN_MS         30000   // 30 seconds between same card taps

// Sync intervals
#define SYNC_INTERVAL_MS        30000   // Try to sync queue every 30 seconds
#define WIFI_CHECK_INTERVAL_MS  60000   // Check WiFi every 60 seconds
#define STREAM_RECONNECT_MS     30000   // Reconnect stream if disconnected

// Captive portal
#define PORTAL_TIMEOUT_MS       300000  // 5 minutes portal timeout

// RFID module reset
#define RFID_RESET_INTERVAL_MS  5000    // Reset MFRC522 every 5 seconds

// Debounce
#define BUTTON_DEBOUNCE_MS      50      // Button debounce time

// =============================================================================
// ATTENDANCE CONFIGURATION
// =============================================================================

// Time thresholds (24-hour format)
#define ON_TIME_HOUR            9       // Before 9:00 AM is on time
#define LATE_HOUR               10       // 9:00 AM and after is late

// Valid year range for RTC sanity check
#define RTC_MIN_YEAR            2024
#define RTC_MAX_YEAR            2030

// =============================================================================
// STORAGE CONFIGURATION
// =============================================================================

// Attendance queue
#define MAX_QUEUE_SIZE          100     // Maximum offline records
#define QUEUE_WARNING_THRESHOLD 80      // Warn when queue reaches this size

// JSON buffer sizes
#define JSON_BUFFER_SMALL       1024    // Single record
#define JSON_BUFFER_MEDIUM      4096    // Multiple records
#define JSON_BUFFER_LARGE       8192    // Full sync

// SPIFFS file paths
#define QUEUE_FILE_PATH         "/attendance_queue.json"
#define USER_DB_FILE_PATH       "/user_database.json"
#define CONFIG_FILE_PATH        "/system_config.json"

// =============================================================================
// INDICATOR TIMING (milliseconds)
// =============================================================================

#define BEEP_SUCCESS_MS         100     // Single short beep
#define BEEP_ERROR_MS           200     // Error beep duration
#define BEEP_ERROR_PAUSE_MS     100     // Pause between error beeps

#define BLINK_FAST_MS           100     // Fast blink interval
#define BLINK_SLOW_MS           500     // Slow blink interval
#define BLINK_SYNC_MS           200     // Sync in progress blink

#define INDICATOR_DISPLAY_MS    2000    // How long to show indicator after tap

// =============================================================================
// NTP CONFIGURATION
// =============================================================================

#define NTP_SERVER              "pool.ntp.org"
#define GMT_OFFSET_SEC          (8 * 3600)  // GMT+8 Philippines
#define DAYLIGHT_OFFSET_SEC     0

// =============================================================================
// SYSTEM MODES
// =============================================================================

typedef enum {
    MODE_AUTO,          // Automatic online/offline based on connectivity
    MODE_FORCE_ONLINE,  // Force online mode (fail if no connection)
    MODE_FORCE_OFFLINE  // Force offline mode (never try to sync)
} SystemMode;

// Default mode on boot
#define DEFAULT_SYSTEM_MODE     MODE_AUTO

// =============================================================================
// DEBUG OPTIONS
// =============================================================================

#define DEBUG_SERIAL            true    // Enable serial debug output
#define DEBUG_FIREBASE          false   // Verbose Firebase logging
#define DEBUG_RFID              false   // Verbose RFID logging

#endif // CONFIG_H
