#include "buzzer.hpp"
#include "config.hpp"

static const char *TAG_BUZZER = "buzzer";

//============================
//========== init ============
//============================
//define gpio pin as output, initialize queue
void buzzer_t::init(){
    //define buzzer pin as output
    gpio_pad_select_gpio(gpio_pin);
    gpio_set_direction(gpio_pin, GPIO_MODE_OUTPUT);
    //create queue
    beepQueue = xQueueCreate( 20, sizeof( struct beepEntry ) );
}


//=============================
//======== constructor ========
//=============================
//copy provided config parameters to private variables, run init function
buzzer_t::buzzer_t(gpio_num_t gpio_pin_f, uint16_t msGap_f){
    ESP_LOGI(TAG_BUZZER, "Initializing buzzer");
    //copy configuration parameters to variables
    gpio_pin = gpio_pin_f;
    msGap = msGap_f;
    //run init function to initialize gpio and queue
    init();
};


//============================
//=========== beep ===========
//============================
//function to add a beep command to the queue
void buzzer_t::beep(uint8_t count, uint16_t msOn, uint16_t msOff){
    //create entry struct with provided data
    struct beepEntry entryInsert = {
        count = count,
        msOn = msOn,
        msOff = msOff
    };

    // Send a pointer to a struct AMessage object.  Don't block if the
    // queue is already full.
    //struct beepEntry *entryInsertPointer;
    //entryInsertPointer = &entryInsertData;
    ESP_LOGW(TAG_BUZZER, "Inserted object to queue - count=%d, msOn=%d, msOff=%d", entryInsert.count, entryInsert.msOn, entryInsert.msOff);
    //xQueueGenericSend( beepQueue, ( void * ) &entryInsertPointer, ( TickType_t ) 0, queueSEND_TO_BACK );
     xQueueSend( beepQueue, ( void * )&entryInsert, ( TickType_t ) 0 );
}


//==============================
//======== processQueue ========
//==============================
void buzzer_t::processQueue(){
    //struct for receiving incomming events
    struct beepEntry entryRead = { };

    //loop forever
    while(1){
        ESP_LOGD(TAG_BUZZER, "processQueue: waiting for beep command");

        //if queue is ready
        if( beepQueue != 0 )
        {
            // wait for a queue entry to be available indefinetely if INCLUDE_vTaskSuspend is enabled in the FreeRTOS config
            // otherwise waits for at least 7 weeks
            if( xQueueReceive( beepQueue, &entryRead, portMAX_DELAY ) )
            {
                ESP_LOGW(TAG_BUZZER, "Read entry from queue: count=%d, msOn=%d, msOff=%d", entryRead.count, entryRead.msOn, entryRead.msOff);

                //beep requested count with requested delays
                for (int i = entryRead.count; i--;){
                    //turn on
                    ESP_LOGD(TAG_BUZZER, "turning buzzer on");
                    gpio_set_level(gpio_pin, 1);                
                    vTaskDelay(entryRead.msOn / portTICK_PERIOD_MS);
                    //turn off
                    ESP_LOGD(TAG_BUZZER, "turning buzzer off");
                    gpio_set_level(gpio_pin, 0);                
                    vTaskDelay(entryRead.msOff / portTICK_PERIOD_MS);
                }
                //wait for minimum gap between beep events
                vTaskDelay(msGap / portTICK_PERIOD_MS);
            }
        }else{ //wait for queue to become available
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
}


