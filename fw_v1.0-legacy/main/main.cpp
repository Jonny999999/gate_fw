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

#include "buzzer.hpp"

static const char *TAG = "main";



//==================================
//===== statusReport function ======
//==================================
//function for debugging that prints several global variables and gpio states
//to get an overview of the current system state
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


//======================================
//============ buzzer task =============
//======================================
//TODO: move the task creation to buzzer class (buzzer.cpp)
//e.g. only have function buzzer.createTask() in app_main
void task_buzzer( void * pvParameters ){
    ESP_LOGI("task_buzzer", "Start of buzzer task...");
        //run function that waits for a beep events to arrive in the queue
        //and processes them
        buzzer.processQueue();
}



//=================================
//=========== app_main ============
//=================================
//main function that gets run at startup
extern "C" void app_main(void)
{
    //==============================
    //====== startup commands ======
    //==============================
    //commands run once at controller startup
    ESP_LOGI(TAG, "Start of main function...");

    //------------------------------
    //--- create task for buzzer ---
    //------------------------------
    xTaskCreate(&task_buzzer, "task_buzzer", 2048, NULL, 5, NULL);

    //beep at startup
    buzzer.beep(3, 50, 100);

    //-----------------------------
    //--------- variables ---------
    //-----------------------------
    //count for testing beep class
    //int count = 2;

    //----------------------------
    //--------- loglevel ---------
    //----------------------------
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("buzzer", ESP_LOG_ERROR);
    esp_log_level_set("main", ESP_LOG_INFO);
    esp_log_level_set("control", ESP_LOG_INFO);
    esp_log_level_set("gate", ESP_LOG_INFO);


    //=============================
    //========= main loop =========
    //=============================
    while (1){
        vTaskDelay(10 / portTICK_PERIOD_MS); //slight delay is required otherwise watchdog will be triggered
        

        //-----------------------------
        //------- run functions -------
        //-----------------------------
        //periodicly run all functions required for controlling the gates

        //run handle function for evaluated switches
        //buttons
        buttonOpen.handle();
        buttonClose.handle();
        //remote
        remoteOpen.handle();
        remoteClose.handle();
        //TODO: declare and run these only where actually used to make use of events (not global)
        //note: the switch objects are declared in config.hpp and configured in config.cpp

        //run function that processes buttons and controls the gate
        control();

        //run handle function for gates
        gateRight.handle();
        gateLeft.handle();
        //note: gates are declared in config.hpp and configured in config.cpp


        //----------------------------
        //------- debug output -------
        //----------------------------
        //TODO: add logging and loglevel support in evaluated switch library to make the below code unnecessary
        //testing evaluated switch - log button events
        if (buttonOpen.risingEdge){
            ESP_LOGI(TAG, "== Button open pressed == - time Released: %d", buttonOpen.msReleased);
            //test beep class:
            //buzzer.beep(count, buttonOpen.msPressed, 500);
            //count++;
        }else if (buttonOpen.fallingEdge){
            ESP_LOGI(TAG, "== Button open released == - time Pressed: %d", buttonOpen.msPressed);
        }

        if (buttonClose.risingEdge){
            ESP_LOGI(TAG, "== Button close pressed == - time Released: %d", buttonClose.msReleased);
        }else if (buttonClose.fallingEdge){
            ESP_LOGI(TAG, "== Button close released == - time Pressed: %d", buttonClose.msPressed);
        }


        //testing remote receiver inputs  - log events
        if (remoteOpen.risingEdge){
            ESP_LOGW(TAG, "== Remote open on== - time Released: %d", remoteOpen.msReleased);
            //buzzer.beep(1, 300, 0);
        }else if (remoteOpen.fallingEdge){
            ESP_LOGI(TAG, "== Remote open off == - time Pressed: %d", remoteOpen.msPressed);
        }

        if (remoteClose.risingEdge){
            ESP_LOGW(TAG, "== Remote close high == - time Released: %d", remoteClose.msReleased);
            //buzzer.beep(2, 100, 0);
        }else if (remoteClose.fallingEdge){
            ESP_LOGI(TAG, "== Remote close low == - time Pressed: %d", remoteClose.msPressed);
        }




        //show debug output in certain time intervals
        if (esp_log_timestamp() - timestamp_debugOutput > 5000){
                    //statusReport();
        }



    }//end while(1)

}//end app_main
