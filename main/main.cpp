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
#include "gate.hpp"
#include "control.hpp"

static const char *TAG = "main";



//==================================
//===== statusReport function ======
//==================================
uint32_t timestamp_debugOutput = esp_log_timestamp();
void statusReport(){
    
    ESP_LOGI(TAG, "============= STATUS REPORT =============");
    ESP_LOGI(TAG, "BUTTONS: S8 button_open=%d  |  S7 button_close=%d", buttonOpen.state, buttonClose.state);
    ESP_LOGI(TAG, "CONTROL: State=%s  |  count=%d  |  lastAction=%d", controlStateStr[(int)ctlState], countPressed, timestampLastAction);
    ESP_LOGI(TAG, "RIGHT GATE: State=%s  |  S1 open=%d  |  S2 closed=%d  |  K1 opening=  |  K2 closing=",
            gateStateStr[(int)gateRight.state], gpio_get_level(GPIO_B_RIGHT_OPEN), gpio_get_level(GPIO_B_RIGHT_CLOSED));
    ESP_LOGI(TAG, "LEFT GATE:  State=%s  |  S3 open=%d  |  S4 closed=%d  |  K1 opening=?  |  K2 closing=?",
            gateStateStr[(int)gateRight.state], gpio_get_level(GPIO_B_LEFT_OPEN), gpio_get_level(GPIO_B_LEFT_CLOSED));
    ESP_LOGI(TAG, "=========================================");
    timestamp_debugOutput = esp_log_timestamp();
}






extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Start of main function...");

    //define buzzer pin as output
    gpio_pad_select_gpio(GPIO_NUM_12);
    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_OUTPUT);
    //beep at startup
    gpio_set_level(GPIO_NUM_12, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_NUM_12, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_NUM_12, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_NUM_12, 0);


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


        //run function that handles buttons and controls the gate
        control();

        //run handle function for gates
        gateRight.handle();
        gateLeft.handle();

        //show debug output in certain time intervals
        if (esp_log_timestamp() - timestamp_debugOutput > 5000){
            statusReport();
        }

    }


}
