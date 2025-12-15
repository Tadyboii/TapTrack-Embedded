/*
 * TapTrack - Indicator Module
 * LED and Buzzer Feedback System
 * 
 * LED States:
 * - Green:  Online success
 * - Yellow: Offline/queued
 * - Red:    Error
 * - Blue:   Mode/status indicator
 */

#ifndef INDICATOR_H
#define INDICATOR_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// INDICATOR STATES
// =============================================================================

typedef enum {
    // Success states
    IND_SUCCESS_ONLINE,         // Green solid + short beep
    IND_SUCCESS_OFFLINE,        // Yellow solid + short beep
    IND_SUCCESS_QUEUED,         // Yellow blink + short beep
    
    // Error states
    IND_ERROR_GENERAL,          // Red solid + double beep
    IND_ERROR_UNREGISTERED,     // Red blink + double beep
    IND_ERROR_QUEUE_FULL,       // Red fast blink + long beep
    IND_ERROR_RTC_INVALID,      // Red + yellow + double beep
    
    // Status states
    IND_STATUS_SYNCING,         // Green blink (during sync)
    IND_STATUS_CONNECTING,      // Yellow blink (WiFi connecting)
    IND_STATUS_PORTAL_ACTIVE,   // Blue solid (captive portal)
    IND_STATUS_STREAM_ACTIVE,   // Blue blink (Firebase stream active)
    
    // Mode indicators
    IND_MODE_ONLINE,            // Blue solid
    IND_MODE_OFFLINE,           // Blue off
    IND_MODE_AUTO,              // Blue slow blink
    
    // System states
    IND_STARTUP,                // Sequence: G -> Y -> R -> all off
    IND_READY,                  // Brief green flash
    IND_PROCESSING,             // Yellow on (while reading card)
    
    // Clear all
    IND_CLEAR                   // All off
} IndicatorState;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * Initialize indicator module
 * Sets up all LED and buzzer pins
 */
void initIndicator();

/**
 * Set indicator to specific state
 * @param state - The indicator state to display
 * @param duration - How long to display (0 = until cleared)
 */
void setIndicator(IndicatorState state, uint16_t duration = INDICATOR_DISPLAY_MS);

/**
 * Update indicator (call in loop for blinking states)
 * Handles non-blocking blink patterns
 */
void updateIndicator();

/**
 * Clear all indicators
 */
void clearIndicators();

/**
 * Quick indicator functions (convenience wrappers)
 */
void indicateSuccessOnline();
void indicateSuccessOffline();
void indicateSuccessQueued();
void indicateError();
void indicateErrorUnregistered();
void indicateErrorQueueFull();
void indicateErrorRTC();
void indicateSyncing(bool active);
void indicateConnecting(bool active);
void indicatePortalActive(bool active);
void indicateProcessing(bool active);
void indicateMode(SystemMode mode);

/**
 * Buzzer functions
 */
void beep(uint16_t duration);
void beepSuccess();
void beepError();
void beepDouble();
void beepLong();

/**
 * Test all indicators
 */
void testIndicators();

/**
 * Startup animation sequence
 */
void startupSequence();

#endif // INDICATOR_H
