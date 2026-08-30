#pragma once

#include "buzzer.hpp"
#include "config.h"
#include "driver/gpio.h"

//=====================================================
//================ Control task =======================
//=====================================================
// Interprets user input and decides what the gates should do.
//
// It does not touch the Gate objects directly - all movement goes through the command
// interface in gate_task.hpp, so this task never blocks on modbus. See ROADMAP.md B2.

// Structure to hold configuration parameters for the control logic
struct ControlConfig {
    gpio_num_t remoteOpenGpio;
    gpio_num_t remoteCloseGpio;
    gpio_num_t buttonOpenGpio;
    gpio_num_t buttonCloseGpio;
    gpio_num_t faultLedGpio;
    gpio_num_t lightBarrierGpio;
    buzzer_t* buzzer;
};

// Function to initialize and start the control task
// note: gateTaskStart() has to be called before this
void controlTask(void* param);
