// Header guard to prevent multiple inclusions
#ifndef GPIO_H
#define GPIO_H

#include <avr/io.h>

// --- Type Definitions for Readability ---
typedef enum { GPIO_PORT_B, GPIO_PORT_C, GPIO_PORT_D } GpioPort_t;
typedef enum { GPIO_PIN_INPUT, GPIO_PIN_OUTPUT } GpioDirection_t;
typedef enum { GPIO_PIN_LOW, GPIO_PIN_HIGH } GpioValue_t;

// --- Function Prototypes (Public API) ---
void gpio_set_direction(GpioPort_t port, uint8_t pin_num, GpioDirection_t direction);
void gpio_set_pullup(GpioPort_t port, uint8_t pin_num, uint8_t enable);
void gpio_write(GpioPort_t port, uint8_t pin_num, GpioValue_t value);
GpioValue_t gpio_read(GpioPort_t port, uint8_t pin_num);

#endif // GPIO_H