// Minimal host stub of driver/gpio.h - just enough to compile the debounce logic off-target.
#pragma once
#include <stdint.h>

typedef int gpio_num_t;
#define GPIO_NUM_0 0

typedef enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT } gpio_mode_t;
typedef enum { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_FLOATING } gpio_pull_mode_t;

// implemented by the test: lets a test feed a level sequence to the state machine
int  gpio_get_level(gpio_num_t pin);
void gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
void gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t mode);
