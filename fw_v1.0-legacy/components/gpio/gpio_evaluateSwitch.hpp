#pragma once

#include <stdio.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
}

//constructor examples:
//switch to gnd and us internal pullup:
//gpio_evaluatedSwitch s3(GPIO_NUM_14);
//switch to gnd dont use internal pullup:
//gpio_evaluatedSwitch s3(GPIO_NUM_14 false);
//switch to VCC (inverted) and dont use internal pullup:
//gpio_evaluatedSwitch s3(GPIO_NUM_14 false, true);


class gpio_evaluatedSwitch {
    public:
        //--- input ---
        uint32_t minOnMs = 50;
        uint32_t minOffMs = 50;
        gpio_evaluatedSwitch( //constructor minimal (default parameters pullup=true, inverted=false)
                gpio_num_t gpio_num_declare
                );
        gpio_evaluatedSwitch( //constructor with optional parameters
                gpio_num_t gpio_num_declare,
                bool pullup_declare,
                bool inverted_declare=false
                );

        //--- output ---         TODO make readonly? (e.g. public section: const int& x = m_x;)
        bool state = false;
        bool risingEdge = false;
        bool fallingEdge = false;
        uint32_t msPressed = 0;
        uint32_t msReleased = 0;

        //--- functions ---
        void handle();  //Statemachine for debouncing and edge detection

    private:
        gpio_num_t gpio_num;
        bool pullup;
        bool inverted;

        enum class switchState {TRUE, FALSE, LOW, HIGH};
        switchState p_state = switchState::FALSE;
        uint32_t timestampLow = 0;
        uint32_t timestampHigh = 0;
        void init();

};


