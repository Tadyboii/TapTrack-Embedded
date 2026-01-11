/*
 * TapTrack - Indicator Module Implementation
 * Non-blocking LED patterns and buzzer feedback
 */

#include "indicator.h"
#include "gpio.h"

// =============================================================================
// STATE VARIABLES
// =============================================================================

static IndicatorState currentState = IND_CLEAR;
static unsigned long stateStartTime = 0;
static unsigned long lastBlinkTime = 0;
static uint16_t stateDuration = 0;
static bool blinkState = false;
static bool continuousMode = false;

// =============================================================================
// PRIVATE HELPERS
// =============================================================================

static void setLED(uint8_t pin, bool state) {
    gpio_write(pin, state ? 1 : 0);
}

static void allLEDsOff() {
    setLED(LED_GREEN_PIN, false);
    setLED(LED_YELLOW_PIN, false);
    setLED(LED_RED_PIN, false);
    setLED(LED_BLUE_PIN, false);
}

static void buzzerOff() {
    gpio_write(BUZZER_PIN, 0);
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

void initIndicator() {
    // Configure LED pins as outputs
    gpio_pin_init(LED_GREEN_PIN, GPIO_OUTPUT_MODE);
    gpio_pin_init(LED_YELLOW_PIN, GPIO_OUTPUT_MODE);
    gpio_pin_init(LED_RED_PIN, GPIO_OUTPUT_MODE);
    gpio_pin_init(LED_BLUE_PIN, GPIO_OUTPUT_MODE);
    gpio_pin_init(BUZZER_PIN, GPIO_OUTPUT_MODE);
    
    // Ensure all outputs are off initially
    clearIndicators();
    
    Serial.println(F("✓ Indicator module initialized"));
}

void setIndicator(IndicatorState state, uint16_t duration) {
    currentState = state;
    stateStartTime = millis();
    stateDuration = duration;
    lastBlinkTime = millis();
    blinkState = true;
    continuousMode = (duration == 0);
    
    // Set initial state
    allLEDsOff();
    
    switch (state) {
        case IND_SUCCESS_ONLINE:
            setLED(LED_GREEN_PIN, true);
            beepSuccess();
            break;
            
        case IND_SUCCESS_OFFLINE:
        case IND_SUCCESS_QUEUED:
            setLED(LED_YELLOW_PIN, true);
            beepSuccess();
            break;
            
        case IND_ERROR_GENERAL:
            setLED(LED_RED_PIN, true);
            beepError();
            break;
            
        case IND_ERROR_UNREGISTERED:
            setLED(LED_RED_PIN, true);
            beepDouble();
            break;
            
        case IND_ERROR_QUEUE_FULL:
            setLED(LED_RED_PIN, true);
            beepLong();
            break;
            
        case IND_ERROR_RTC_INVALID:
            setLED(LED_RED_PIN, true);
            setLED(LED_YELLOW_PIN, true);
            beepDouble();
            break;
            
        case IND_STATUS_SYNCING:
            setLED(LED_GREEN_PIN, true);
            break;
            
        case IND_STATUS_CONNECTING:
            setLED(LED_YELLOW_PIN, true);
            break;
            
        case IND_STATUS_PORTAL_ACTIVE:
            setLED(LED_BLUE_PIN, true);
            break;
            
        case IND_STATUS_STREAM_ACTIVE:
            setLED(LED_BLUE_PIN, true);
            break;
            
        case IND_MODE_ONLINE:
            setLED(LED_BLUE_PIN, true);
            break;
            
        case IND_MODE_OFFLINE:
            setLED(LED_BLUE_PIN, false);
            break;
            
        case IND_MODE_AUTO:
            setLED(LED_BLUE_PIN, true);
            break;
            
        case IND_PROCESSING:
            setLED(LED_YELLOW_PIN, true);
            break;
            
        case IND_READY:
            setLED(LED_GREEN_PIN, true);
            break;
            
        case IND_CLEAR:
        default:
            allLEDsOff();
            break;
    }
}

void updateIndicator() {
    unsigned long now = millis();
    
    // Check if timed state has expired
    if (!continuousMode && stateDuration > 0) {
        if (now - stateStartTime >= stateDuration) {
            clearIndicators();
            return;
        }
    }
    
    // Handle blinking states
    uint16_t blinkInterval = BLINK_SLOW_MS;
    bool needsBlink = false;
    
    switch (currentState) {
        case IND_SUCCESS_QUEUED:
        case IND_ERROR_UNREGISTERED:
            blinkInterval = BLINK_SLOW_MS;
            needsBlink = true;
            break;
            
        case IND_ERROR_QUEUE_FULL:
            blinkInterval = BLINK_FAST_MS;
            needsBlink = true;
            break;
            
        case IND_STATUS_SYNCING:
            blinkInterval = BLINK_SYNC_MS;
            needsBlink = true;
            break;
            
        case IND_STATUS_CONNECTING:
        case IND_STATUS_STREAM_ACTIVE:
            blinkInterval = BLINK_SLOW_MS;
            needsBlink = true;
            break;
            
        case IND_MODE_AUTO:
            blinkInterval = 1000;  // Very slow blink for mode
            needsBlink = true;
            break;
            
        default:
            break;
    }
    
    if (needsBlink && (now - lastBlinkTime >= blinkInterval)) {
        lastBlinkTime = now;
        blinkState = !blinkState;
        
        switch (currentState) {
            case IND_SUCCESS_QUEUED:
            case IND_STATUS_CONNECTING:
                setLED(LED_YELLOW_PIN, blinkState);
                break;
                
            case IND_ERROR_UNREGISTERED:
            case IND_ERROR_QUEUE_FULL:
                setLED(LED_RED_PIN, blinkState);
                break;
                
            case IND_STATUS_SYNCING:
                setLED(LED_GREEN_PIN, blinkState);
                break;
                
            case IND_STATUS_STREAM_ACTIVE:
            case IND_MODE_AUTO:
                setLED(LED_BLUE_PIN, blinkState);
                break;
                
            default:
                break;
        }
    }
}

void clearIndicators() {
    currentState = IND_CLEAR;
    continuousMode = false;
    allLEDsOff();
    buzzerOff();
}

// =============================================================================
// CONVENIENCE WRAPPERS
// =============================================================================

void indicateSuccessOnline() {
    setIndicator(IND_SUCCESS_ONLINE, INDICATOR_DISPLAY_MS);
    // Serial.println(F("✅ Success (Online) - Green LED"));
}

void indicateSuccessOffline() {
    setIndicator(IND_SUCCESS_OFFLINE, INDICATOR_DISPLAY_MS);
    // Serial.println(F("⚠️ Success (Offline) - Yellow LED"));
}

void indicateSuccessQueued() {
    setIndicator(IND_SUCCESS_QUEUED, INDICATOR_DISPLAY_MS);
    // Serial.println(F("📝 Queued - Yellow blink"));
}

void indicateError() {
    setIndicator(IND_ERROR_GENERAL, INDICATOR_DISPLAY_MS);
    // Serial.println(F("❌ Error - Red LED"));
}

void indicateErrorUnregistered() {
    setIndicator(IND_ERROR_UNREGISTERED, INDICATOR_DISPLAY_MS);
    // Serial.println(F("❌ Unregistered - Red blink"));
}

void indicateErrorQueueFull() {
    setIndicator(IND_ERROR_QUEUE_FULL, INDICATOR_DISPLAY_MS * 2);
    // Serial.println(F("❌ Queue Full - Red fast blink"));
}

void indicateErrorRTC() {
    setIndicator(IND_ERROR_RTC_INVALID, INDICATOR_DISPLAY_MS);
    // Serial.println(F("❌ RTC Invalid - Red+Yellow"));
}

void indicateSyncing(bool active) {
    if (active) {
        setIndicator(IND_STATUS_SYNCING, 0);  // Continuous until cleared
    } else {
        clearIndicators();
    }
}

void indicateConnecting(bool active) {
    if (active) {
        setIndicator(IND_STATUS_CONNECTING, 0);
    } else {
        clearIndicators();
    }
}

void indicatePortalActive(bool active) {
    if (active) {
        setIndicator(IND_STATUS_PORTAL_ACTIVE, 0);
    } else {
        clearIndicators();
    }
}

void indicateProcessing(bool active) {
    if (active) {
        setIndicator(IND_PROCESSING, 0);
    } else {
        clearIndicators();
    }
}

void indicateMode(SystemMode mode) {
    switch (mode) {
        case MODE_FORCE_ONLINE:
            setIndicator(IND_MODE_ONLINE, 0);
            break;
        case MODE_FORCE_OFFLINE:
            setIndicator(IND_MODE_OFFLINE, 0);
            break;
        case MODE_AUTO:
        default:
            setIndicator(IND_MODE_AUTO, 0);
            break;
    }
}

// =============================================================================
// BUZZER FUNCTIONS
// =============================================================================

void beep(uint16_t duration) {
    gpio_write(BUZZER_PIN, 1);
    delay(duration);
    gpio_write(BUZZER_PIN, 0);
}

void beepSuccess() {
    beep(BEEP_SUCCESS_MS);
}

void beepError() {
    // beep(BEEP_ERROR_MS);
    // delay(BEEP_ERROR_PAUSE_MS);
    // beep(BEEP_ERROR_MS);
}

void beepDouble() {
    beep(BEEP_SUCCESS_MS);
    delay(BEEP_ERROR_PAUSE_MS);
    beep(BEEP_SUCCESS_MS);
}

void beepLong() {
    beep(500);
}

// =============================================================================
// TEST & STARTUP
// =============================================================================

void testIndicators() {
    Serial.println(F("\n🔍 Testing Indicator Module..."));
    
    Serial.println(F("  Testing Green LED..."));
    setLED(LED_GREEN_PIN, true);
    delay(500);
    setLED(LED_GREEN_PIN, false);
    delay(200);
    
    Serial.println(F("  Testing Yellow LED..."));
    setLED(LED_YELLOW_PIN, true);
    delay(500);
    setLED(LED_YELLOW_PIN, false);
    delay(200);
    
    Serial.println(F("  Testing Red LED..."));
    setLED(LED_RED_PIN, true);
    delay(500);
    setLED(LED_RED_PIN, false);
    delay(200);
    
    Serial.println(F("  Testing Blue LED..."));
    setLED(LED_BLUE_PIN, true);
    delay(500);
    setLED(LED_BLUE_PIN, false);
    delay(200);
    
    Serial.println(F("  Testing Buzzer..."));
    beepSuccess();
    delay(300);
    beepError();
    
    Serial.println(F("✓ Indicator test complete\n"));
}

void startupSequence() {
    Serial.println(F("🚀 Startup sequence..."));
    
    // Green
    setLED(LED_GREEN_PIN, true);
    beep(50);
    delay(200);
    setLED(LED_GREEN_PIN, false);
    
    // Yellow
    setLED(LED_YELLOW_PIN, true);
    beep(50);
    delay(200);
    setLED(LED_YELLOW_PIN, false);
    
    // Red
    setLED(LED_RED_PIN, true);
    beep(50);
    delay(200);
    setLED(LED_RED_PIN, false);
    
    // Blue
    setLED(LED_BLUE_PIN, true);
    beep(50);
    delay(200);
    setLED(LED_BLUE_PIN, false);
    
    // All on briefly
    allLEDsOff();
    delay(100);
    setLED(LED_GREEN_PIN, true);
    setLED(LED_YELLOW_PIN, true);
    setLED(LED_RED_PIN, true);
    setLED(LED_BLUE_PIN, true);
    delay(300);
    allLEDsOff();
    
    Serial.println(F("✓ Startup complete"));
}