#include "gpio_evaluateSwitch.hpp"

static const char *TAG = "evaluateSwitch";


gpio_evaluatedSwitch::gpio_evaluatedSwitch( //minimal (use default values)
        gpio_num_t gpio_num_declare
        ){
    gpio_num = gpio_num_declare;
    pullup = true;
    inverted = false;

    init();
};



gpio_evaluatedSwitch::gpio_evaluatedSwitch( //optional parameters given
        gpio_num_t gpio_num_declare,
        bool pullup_declare,
        bool inverted_declare
        ){
    gpio_num = gpio_num_declare;
    pullup = pullup_declare;
    inverted = inverted_declare;

    init();
};



void gpio_evaluatedSwitch::init(){
    ESP_LOGI(TAG, "initializing gpio pin %d", (int)gpio_num);

    //define gpio pin as input
    //gpio_pad_select_gpio(gpio_num);
    gpio_set_direction(gpio_num, GPIO_MODE_INPUT);

    if (pullup == true){ //enable pullup if desired (default)
        //gpio_pad_select_gpio(gpio_num);
        gpio_set_pull_mode(gpio_num, GPIO_PULLUP_ONLY);
    }else{
        gpio_set_pull_mode(gpio_num, GPIO_FLOATING);
        //gpio_pad_select_gpio(gpio_num);
    }
    //TODO add pulldown option
    //gpio_set_pull_mode(gpio_num, GPIO_PULLDOWN_ONLY);
};


void gpio_evaluatedSwitch::handle(){  //Statemachine for debouncing and edge detection
    if (inverted == true){
        //=========================================================
        //=========== Statemachine for inverted switch ============
        //=================== (switch to VCC) =====================
        //=========================================================
        switch (p_state){
            default:
                p_state = switchState::FALSE;
                break;

            case switchState::FALSE:  //input confirmed high (released)
                fallingEdge = false; //reset edge event
                if (gpio_get_level(gpio_num) == 1){ //pin high (on)
                    p_state = switchState::HIGH;
                    timestampHigh = esp_log_timestamp(); //save timestamp switched from low to high
                } else {
                    msReleased = esp_log_timestamp() - timestampLow; //update duration released
                }
                break;

            case switchState::HIGH: //input recently switched to high (pressed)
                if (gpio_get_level(gpio_num) == 1){ //pin still high (on)
                    if (esp_log_timestamp() - timestampHigh > minOnMs){ //pin in same state long enough
                        p_state = switchState::TRUE;
                        state = true;
                        risingEdge = true;
                        msReleased = timestampHigh - timestampLow; //calculate duration the button was released 
                        //start a fresh press duration measurement (see non-inverted branch)
                        msPressed = esp_log_timestamp() - timestampHigh;
                    }
                }else{
                    p_state = switchState::FALSE;
                }
                break;

            case switchState::TRUE:  //input confirmed high (pressed)
                risingEdge = false; //reset edge event
                if (gpio_get_level(gpio_num) == 0){ //pin low (off)
                    timestampLow = esp_log_timestamp();
                    p_state = switchState::LOW;
                } else {
                    msPressed = esp_log_timestamp() - timestampHigh; //update duration pressed
                }

                break;

            case switchState::LOW: //input recently switched to low (released)
                if (gpio_get_level(gpio_num) == 0){ //pin still low (off)
                    if (esp_log_timestamp() - timestampLow > minOffMs){ //pin in same state long enough
                        p_state = switchState::FALSE;
                        state=false;
                        fallingEdge=true;
                        //note: msPressed intentionally not recalculated here (see non-inverted branch)
                    }
                }else{
                    p_state = switchState::TRUE;
                }
                break;
        }

    }else{
        //=========================================================
        //========= Statemachine for not inverted switch ==========
        //=================== (switch to GND) =====================
        //=========================================================
        switch (p_state){
            default:
                p_state = switchState::FALSE;
                break;

            case switchState::FALSE:  //input confirmed high (released)
                fallingEdge = false; //reset edge event
                if (gpio_get_level(gpio_num) == 0){ //pin low (on)
                    p_state = switchState::LOW;
                    timestampLow = esp_log_timestamp(); //save timestamp switched from high to low
                } else {
                    msReleased = esp_log_timestamp() - timestampHigh; //update duration released
                }
                break;

            case switchState::LOW: //input recently switched to low (pressed)
                if (gpio_get_level(gpio_num) == 0){ //pin still low (on)
                    if (esp_log_timestamp() - timestampLow > minOnMs){ //pin in same state long enough
                        p_state = switchState::TRUE;
                        state = true;
                        risingEdge = true;
                        msReleased = timestampLow - timestampHigh; //calculate duration the button was released
                        //start a fresh press duration measurement.
                        //without this msPressed would still hold the value of the PREVIOUS press
                        //while risingEdge is evaluated -> a short press could be read as a long one
                        msPressed = esp_log_timestamp() - timestampLow;
                    }
                }else{
                    p_state = switchState::FALSE;
                }
                break;

            case switchState::TRUE:  //input confirmed low (pressed)
                risingEdge = false; //reset edge event
                if (gpio_get_level(gpio_num) == 1){ //pin high (off)
                    timestampHigh = esp_log_timestamp();
                    p_state = switchState::HIGH;
                } else {
                    msPressed = esp_log_timestamp() - timestampLow; //update duration pressed
                }

                break;

            case switchState::HIGH: //input recently switched to high (released)
                if (gpio_get_level(gpio_num) == 1){ //pin still high (off)
                    if (esp_log_timestamp() - timestampHigh > minOffMs){ //pin in same state long enough
                        p_state = switchState::FALSE;
                        state=false;
                        fallingEdge=true;
                        //note: msPressed is intentionally NOT recalculated from timestampHigh here.
                        //timestampHigh is when the release was NOTICED, which can be much later than
                        //the actual release when handle() was delayed (e.g. blocking modbus command).
                        //The value measured while the input was last seen pressed is the safer one.
                    }
                }else{
                    p_state = switchState::TRUE;
                }
                break;
        }
    }

}


