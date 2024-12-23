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
    if (state == gateState::IDLE){
        ESP_LOGW(TAG, "%s - => Opening", name);
        //turn off close-relay
        gpio_set_level(gpio_relayClose, 0);
        //turn on open-relay
        gpio_set_level(gpio_relayOpen, 1);
        timestampStart = esp_log_timestamp();
        //set target time the motor should run
        msRun = msRun_f; //default = msTimeout

        changeState(gateState::OPEN_START);
        //store direction for other functions
        lastDirection = gateDirection::OPEN;
    } else {
        ESP_LOGE(TAG, "%s - cant start opening, still in state [%s]", name, gateStateStr[(int)state]);
    }
}


//=============================
//=========== close ===========
//=============================
//function to start closing the gate for specified duration
void gate::close(uint32_t msRun_f){
    if (state == gateState::IDLE){
        ESP_LOGW(TAG, "%s - => Closing", name);
        //turn off open-relay
        gpio_set_level(gpio_relayOpen, 0);
        //turn on close-relay
        gpio_set_level(gpio_relayClose, 1);
        timestampStart = esp_log_timestamp();
        //set target time the motor should run
        msRun = msRun_f; //default = msTimeout

        changeState(gateState::CLOSE_START);
        //store direction for other functions
        lastDirection = gateDirection::CLOSE;
    } else {
        ESP_LOGE(TAG, "%s - cant start closing, still in state [%s]", name, gateStateStr[(int)state]);
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
            ESP_LOGW(TAG, "%s - STOPPED, target reached", name);
            buzzer.beep(2, 50, 30);
            changeState(gateState::WAIT_LOCK);
            break;
        case stopReason::LIMIT:
            ESP_LOGW(TAG, "%s - STOPPED, limit reached", name);
            buzzer.beep(3, 50, 30);
            changeState(gateState::WAIT_LOCK);
            break;
        case stopReason::TIMEOUT:
            ESP_LOGE(TAG, "%s - STOPPED, timeout!", name);
            buzzer.beep(4, 100, 50);
            changeState(gateState::WAIT_LOCK);
            break;
        case stopReason::CANCEL:
            ESP_LOGE(TAG, "%s - STOPPED, via button!", name);
            changeState(gateState::WAIT_LOCK);
            break;
        case stopReason::RETRY:
            ESP_LOGE(TAG, "%s - STOPPED for retry", name);
            changeState(gateState::WAIT_RETRY);
            break;
    }
    ESP_LOGI(TAG, "%s - [%s] waiting for standstill...", name, gateStateStr[(int)state]);
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
    ESP_LOGI(TAG, "%s - changed run duration (msRun) from %d to %d", name, msRun, msRun_f);
    msRun = msRun_f;
}


//=============================
//======== changeState ========
//=============================
//function that simply updates the state variable and sends log output if the state actually changed
void gate::changeState(gateState stateNew){
    //only proceed when state actually changed
    if (state == stateNew) {
        return; //already at target state -> nothing to do
    }
    //log state change
    ESP_LOGW(TAG, "%s - changed state from [%s] to [%s]",
            name, gateStateStr[(int)state], gateStateStr[(int)stateNew]);
    //update state
    state = stateNew;
}


//===============================
//===== handleStopCondition =====
//===============================
//function that checks the stop conditions for either opening or closing gate and initiates stop if necessary
//returns TRUE if stop got initiated
bool gate::handleStopCondition(gateDirection direction){
    //--- target duration exceeded ---
    if (esp_log_timestamp() - timestampStart > msRun){
        stop(stopReason::REACHED);
    } 
    //--- timeout ---
    else if (esp_log_timestamp() - timestampStart > msTimeout){
        stop(stopReason::TIMEOUT);
    } 
    //--- limit switch open ---
    else if ( (direction == gateDirection::OPEN)
            && (gpio_get_level(gpio_switchOpen) == 1) ){
        stop(stopReason::LIMIT);
    }
    //--- limit switch closed ---
    else if ( (direction == gateDirection::CLOSE)
            && (gpio_get_level(gpio_switchClosed) == 1) ){
        stop(stopReason::LIMIT);
    }
    //--- no stop condiction applies ---
    else {
        return false; //no action taken
    }
    return true; //stop initiated
}



//=============================
//=========== handle  =========
//======== statemachine =======
//=============================
//function with statemachine for a gate
//handles limit switches, timeout and target time
void gate::handle(void){
    ESP_LOGV(TAG, "%s - running handle function", name);
    bool stopped = false;
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
            //handle target-reached, timout and limit-switch
            stopped = handleStopCondition(gateDirection::OPEN);
            //--- away from closed position ---
            if (!stopped && gpio_get_level(gpio_switchClosed) == 0){ //TODO debounce switch
                changeState(gateState::OPEN_MOVING);
            }
            break;

        case gateState::OPEN_MOVING:
            //handle target-reached, timout and limit-switch
            stopped = handleStopCondition(gateDirection::OPEN);
            //--- reached wrong limit switch ---
            if (!stopped && gpio_get_level(gpio_switchClosed) == 1){
                ESP_LOGE(TAG, "%s - WRONG LIMIT SWITCH", name);
                buzzer.beep(2, 1000, 100);
                retry(); //stop, wait, restart opening
            }
            break;

        //-------------------
        //----- closing -----
        //-------------------
        case gateState::CLOSE_START:
            //handle target-reached, timout and limit-switch
            stopped = handleStopCondition(gateDirection::CLOSE);
            //--- away from open position ---
            if (!stopped && gpio_get_level(gpio_switchOpen) == 0){ //TODO debounce switch
                changeState(gateState::CLOSE_MOVING);
            }
            break;

        case gateState::CLOSE_MOVING:
            //handle target-reached, timout and limit-switch
            stopped = handleStopCondition(gateDirection::CLOSE);
            //--- reached wrong limit switch ---
            if (!stopped && gpio_get_level(gpio_switchOpen) == 1){
                ESP_LOGE(TAG, "%s - WRONG LIMIT SWITCH", name);
                buzzer.beep(2, 1000, 500);
                retry(); //stop, wait, restart closing
            }
            break;
            
        //-------------------
        //----- waiting -----
        //-------------------
        //wait some time for gates to stop moving and restart
        case gateState::WAIT_RETRY: //TODO: delay necessary? maybe restart immedeately at limit switch event
            if (esp_log_timestamp() - timestampStop > msRetry) {
                changeState(gateState::IDLE);
                switch (lastDirection){
                    case gateDirection::OPEN:
                        open(msRun);
                        break;
                    case gateDirection::CLOSE:
                        close(msRun);
                }
                ESP_LOGI(TAG, "%s - done waiting %dms for stop of movement (retry delay)", name, msRetry);
            }
            break;

        //wait some time for gates to stop moving and switch to IDLE
        case gateState::WAIT_LOCK:
            if (esp_log_timestamp() - timestampStop > msStop) {
                changeState(gateState::IDLE);
                ESP_LOGI(TAG, "%s - done waiting %dms for stop of movement (stop delay)", name, msStop);
            }
            break;
    }
}

