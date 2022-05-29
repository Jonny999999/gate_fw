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
    ESP_LOGI(TAG, "BUTTONS (evaluated): S8 button_open=%d  |  S7 button_close=%d", buttonOpen.state, buttonClose.state);
    ESP_LOGI(TAG, "BUTTONS (gpioLevel): S8 button_open=%d  |  S7 button_close=%d", gpio_get_level(GPIO_S_OPEN), gpio_get_level(GPIO_S_CLOSE));
    ESP_LOGI(TAG, "RIGHT GATE:  S1 open=%d  |  S2 closed=%d  |  K1 opening=?  |  K2 closing=?", gpio_get_level(GPIO_B_RIGHT_OPEN), gpio_get_level(GPIO_B_RIGHT_CLOSED));
    ESP_LOGI(TAG, "LEFT GATE:   S3 open=%d  |  S4 closed=%d  |  K1 opening=?  |  K2 closing=?", gpio_get_level(GPIO_B_LEFT_OPEN), gpio_get_level(GPIO_B_LEFT_CLOSED));
    ESP_LOGI(TAG, "=========================================");
    timestamp_debugOutput = esp_log_timestamp();
}




//function to control gate/two outputs via open/close button
void controlButtonGate(gpio_num_t gpio_relayOpen, gpio_num_t gpio_relayClose){
    //open
    if (gpio_get_level(GPIO_S_OPEN) == 1){
        gpio_set_level(gpio_relayOpen, 1);
        //beep when pin is on
        gpio_set_level(GPIO_NUM_12, 1);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_12, 0);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }else{
        gpio_set_level(gpio_relayOpen, 0);
    }
    //close
    if (gpio_get_level(GPIO_S_CLOSE) == 1){
        gpio_set_level(gpio_relayClose, 1);
        //beep when pin is on
        gpio_set_level(GPIO_NUM_12, 1);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_12, 0);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }else{
        gpio_set_level(gpio_relayClose, 0);
    }
}


extern "C" void app_main(void)
{

    //=================================
    //===== Variable declarations =====
    //=================================
    //variables for switching between the two gates
    bool gateSelect = false;
    bool switched = false;



    while (1){
        vTaskDelay(100 / portTICK_PERIOD_MS); //slight delay is required otherwise watchdog will be triggered
        
        //run handle function for buttons TODO: run these in another task?
        //note: the switch objects are declared and configured in config.hpp
        buttonOpen.handle();
        buttonClose.handle();


        //====== log button press/release events from evaluated switch object ======
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




        //====== switch to controlling other gate when pressing both buttons ======
        if (gpio_get_level(GPIO_S_OPEN) == 1 && gpio_get_level(GPIO_S_CLOSE) == 1 && switched == false){
            //lock
            switched = true;
            //invert selected gate
            gateSelect = !gateSelect;
            //beep buzzer
            gpio_set_level(GPIO_NUM_12, 1);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            gpio_set_level(GPIO_NUM_12, 0);
        }else{
            //release lock
            switched = false;
        }



        //======= control selected gate via open/close buttons ========
        if (gateSelect){
            //control left gate via buttons
            controlButtonGate(GPIO_K_OPEN_LEFT, GPIO_K_CLOSE_LEFT);
        }else{
            //control right gate via buttons
            controlButtonGate(GPIO_K_OPEN_RIGHT, GPIO_K_CLOSE_RIGHT);
        }



        //show debug output in certain time intervals
        if (esp_log_timestamp() - timestamp_debugOutput > 3000){
            statusReport();
        }

//        //run function that handles buttons and controls the gate
//        control();
//
//        //run handle function for gates
//        gateRight.handle();
//        gateLeft.handle();
//

    }


}
