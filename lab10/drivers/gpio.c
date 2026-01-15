#include "gpio.h"

// Helper arrays to map enum to actual registers
static volatile uint8_t* ddr_registers[] = {&DDRB, &DDRC, &DDRD};
static volatile uint8_t* port_registers[] = {&PORTB, &PORTC, &PORTD};
static volatile uint8_t* pin_registers[] = {&PINB, &PINC, &PIND};

void gpio_set_direction(GpioPort_t port, uint8_t pin_num, GpioDirection_t direction) {
    if (direction == GPIO_PIN_OUTPUT) {
        *ddr_registers[port] |= (1 << pin_num);
    } else {
        *ddr_registers[port] &= ~(1 << pin_num);
    }
}

void gpio_write(GpioPort_t port, uint8_t pin_num, GpioValue_t value) {
    // TODO: Implement this function. Use the correct register from the
    // 'port_registers' array and perform bitwise operations to set
    // the pin HIGH or LOW based on the 'value' parameter.
    if (value == GPIO_PIN_HIGH) {
        *port_registers[port] |= (1 << pin_num);
    } else {
        *port_registers[port] &= ~(1 << pin_num);
    }
}

void gpio_set_pullup(GpioPort_t port, uint8_t pin_num, uint8_t enable) {
    // HINT: Enabling a pull-up is the same as writing HIGH to an input pin.
    gpio_write(port, pin_num, enable ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
}

GpioValue_t gpio_read(GpioPort_t port, uint8_t pin_num) {
    // TODO: Implement this function. Use the correct register from the
    // 'pin_registers' array. Read the register's value, check if the
    // specific bit for 'pin_num' is set, and return GPIO_PIN_HIGH or GPIO_PIN_LOW.
    if (*pin_registers[port] & (1 << pin_num)) {
        return GPIO_PIN_HIGH;
    } else {
        return GPIO_PIN_LOW;
    }
}