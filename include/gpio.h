#ifndef GPIO_BAREMETAL_H
#define GPIO_BAREMETAL_H

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_INPUT_MODE  = 0,
    GPIO_OUTPUT_MODE = 1
} gpio_dir_t;

typedef enum {
    GPIO_PULL_NONE   = 0,
    GPIO_PULL_UP     = 1,
    GPIO_PULL_DOWN   = 2
} gpio_pull_t;

void gpio_pin_init(uint8_t pin, gpio_dir_t dir);
void gpio_pin_init_pullup(uint8_t pin, gpio_dir_t dir, gpio_pull_t pull);
void gpio_write(uint8_t pin, uint8_t level);
uint8_t gpio_read(uint8_t pin);
void delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif