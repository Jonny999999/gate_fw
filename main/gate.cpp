#include "gate.hpp"
#include "config.hpp"

//individual tag for logging
static const char *TAG = "gate";

//definition of string array to be able to convert state enum to readable string
const char* gateStateStr[7] = { "IDLE", "OPEN_START", "OPEN_MOVING", "CLOSE_START", "CLOSE_MOVING", "WAIT_RETRY", "WAIT_LOCK" }; 


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
        uint32_t msStop_f,
        uint32_t msRetry_f,
        uint32_t msTimeout_f
        ){
    //copy provided configuration to private variables
    gpio_relayOpen = gpio_relayOpen_f;
    gpio_relayClose = gpio_relayClose_f;
    gpio_switchOpen = gpio_switchOpen_f;
    gpio_switchClosed = gpio_switchClosed_f;
    strcpy(name, name_f);
    msStop = msStop_f;
    msRetry = msRetry_f;
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

        state = gateState::OPEN_START;
        //store direction for other functions
        lastDirection = gateDirection::OPEN;
    } else {
        ESP_LOGE(TAG, "gate %s - cant start opening, still in state %s", name, gateStateStr[(int)state]);
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

        state = gateState::CLOSE_START;
        //store direction for other functions
        lastDirection = gateDirection::CLOSE;
    } else {
        ESP_LOGE(TAG, "gate %s - cant start closing, still in state %s", name, gateStateStr[(int)state]);
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

    //TODO: check if waiting is necessary at every stop reason
    //e.g. switch directly to IDLE when reason is LIMIT

    //send notifications according to stop reason
    switch(reason){
        case stopReason::REACHED:
            ESP_LOGE(TAG, "Stopped gate %s - target reached", name);
            buzzer.beep(2, 50, 30);
            state = gateState::WAIT_LOCK;
            break;
        case stopReason::LIMIT:
            ESP_LOGE(TAG, "Stopped gate %s - limit reached", name);
            buzzer.beep(3, 50, 30);
            state = gateState::WAIT_LOCK;
            break;
        case stopReason::TIMEOUT:
            ESP_LOGE(TAG, "Stopped gate %s - timeout!", name);
            buzzer.beep(4, 100, 50);
            state = gateState::WAIT_LOCK;
            break;
        case stopReason::CANCEL:
            ESP_LOGE(TAG, "Stopped gate %s via button!", name);
            state = gateState::WAIT_LOCK;
            break;
        case stopReason::RETRY:
            ESP_LOGE(TAG, "Stopped gate %s and switch to WAIT_RETRY for RETRY run", name);
            state = gateState::WAIT_RETRY;
            break;
    }
    ESP_LOGI(TAG, "gate %s - waiting for standstill...", name);
}


//============================
//========== retry ===========
//============================
//function that stopps the gate, waits and starts the last command again
void gate::retry(){
    //stop movement and switch to WAIT_RETRY state
    stop(stopReason::RETRY);
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
        case gateState::OPEN_START:
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

            //--- away from closed position ---
            }else if (gpio_get_level(gpio_switchClosed) == 0){ //TODO debounce switch
                state = gateState::OPEN_MOVING;
            }
            break;

        case gateState::OPEN_MOVING:
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
                
            //--- reached wrong limit switch ---
            }else if (gpio_get_level(gpio_switchClosed) == 1){
                ESP_LOGE(TAG, "WRONG LIMIT SWITCH gate %s", name);
                buzzer.beep(2, 1000, 100);
                retry(); //stop, wait, restart opening
            }
            break;

        //-------------------
        //----- closing -----
        //-------------------
        case gateState::CLOSE_START:
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

            //--- away from open position ---
            }else if (gpio_get_level(gpio_switchOpen) == 0){ //TODO debounce switch
                state = gateState::CLOSE_MOVING;
            }
            break;

        case gateState::CLOSE_MOVING:
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
            
            //--- reached wrong limit switch ---
            }else if (gpio_get_level(gpio_switchOpen) == 1){
                ESP_LOGE(TAG, "WRONG LIMIT SWITCH gate %s", name);
                buzzer.beep(2, 1000, 500);
                retry(); //stop, wait, restart closing
            }
            break;
            
        //-------------------
        //----- waiting -----
        //-------------------
        case gateState::WAIT_RETRY: //wait some time for gates to stop moving
            if (esp_log_timestamp() - timestampStop > msRetry) {
                state = gateState::IDLE;
                switch (lastDirection){
                    case gateDirection::OPEN:
                        open(msRun);
                        break;
                    case gateDirection::CLOSE:
                        close(msRun);
                }
                ESP_LOGW(TAG, "gate %s: done waiting for %dms stop of movement - WAIT_RETRY -> open/close", name, msRetry);
            }
            break;
        case gateState::WAIT_LOCK: //wait some time for gates to stop moving
            if (esp_log_timestamp() - timestampStop > msStop) {
                state = gateState::IDLE;
                ESP_LOGW(TAG, "gate %s: done waiting for stop of movement - WAIT_LOCK -> IDLE", name);
            }
            break;
    }
}

