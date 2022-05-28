#pragma once

#include <stdio.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
}



//declaration of enum for gate state
enum class gateState {IDLE, OPENING, CLOSING}; //OPEN, CLOSED, TIMEOUT
extern const char* gateStateStr[3]; //string array for printing the state as string, defined in gate.cpp (e.g. gateStateStr[(int)gateRight.state])



//===================================
//=========== gate class ============
//===================================
//class which controls a gate with 2 relays and 2 limit switches
class gate {
    public:
        //constructor
        gate(
                gpio_num_t gpio_relayOpen_f,
                gpio_num_t gpio_relayClose_f,
                gpio_num_t gpio_switchOpen_f,
                gpio_num_t gpio_switchClosed_f,
                uint32_t msTimeout_f = 10000
            );

        //functions
        void handle(void);
        void open(uint32_t msRun_t);
        void close(uint32_t msRun_t);
        void stop();
        void setDuration(uint32_t msRun_f);

        //out
        gateState state = gateState::IDLE;
        //float position;

    private:
        //configuration
        gpio_num_t gpio_relayOpen;
        gpio_num_t gpio_relayClose;
        gpio_num_t gpio_switchOpen;
        gpio_num_t gpio_switchClosed;
        uint32_t msTimeout = 10000;

        //functions
        void init(void);

        //process variables
        uint32_t timestampStart;
        uint32_t timestampStop;
        uint32_t msRun;
};
