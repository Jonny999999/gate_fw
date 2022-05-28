#include "gate.hpp"

//tag for logging
static const char *TAG = "gate";

//definition of string array to be able to convert state enum to readable string
const char* gateStateStr[3] = { "IDLE", "OPENING", "CLOSING" }; 

//constructor
gate::gate(
        gpio_num_t gpio_relayOpen_f,
        gpio_num_t gpio_relayClose_f,
        gpio_num_t gpio_switchOpen_f,
        gpio_num_t gpio_switchClosed_f,
        const char name_f[16],
        uint32_t msTimeout_f
        ){
    //copy provided configuration to private variables
    gpio_relayOpen = gpio_relayOpen_f;
    gpio_relayClose = gpio_relayClose_f;
    gpio_switchOpen = gpio_switchOpen_f;
    gpio_switchClosed = gpio_switchClosed_f;
    strcpy(name, name_f);
    msTimeout = msTimeout_f;

    //run init function which configures the gpio pins
    init();
}


//function to initialize the gpio pins
void gate::init(void){
    ESP_LOGW(TAG, "Initializing gate %s", name);
    //define relays as outputs
    gpio_set_direction(gpio_relayOpen, GPIO_MODE_OUTPUT);
    gpio_set_direction(gpio_relayClose, GPIO_MODE_OUTPUT);
    //define limit switches as inputs
    gpio_set_direction(gpio_switchOpen, GPIO_MODE_INPUT);
    gpio_set_direction(gpio_switchClosed, GPIO_MODE_INPUT);
}


//function to start opening the gate
void gate::open(uint32_t msRun_f){
    ESP_LOGW(TAG, "=> Opening gate %s", name);
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
    ESP_LOGW(TAG, "=> Closing gate %s", name);
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
    ESP_LOGW(TAG, "=> Stopping gate %s", name);
    timestampStop = esp_log_timestamp();
    //turn off both relays
    gpio_set_level(gpio_relayClose, 0);
    gpio_set_level(gpio_relayOpen, 0);

    state = gateState::IDLE;
}


//function to update the target duration the gate should move
void gate::setDuration(uint32_t msRun_f){
    msRun = msRun_f;
}


//function with statemachine for a gate
//(handles limit switches, timeout and target time)
void gate::handle(void){
    ESP_LOGV(TAG, "handle function %s", name);
    switch(state){
        case gateState::IDLE:
            //do nothig, wait for function open() or close() to be called
            break;

        case gateState::OPENING:
            if (esp_log_timestamp() - timestampStart > msRun){ //target run duration exceeded
                ESP_LOGE(TAG, "Duration-reached gate %s", name);
                stop();
            }else if (gpio_get_level(gpio_switchOpen) == 1){ //limit switch (completely open)
                ESP_LOGE(TAG, "LIMIT-SWITCH gate %s", name);
                stop();
            }else if (esp_log_timestamp() - timestampStart > msTimeout){ //timeout
                ESP_LOGE(TAG, "TIMEOUT gate %s", name);
                stop();
            }
            break;

        case gateState::CLOSING:
            if (esp_log_timestamp() - timestampStart > msRun){ //target run duration exceeded
                ESP_LOGE(TAG, "Duration-reached gate %s", name);
                stop();
            }else if (gpio_get_level(gpio_switchClosed) == 1){ //limit switch (completely closed)
                ESP_LOGE(TAG, "LIMIT-SWITCH gate %s", name);
                stop();
            }else if (esp_log_timestamp() - timestampStart > msTimeout){ //timeout
                ESP_LOGE(TAG, "TIMEOUT gate %s", name);
                stop();
            }

            break;
    }
}

