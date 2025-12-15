#include "gpio.h"
#include "esp_rom_sys.h"

void gpio_pin_init(uint8_t pin, gpio_dir_t dir) {
    gpio_pin_init_pullup(pin, dir, GPIO_PULL_NONE);
}

void gpio_pin_init_pullup(uint8_t pin, gpio_dir_t dir, gpio_pull_t pull) {
    gpio_config_t io_conf = {0};

    io_conf.pin_bit_mask = (1ULL << pin);

    if (dir == GPIO_OUTPUT_MODE) {
        io_conf.mode = GPIO_MODE_OUTPUT;
    } else {
        io_conf.mode = GPIO_MODE_INPUT;
    }

    // Configure pull-up/pull-down
    if (pull == GPIO_PULL_UP) {
        io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    } else if (pull == GPIO_PULL_DOWN) {
        io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    } else {
        io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    }

    io_conf.intr_type    = GPIO_INTR_DISABLE;

    gpio_config(&io_conf);
}

void gpio_write(uint8_t pin, uint8_t level) {
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

uint8_t gpio_read(uint8_t pin) {
    return (uint8_t)gpio_get_level((gpio_num_t)pin);
}
void delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}