#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

// =====================================================
// Servo Configuration
// =====================================================
// Servo angles and timing are defined in config.h:
//   SERVO_ANGLE_RIGHT      - Right wall scan position
//   SERVO_ANGLE_FRONT      - Forward obstacle scan
//   SERVO_ANGLE_LEFT       - Left wall scan
//   SERVO_ANGLE_CENTER     - Neutral/idle position
//   SERVO_SETTLE_TIME_MS   - Time to wait after movement
// =====================================================

// =====================================================
// Function Prototypes
// =====================================================

// Initialize servo on D10 (PB2) using Timer2 software PWM
void servo_init(void);

// Set servo angle (0-180 degrees)
void servo_set_angle(uint8_t angle);

// Set servo pulse width directly (in 100µs ticks)
void servo_set_pulse_ticks(uint16_t ticks);

// Set servo pulse width (in microseconds, typically 1000-2000)
void servo_set_pulse_us(uint16_t pulse_us);

// Disable servo (stop PWM signal)
void servo_disable(void);

// Enable servo (resume PWM signal)
void servo_enable(void);

// Get current servo angle (approximate)
uint8_t servo_get_angle(void);

#endif // SERVO_H