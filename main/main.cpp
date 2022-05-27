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

#include "config.hpp"

static const char *TAG = "gate ctl";

class gate {
    public:
        gate(){
            ESP_LOGD(TAG, "constructor");
        }

        //functions
        void init(void);
        void handle(void);

        void open();
        void close();
        void stop();

        //config
        gpio_num_t gpio_relayOpen;
        gpio_num_t gpio_relayClose;
        gpio_num_t gpio_switchOpen;
        gpio_num_t gpio_switchClosed;
        uint32_t ms_open = 10000;
        uint32_t ms_close = 10000;

        //out
        enum class gateState {IDLE, OPENING, CLOSING}; //OPEN, CLOSED, TIMEOUT
        gateState state = gateState::IDLE;
        float position;

    private:
};


void gate::init(void){
    ESP_LOGI(TAG, "Initializing gate");
}


void gate::handle(void){
    ESP_LOGI(TAG, "handle function");
    switch(state){
        case gateState::IDLE:
            
            break;

        case gateState::OPENING:
            if (gpio_get_level(gpio_switchOpen) == 1){
                gpio_set_level(gpio_relayOpen, 0);
                state = gateState::IDLE;
            }//TODO elsif timeout, target pos
            break;

        case gateState::CLOSING:
            if (gpio_get_level(gpio_switchClosed) == 1){
                gpio_set_level(gpio_relayClose, 0);
                state = gateState::IDLE;
            }//TODO elsif timeout, target pos
            break;
    }

}





uint32_t timestamp_debugOutput = esp_log_timestamp();
void statusReport(){
    ESP_LOGI(TAG, "============= STATUS REPORT =============");
    ESP_LOGI(TAG, "BUTTONS: S8 button_open=%d  |  S7 button_close=%d", gpio_get_level(GPIO_S_OPEN), gpio_get_level(GPIO_S_CLOSE));
    ESP_LOGI(TAG, "RIGHT GATE: S1 open=%d  |  S2 closed=%d  |  K1 opening=  |  K2 closing=", gpio_get_level(GPIO_B_RIGHT_OPEN), gpio_get_level(GPIO_B_RIGHT_CLOSED));
    ESP_LOGI(TAG, "LEFT GATE:  S3 open=%d  |  S4 closed=%d  |  K1 opening=?  |  K2 closing=?", gpio_get_level(GPIO_B_LEFT_OPEN), gpio_get_level(GPIO_B_LEFT_CLOSED));
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
    gate gate_right;
    //define gpio pins (see config.hpp for actual pin number)
    gate_right.gpio_relayOpen = GPIO_K_OPEN_RIGHT;
    gate_right.gpio_relayClose = GPIO_K_CLOSE_RIGHT;
    gate_right.gpio_switchOpen = GPIO_B_RIGHT_OPEN;
    gate_right.gpio_switchClosed = GPIO_B_RIGHT_CLOSED;
    //define parameters
    gate_right.ms_open = 10000;
    gate_right.ms_close = 10000;


    //initialize gate (define gpio's etc)
    gate_right.init();

    
    while (1){
        vTaskDelay(10 / portTICK_PERIOD_MS); //slight delay is required otherwise watchdog will be triggered
        //gate_right.handle();

        //show debug output in certain time intervals
        if (esp_log_timestamp() - timestamp_debugOutput > 5000){
            statusReport();
        }

    }


}
