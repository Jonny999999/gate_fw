#pragma once

#include <stdio.h>
#include <string.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
}



//declaration of enum for gate state
enum class gateState {IDLE, OPEN_START, OPEN_MOVING, CLOSE_START, CLOSE_MOVING, WAIT_RETRY, WAIT_LOCK};
//declaration of enum to tell stop function why the gate was stopped (for notifications)
enum class stopReason {REACHED, LIMIT, TIMEOUT, CANCEL, RETRY};
//declaration of enum describing the direction a gate is moving
enum class gateDirection {OPEN, CLOSE};
 
//string array for printing the state as string
//defined in gate.cpp (e.g. gateStateStr[(int)gateRight.state])
extern const char* gateStateStr[7]; 



//===================================
//=========== gate class ============
//===================================
//class which controls a gate with 2 relays and 2 limit switches
class gate {
    public:
        //--- constructor ---
        gate(
                gpio_num_t gpio_relayOpen_f,
                gpio_num_t gpio_relayClose_f,
                gpio_num_t gpio_switchOpen_f,
                gpio_num_t gpio_switchClosed_f,
                const char name_f[16],
                uint32_t msStop_f,
                uint32_t msRetry_f,
                uint32_t msTimeout_f = 10000
            );

        //--- functions ---
        void handle(void);
        void open(uint32_t msRun_t);
        void close(uint32_t msRun_t);
        void stop(stopReason reason);
        void setDuration(uint32_t msRun_f);

        //--- out ---
        gateState state = gateState::IDLE;
        gateDirection lastDirection = gateDirection::OPEN;
        //float position;

    private:
        //--- configuration ---
        gpio_num_t gpio_relayOpen;
        gpio_num_t gpio_relayClose;
        gpio_num_t gpio_switchOpen;
        gpio_num_t gpio_switchClosed;
        char name[16];
        uint32_t msStop = 1000; //ms gate moves after motor turned off
        uint32_t msTimeout = 10000;
        uint32_t msRetry = 500; //ms to wait before restarting after ending up at wrong limit switch

        //--- functions ---
        void init(void);
        void retry(void);
        void changeState(gateState stateNew); //update 'state' variable and send log output
        bool handleStopCondition(gateDirection direction); //check for targetduration, timeout, limitswitch

        //--- process variables ---
        uint32_t timestampStart;
        uint32_t timestampStop;
        uint32_t msRun;
};
