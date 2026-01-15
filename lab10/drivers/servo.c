// #include <avr/io.h>
// #include "servo.h"

// void servo_init(void)
// {
//     // OC2A = PB3 → Output
//     DDRB |= (1 << PB3);

//     // Timer2: Phase Correct PWM (TOP = 255)
//     // Enable OC2A (non-inverting)
//     TCCR2A = (1 << WGM20) | (1 << COM2A1);

//     // Prescaler = 1024
//     TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20);

//     // Start centered
//     OCR2A = SERVO_45_RIGHT;
// }

// void servo_set_position(uint8_t position)
// {
//     OCR2A = position;
// }

// servo.c - Interrupt-driven servo using Timer2
// Connected to D10 = PB2

#include <avr/io.h>
#include <avr/interrupt.h>
#include "servo.h"

// =====================================================
// Configuration
// =====================================================
#define SERVO_PORT      PORTB
#define SERVO_DDR       DDRB
#define SERVO_PIN       PB2

#define SERVO_MIN_US    500     // 0.5ms = 0°
#define SERVO_MAX_US    2500    // 2.5ms = 180°
#define SERVO_PERIOD_US 20000   // 20ms period

// =====================================================
// Static Variables
// =====================================================
static volatile uint16_t pulse_width_us = 1500;  // Current pulse width
static volatile uint16_t elapsed_us = 0;         // Elapsed time in period
static volatile uint8_t pin_state = 0;           // Current pin state

static uint8_t current_angle = 90;

// =====================================================
// Timer2 Overflow ISR
// =====================================================
// Timer2 overflows every 128µs (prescaler 256, 8-bit)
// 16 MHz / 256 / 256 = 244 Hz overflow rate
// Each overflow ≈ 128µs

#define TICK_US 128

ISR(TIMER2_OVF_vect)
{
    elapsed_us += TICK_US;
    
    // Start of period - set pin high
    if (elapsed_us >= SERVO_PERIOD_US) {
        elapsed_us = 0;
        SERVO_PORT |= (1 << SERVO_PIN);
        pin_state = 1;
    }
    // End of pulse - set pin low
    else if (pin_state && elapsed_us >= pulse_width_us) {
        SERVO_PORT &= ~(1 << SERVO_PIN);
        pin_state = 0;
    }
}

// =====================================================
// Initialization
// =====================================================
void servo_init(void)
{
    // Set servo pin as output
    SERVO_DDR |= (1 << SERVO_PIN);
    SERVO_PORT &= ~(1 << SERVO_PIN);
    
    // Timer2: Normal mode, prescaler 256
    // Overflow every 256 * 256 / 16 MHz = 4.096ms... too slow
    // Use prescaler 64: 256 * 64 / 16 MHz = 1.024ms... still coarse
    // Use prescaler 32: 256 * 32 / 16 MHz = 512µs
    // Prescaler 8: 256 * 8 / 16 MHz = 128µs ← good resolution
    
    TCCR2A = 0;                         // Normal mode
    TCCR2B = (1 << CS21);               // Prescaler 8
    TIMSK2 = (1 << TOIE2);              // Enable overflow interrupt
    
    // Initialize to center
    servo_set_angle(90);
}

// =====================================================
// Set Servo Angle (0-180 degrees)
// =====================================================
void servo_set_angle(uint8_t angle)
{
    if (angle > 180) angle = 180;
    current_angle = angle;
    
    // Map 0-180° to 500-2500µs
    uint16_t us = SERVO_MIN_US + 
                  ((uint32_t)angle * (SERVO_MAX_US - SERVO_MIN_US)) / 180;
    
    // Atomic write
    uint8_t sreg = SREG;
    cli();
    pulse_width_us = us;
    SREG = sreg;
}

// =====================================================
// Set Servo Pulse Width (microseconds)
// =====================================================
void servo_set_pulse_us(uint16_t us)
{
    if (us < SERVO_MIN_US) us = SERVO_MIN_US;
    if (us > SERVO_MAX_US) us = SERVO_MAX_US;
    
    uint8_t sreg = SREG;
    cli();
    pulse_width_us = us;
    SREG = sreg;
    
    // Update angle estimate
    current_angle = (uint8_t)(((uint32_t)(us - SERVO_MIN_US) * 180) / 
                              (SERVO_MAX_US - SERVO_MIN_US));
}

// =====================================================
// Set Servo Pulse Width (in 50µs ticks) - compatibility
// =====================================================
void servo_set_pulse_ticks(uint16_t ticks)
{
    servo_set_pulse_us(ticks * 50);
}

// =====================================================
// Disable Servo
// =====================================================
void servo_disable(void)
{
    TIMSK2 &= ~(1 << TOIE2);            // Disable interrupt
    SERVO_PORT &= ~(1 << SERVO_PIN);    // Pin low
}

// =====================================================
// Enable Servo
// =====================================================
void servo_enable(void)
{
    elapsed_us = 0;
    pin_state = 0;
    TIMSK2 |= (1 << TOIE2);             // Enable interrupt
}

// =====================================================
// Get Current Servo Angle
// =====================================================
uint8_t servo_get_angle(void)
{
    return current_angle;
}