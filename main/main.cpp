#include <stdio.h>

extern "C"
{
#include <esp_system.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
}

#include "gpio_evaluateSwitch.hpp"
#include "config.hpp"

static const char *TAG = "gate ctl";


//===================================
//=========== gate class ============
//===================================
//class which controls a gate with 2 relays and 2 limit switches
class gate {
    public:
        gate(){
            ESP_LOGD(TAG, "constructor");
        }

        //functions
        void handle(void);

        void open(uint32_t msRun_t);
        void close(uint32_t msRun_t);
        void stop();
        void setDuration(uint32_t msRun_f);

        //config
        gpio_num_t gpio_relayOpen;
        gpio_num_t gpio_relayClose;
        gpio_num_t gpio_switchOpen;
        gpio_num_t gpio_switchClosed;
        uint32_t msTimeout = 10000;

        //out
        enum class gateState {IDLE, OPENING, CLOSING}; //OPEN, CLOSED, TIMEOUT
        gateState state = gateState::IDLE;
        //float position;

    private:
        void init(void);
        uint32_t timestampStart;
        uint32_t timestampStop;
        uint32_t msRun;
};


void gate::init(void){
    ESP_LOGI(TAG, "Initializing gate");
    //define relays as outputs
    gpio_set_direction(gpio_relayOpen, GPIO_MODE_OUTPUT);
    gpio_set_direction(gpio_relayClose, GPIO_MODE_OUTPUT);
    //define limit switches as inputs
    gpio_set_direction(gpio_switchOpen, GPIO_MODE_INPUT);
    gpio_set_direction(gpio_switchClosed, GPIO_MODE_INPUT);
}


//function to start opening the gate
void gate::open(uint32_t msRun_f){
    ESP_LOGI(TAG, "=> Opening gate");
    if (state == gateState::IDLE){
        //turn off close-relay
        gpio_set_level(gpio_relayClose, 0);
        //turn on open-relay
        gpio_set_level(gpio_relayOpen, 1);
        timestampStart = esp_log_timestamp();
        //set target time the motor should run
        msRun = msRun_f; //default = msTimeout

        state = gateState::OPENING;
    }
}


//function to start closing the gate
void gate::close(uint32_t msRun_f){
    ESP_LOGI(TAG, "=> Opening gate");
    if (state == gateState::IDLE){
        //turn off open-relay
        gpio_set_level(gpio_relayOpen, 0);
        //turn on close-relay
        gpio_set_level(gpio_relayClose, 1);
        timestampStart = esp_log_timestamp();
        //set target time the motor should run
        msRun = msRun_f; //default = msTimeout

        state = gateState::CLOSING;
    }
}


//function to stop the gate
void gate::stop(){
    timestampStop = esp_log_timestamp();
    //turn off both relays
    gpio_set_level(gpio_relayClose, 0);
    gpio_set_level(gpio_relayOpen, 0);

    state = gateState::IDLE;
}


//function with statemachine for a gate
//(handles limit switches, timeout, target time)
void gate::handle(void){
    ESP_LOGI(TAG, "handle function");
    switch(state){
        case gateState::IDLE:
            //do nothig, wait for function open() or close() to be called
            break;

        case gateState::OPENING:
            if (esp_log_timestamp() - timestampStart > msRun){ //target run duration exceeded
                stop();
            }else if (gpio_get_level(gpio_switchOpen) == 1){ //limit switch (completely open)
                stop();
            }else if (esp_log_timestamp() - timestampStart > msTimeout){ //timeout
                stop();
            }
            break;

        case gateState::CLOSING:
            if (esp_log_timestamp() - timestampStart > msRun){ //target run duration exceeded
                stop();
            }else if (gpio_get_level(gpio_switchClosed) == 1){ //limit switch (completely closed)
                stop();
            }else if (esp_log_timestamp() - timestampStart > msTimeout){ //timeout
                stop();
            }

            break;
    }
}




    gate gateRight;
//==================================
//===== statusReport function ======
//==================================
uint32_t timestamp_debugOutput = esp_log_timestamp();
const char* gateStateStr[3] = { "IDLE", "OPENING", "CLOSING" };
void statusReport(){
    
    ESP_LOGI(TAG, "============= STATUS REPORT =============");
    ESP_LOGI(TAG, "BUTTONS: S8 button_open=%d  |  S7 button_close=%d", buttonOpen.state, buttonClose.state);
    ESP_LOGI(TAG, "RIGHT GATE: State=%s  |  S1 open=%d  |  S2 closed=%d  |  K1 opening=  |  K2 closing=",
            gateStateStr[(int)gateRight.state], gpio_get_level(GPIO_B_RIGHT_OPEN), gpio_get_level(GPIO_B_RIGHT_CLOSED));
    ESP_LOGI(TAG, "LEFT GATE:  State=%s  |  S3 open=%d  |  S4 closed=%d  |  K1 opening=?  |  K2 closing=?",
            gateStateStr[(int)gateRight.state], gpio_get_level(GPIO_B_LEFT_OPEN), gpio_get_level(GPIO_B_LEFT_CLOSED));
    ESP_LOGI(TAG, "=========================================");
    timestamp_debugOutput = esp_log_timestamp();
}






extern "C" void app_main(void)
{

    //====================================
    //====== Create and Configure ========
    //========== Gate objects ============
    //====================================
    //------ right gate ------
    //create gate object
    //define gpio pins (see config.hpp for actual pin number)
    gateRight.gpio_relayOpen = GPIO_K_OPEN_RIGHT;
    gateRight.gpio_relayClose = GPIO_K_CLOSE_RIGHT;
    gateRight.gpio_switchOpen = GPIO_B_RIGHT_OPEN;
    gateRight.gpio_switchClosed = GPIO_B_RIGHT_CLOSED;
    //define parameters
    gateRight.msTimeout = 10000;

    //------ left gate ------
    //create gate object
    gate gateLeft;
    //define gpio pins (see config.hpp for actual pin number)
    gateLeft.gpio_relayOpen = GPIO_K_OPEN_LEFT;
    gateLeft.gpio_relayClose = GPIO_K_CLOSE_LEFT;
    gateLeft.gpio_switchOpen = GPIO_B_LEFT_OPEN;
    gateLeft.gpio_switchClosed = GPIO_B_LEFT_CLOSED;
    //define parameters
    gateLeft.msTimeout = 10000;

    


    while (1){
        vTaskDelay(10 / portTICK_PERIOD_MS); //slight delay is required otherwise watchdog will be triggered
        
        //run handle function for buttons TODO: run these in another task?
        //note: the switch objects are declared and configured in config.hpp
        buttonOpen.handle();
        buttonClose.handle();


        //testing evaluated switch - log button events
        if (buttonOpen.risingEdge){
            ESP_LOGI(TAG, "== Button open pressed == - time Released: %d", buttonOpen.msReleased);
        }else if (buttonOpen.fallingEdge){
            ESP_LOGI(TAG, "== Button open released == - time Pressed: %d", buttonOpen.msPressed);
        }

        if (buttonClose.risingEdge){
            ESP_LOGI(TAG, "== Button close pressed == - time Released: %d", buttonClose.msReleased);
        }else if (buttonClose.fallingEdge){
            ESP_LOGI(TAG, "== Button close released == - time Pressed: %d", buttonClose.msPressed);
        }


        //run handle function for gates
        //gate_right.handle();

        //show debug output in certain time intervals
        if (esp_log_timestamp() - timestamp_debugOutput > 5000){
            statusReport();
        }

    }


}
