#pragma once

#include "gate.hpp"
#include "config.h"
#include "driver/gpio.h"

// Structure to hold configuration parameters for the control logic
struct ControlConfig {
    Gate* gateA;
    Gate* gateB;
    gpio_num_t remoteOpenGpio;
    gpio_num_t remoteCloseGpio;
    gpio_num_t buttonOpenGpio;
    gpio_num_t buttonCloseGpio;
    gpio_num_t faultLedGpio;
    gpio_num_t lightBarrierGpio;
    buzzer_t* buzzer;
};

// Function to initialize and start the control task
void controlTask(void* param);

