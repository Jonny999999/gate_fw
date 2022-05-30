#include "gate.hpp"
#include "config.hpp"

//individual tag for logging
static const char *TAG = "gate";

//definition of string array to be able to convert state enum to readable string
const char* gateStateStr[3] = { "IDLE", "OPENING", "CLOSING" }; 


//=============================
//======== constructor ========
//=============================
//copy provided config parameters to private variables, run init function
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


//============================
//========== init ============
//============================
//function to initialize the gpio pins
void gate::init(void){
    ESP_LOGW(TAG, "Initializing gate %s", name);

    //define relays as outputs
    gpio_pad_select_gpio(gpio_relayOpen);
    gpio_set_direction(gpio_relayOpen, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio(gpio_relayClose);
    gpio_set_direction(gpio_relayClose, GPIO_MODE_OUTPUT);

    //define limit switches as inputs
    gpio_pad_select_gpio(gpio_switchOpen);
    gpio_set_direction(gpio_switchOpen, GPIO_MODE_INPUT);
    gpio_pad_select_gpio(gpio_switchClosed);
    gpio_set_direction(gpio_switchClosed, GPIO_MODE_INPUT);
}


//============================
//=========== open ===========
//============================
//function to start opening the gate for specified duration
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


//=============================
//=========== close ===========
//=============================
//function to start closing the gate for specified duration
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


//============================
//=========== stop ===========
//============================
//function to stop the gate
void gate::stop(stopReason reason){
    timestampStop = esp_log_timestamp();
    //turn off both relays
    gpio_set_level(gpio_relayClose, 0);
    gpio_set_level(gpio_relayOpen, 0);

    state = gateState::IDLE;

    //send notifications according to stop reason
    switch(reason){
        case stopReason::REACHED:
            ESP_LOGE(TAG, "Stopped gate %s - target reached", name);
            buzzer.beep(2, 50, 30);
            break;
        case stopReason::LIMIT:
            ESP_LOGE(TAG, "Stopped gate %s - limit reached", name);
            buzzer.beep(3, 50, 30);
            break;
        case stopReason::TIMEOUT:
            ESP_LOGE(TAG, "Stopped gate %s - timeout!", name);
            buzzer.beep(4, 100, 50);
            break;
        case stopReason::CANCEL:
            ESP_LOGE(TAG, "Stopped gate %s via button!", name);
            break;
    }
}


//=============================
//======== setDuration ========
//=============================
//function to update the target duration the gate should move
void gate::setDuration(uint32_t msRun_f){
    msRun = msRun_f;
}


//=============================
//=========== handle  =========
//======== statemachine =======
//=============================
//function with statemachine for a gate
//handles limit switches, timeout and target time
void gate::handle(void){
    ESP_LOGV(TAG, "handle function %s", name);
    switch(state){
        //------------------
        //----- idle -------
        //------------------
        case gateState::IDLE:
            //do nothig, wait for function open() or close() to be called
            break;

        //-------------------
        //----- opening -----
        //-------------------
        case gateState::OPENING:
            //--- target duration exceeded ---
            if (esp_log_timestamp() - timestampStart > msRun){
                ESP_LOGE(TAG, "Duration-reached gate %s", name);
                stop(stopReason::REACHED);

            //--- limit switch ---
            }else if (gpio_get_level(gpio_switchOpen) == 1){
                ESP_LOGE(TAG, "LIMIT-SWITCH gate %s", name);
                stop(stopReason::LIMIT);

            //--- timeout ---
            }else if (esp_log_timestamp() - timestampStart > msTimeout){
                ESP_LOGE(TAG, "TIMEOUT gate %s", name);
                stop(stopReason::TIMEOUT);
            }
            break;

        //-------------------
        //----- closing -----
        //-------------------
        case gateState::CLOSING:
            //--- target duration exceeded ---
            if (esp_log_timestamp() - timestampStart > msRun){
                ESP_LOGE(TAG, "Duration-reached gate %s", name);
                stop(stopReason::REACHED);

            //--- limit switch ---
            }else if (gpio_get_level(gpio_switchClosed) == 1){
                ESP_LOGE(TAG, "LIMIT-SWITCH gate %s", name);
                stop(stopReason::LIMIT);

            //--- timeout ---
            }else if (esp_log_timestamp() - timestampStart > msTimeout){
                ESP_LOGE(TAG, "TIMEOUT gate %s", name);
                stop(stopReason::TIMEOUT);
            }
            break;
    }
}

