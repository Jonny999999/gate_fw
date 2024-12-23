#pragma once

#include <stdio.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
}

#include "freertos/queue.h"



//===================================
//========= buzzer_t class ==========
//===================================
//class which blinks a gpio pin for the provided count and durations.
//- 'processQueue' has to be run in a separate task
//- uses a queue to queue up multiple beep commands
class buzzer_t {
    public:
        //--- constructor ---
        buzzer_t(gpio_num_t gpio_pin_f, uint16_t msGap_f = 200);

        //--- functions ---
        void processQueue(); //has to be run once in a separate task, waits for and processes queued events
        void beep(uint8_t count, uint16_t msOn, uint16_t msOff);
        //void clear(); (TODO - not implemented yet)
        //void createTask(); (TODO - not implemented yet)

        //--- variables ---
        uint16_t msGap; //gap between beep entries (when multiple queued)
        
    private:
        //--- functions ---
        void init();

        //--- variables ---
        gpio_num_t gpio_pin;

        struct beepEntry {
            uint8_t count;
            uint16_t msOn;
            uint16_t msOff;
        };

        //queue for queueing up multiple events while one is still processing
        QueueHandle_t beepQueue = NULL;

};



