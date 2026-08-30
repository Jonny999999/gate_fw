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
        uint32_t minOnMs = 40;  // note: default is 50, 20 triggered multiple events on open button 
        uint32_t minOffMs = 40; // TODO: add this as option to constructor
        gpio_evaluatedSwitch( //constructor minimal (default parameters pullup=true, inverted=false)
                gpio_num_t gpio_num_declare
                );
        gpio_evaluatedSwitch( //constructor with optional parameters
                gpio_num_t gpio_num_declare,
                bool pullup_declare,
                bool inverted_declare=false
                );

        //--- output ---         TODO make readonly? (e.g. public section: const int& x = m_x;)
        bool state = false;       // debounced state: true = pressed
        bool risingEdge = false;  // set for one handle() call when a press was confirmed
        bool fallingEdge = false; // set for one handle() call when a release was confirmed

        // Duration the button is / was pressed, in milliseconds.
        // Only ever advanced while the raw input is actually still reading 'pressed', so a
        // delayed handle() call can never inflate it (it may under-report by at most one
        // handle() interval, which fails safe towards 'short press').
        // Reset at every confirmed press, so it never carries a value over from the
        // previous press.
        uint32_t msPressed = 0;
        uint32_t msReleased = 0;  // duration the button is / was released, in milliseconds

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


