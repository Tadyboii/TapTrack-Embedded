#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "drivers/servo.h"
#include "drivers/gpio.h"

/* ===================== PIN DEFINITIONS ===================== */
#define GREEN_LED_PIN   PD7
#define YELLOW_LED_PIN  PB0
#define RED_LED_PIN     PB1
#define BUZZER_PIN      PB3

/* INT pins (ATmega328P) */
#define INT0_PIN PD2    // RFID detected
#define INT1_PIN PD3    // User registered

/* ===================== TIMEOUTS ===================== */
#define SCANNING_TIMEOUT 30  // 30 iterations * 100ms = 3 seconds

/* ===================== SYSTEM STATES ===================== */
typedef enum {
    IDLE,
    SCANNING,
    ACCESS_DENIED,
    OPEN_DOOR,
    CLOSE_DOOR
} system_state_t;

/* ===================== INTERRUPT FLAGS ===================== */
volatile uint8_t card_detected = 0;      // INT0
volatile uint8_t user_registered = 0;    // INT1

/* ===================== INTERRUPT SETUP ===================== */
void external_interrupt_init(void)
{
    /* Configure INT0 (PD2) and INT1 (PD3) as inputs */
    DDRD &= ~((1 << INT0_PIN) | (1 << INT1_PIN));

    /* Enable internal pull-ups */
    PORTD |= (1 << INT0_PIN) | (1 << INT1_PIN);

    /* Configure falling edge trigger */
    EICRA &= ~((1 << ISC00) | (1 << ISC10));
    EICRA |=  (1 << ISC01) | (1 << ISC11);

    /* Clear pending interrupt flags */
    EIFR |= (1 << INTF0) | (1 << INTF1);

    /* Enable INT0 and INT1 */
    EIMSK |= (1 << INT0) | (1 << INT1);
}

/* ===================== ISRs ===================== */
ISR(INT0_vect)
{
    card_detected = 1;      // RFID card detected
}

ISR(INT1_vect)
{
    user_registered = 1;   // User verified
}

/* ===================== MAIN ===================== */
int main(void)
{
    external_interrupt_init();
    servo_init();

    gpio_set_direction(GPIO_PORT_D, GREEN_LED_PIN, GPIO_PIN_OUTPUT);
    gpio_set_direction(GPIO_PORT_B, YELLOW_LED_PIN, GPIO_PIN_OUTPUT);
    gpio_set_direction(GPIO_PORT_B, RED_LED_PIN, GPIO_PIN_OUTPUT);
    gpio_set_direction(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_OUTPUT);

    sei(); 

    system_state_t state = IDLE;
    static uint8_t scan_counter = 0;  
    uint8_t i;  

    while (1)
    {
        switch (state)
        {
            /* ===================== IDLE ===================== */
            case IDLE:
                gpio_write(GPIO_PORT_D, GREEN_LED_PIN, GPIO_PIN_LOW);
                gpio_write(GPIO_PORT_B, YELLOW_LED_PIN, GPIO_PIN_LOW);
                gpio_write(GPIO_PORT_B, RED_LED_PIN, GPIO_PIN_LOW);
                gpio_write(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_LOW);

                servo_set_angle(0); // Door closed

                if (card_detected) {
                    card_detected = 0;
                    state = SCANNING;
                }
                break;

            /* ===================== SCANNING ===================== */
            case SCANNING:
                // Blink yellow LED to show scanning
                gpio_write(GPIO_PORT_B, YELLOW_LED_PIN, GPIO_PIN_HIGH);
                _delay_ms(50);
                gpio_write(GPIO_PORT_B, YELLOW_LED_PIN, GPIO_PIN_LOW);
                _delay_ms(50);
                
                scan_counter++;
                
                // Check if user registered
                if (user_registered) {
                    user_registered = 0;
                    scan_counter = 0;  // Reset counter
                    gpio_write(GPIO_PORT_B, YELLOW_LED_PIN, GPIO_PIN_LOW);
                    state = OPEN_DOOR;
                }
                // Check for timeout (3 seconds)
                else if (scan_counter >= SCANNING_TIMEOUT) {
                    scan_counter = 0;  // Reset counter
                    gpio_write(GPIO_PORT_B, YELLOW_LED_PIN, GPIO_PIN_LOW);
                    state = ACCESS_DENIED;
                }
                break;

            /* ===================== ACCESS DENIED ===================== */
            case ACCESS_DENIED:
                gpio_write(GPIO_PORT_B, RED_LED_PIN, GPIO_PIN_HIGH);
                gpio_write(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_HIGH);
                _delay_ms(100);
                gpio_write(GPIO_PORT_B, RED_LED_PIN, GPIO_PIN_LOW);
                gpio_write(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_LOW);
                _delay_ms(100);
                gpio_write(GPIO_PORT_B, RED_LED_PIN, GPIO_PIN_HIGH);
                gpio_write(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_HIGH);
                _delay_ms(100);
                gpio_write(GPIO_PORT_B, RED_LED_PIN, GPIO_PIN_LOW);
                gpio_write(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_LOW);
                _delay_ms(100);
                
                card_detected = 0;
                user_registered = 0;
                state = IDLE;
                break;

            /* ===================== OPEN DOOR ===================== */
            case OPEN_DOOR:
                gpio_write(GPIO_PORT_D, GREEN_LED_PIN, GPIO_PIN_HIGH);
                gpio_write(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_HIGH);
                _delay_ms(200);
                gpio_write(GPIO_PORT_B, BUZZER_PIN, GPIO_PIN_LOW);
                servo_set_angle(90);   // Open door
                _delay_ms(2000);
                card_detected = 0;
                user_registered = 0;
                state = CLOSE_DOOR;
                break;

            /* ===================== CLOSE DOOR ===================== */
            case CLOSE_DOOR:
                gpio_write(GPIO_PORT_D, GREEN_LED_PIN, GPIO_PIN_LOW);
                servo_set_angle(0);    // Close door
                _delay_ms(500);
   
                card_detected = 0;
                user_registered = 0;
                state = IDLE;
                break;
        }
        _delay_ms(10);
    }
    
    return 0;
}