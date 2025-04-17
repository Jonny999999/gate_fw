#include "gate.hpp"
#include <esp_timer.h>



// Gate state strings for logging
const char *GateState_str [] = {
    "IDLE_FULLY_OPEN", 
    "IDLE_FULLY_CLOSED",
    "IDLE_PARTIALLY_OPEN",
    "WAITING_FOR_VFD_STARTUP",
    "MOVING_OPENING",
    "MOVING_CLOSING",
    "PAUSED_STATE",
    "ERROR_STATE"
};


Gate::Gate(const char *name,
           gpio_num_t kLimitSwitchOpenGpio,
           bool kLimitSwitchOpen_ActiveLevel,
           gpio_num_t kLimitSwitchClosedGpio,
           bool kLimitSwitchClosed_ActiveLevel,
           gpio_num_t kRelayPinGpio,
           VFD *vfd,
           buzzer_t *buzzer,
           uint32_t runDurationMs) : name(name),
                                     kLimitSwitchOpenGpio(kLimitSwitchOpenGpio),
                                     kLimitSwitchOpen_ActiveLevel(kLimitSwitchOpen_ActiveLevel),
                                     kLimitSwitchClosedGpio(kLimitSwitchClosedGpio),
                                     kLimitSwitchClosed_ActiveLevel(kLimitSwitchClosed_ActiveLevel),
                                     kRelayPinGpio(kRelayPinGpio),
                                     vfd(vfd),
                                     buzzer(buzzer),
                                     runDurationMs(runDurationMs),

                                     state(IDLE_FULLY_CLOSED), relayTimeoutActive(false), relayOn(false), positionPercent(0.0f)
{
    gpio_set_direction(kLimitSwitchOpenGpio, GPIO_MODE_INPUT);
    gpio_set_direction(kLimitSwitchClosedGpio, GPIO_MODE_INPUT);
    gpio_set_direction(kRelayPinGpio, GPIO_MODE_OUTPUT);
    // Initialize previous switch states.
    prevOpenSwitchState = checkLimitSwitchOpenActive();
    prevClosedSwitchState = checkLimitSwitchClosedActive();
    ESP_LOGW(name, "GPIO pins and variables initialized.");
    // initially start VFD immediately (will likely open?)
    startRelay();
}



void Gate::startRelay() {
    if (!relayOn) {
        ESP_LOGI(name, "Turning relay on...");
        gpio_set_level(kRelayPinGpio, 1);
        timestampRelayTurnedOnUs = esp_timer_get_time();
        relayOn = true;
        //ESP_LOGI(name, "Waiting %d ms for VFD to boot up...", DELAY_VFD_STARTUP);
        //state = WAITING_FOR_VFD_STARTUP;
        // Delay to allow the VFD to boot up.
        //vTaskDelay(pdMS_TO_TICKS(DELAY_VFD_STARTUP));
        // note: optimized to not blocking delay, waiting in state WAITING_FOR_VFD_STARTUP
    }
    lastActivityTimestampUs = esp_timer_get_time();
    relayTimeoutActive = false;
}


void Gate::softStopRelay() {
    relayTimeoutActive = true;
    lastActivityTimestampUs = esp_timer_get_time();
    ESP_LOGI(name, "Relay soft stop initiated.");
}


void Gate::forceStopRelay() {
    gpio_set_level(kRelayPinGpio, 0);
    relayOn = false;
    relayTimeoutActive = false;
    ESP_LOGW(name, "Relay forced OFF.");
}



void Gate::updatePosition() {
    uint64_t currentTimeUs = esp_timer_get_time();
    uint64_t elapsed = currentTimeUs - lastPositionUpdateTimestampUs;
    float deltaPercent = (elapsed / (float)(runDurationMs * 1000)) * 100.0f;
    if (state == MOVING_OPENING) {
        positionPercent = std::min(100.0f, positionPercent + deltaPercent);
    } else if (state == MOVING_CLOSING) {
        positionPercent = std::max(0.0f, positionPercent - deltaPercent);
    }
    lastPositionUpdateTimestampUs = currentTimeUs;
    ESP_LOGD(name, "Position updated: %.2f%% - timeElapsed=%lld -> deltaPercent=%f", positionPercent, elapsed, deltaPercent);
}



bool Gate::checkLimitSwitchOpenActive() {
    bool current = (gpio_get_level(kLimitSwitchOpenGpio) == kLimitSwitchOpen_ActiveLevel);
    if (current != prevOpenSwitchState) {
        ESP_LOGI(name, "Open limit switch changed to %s.", current ? "ACTIVE" : "INACTIVE");
        prevOpenSwitchState = current;
    }
    return current;
}


bool Gate::checkCurrentLimitExceeded() {
    float vfdCurrent;
    vfd->getCurrent(&vfdCurrent);
    ESP_LOGD(name, "VFD current: %05.2f A", vfdCurrent);
    return (vfdCurrent > kCurrentLimitAmpere);
}



bool Gate::checkLimitSwitchClosedActive() {
    bool current = (gpio_get_level(kLimitSwitchClosedGpio) == kLimitSwitchClosed_ActiveLevel);
    if (current != prevClosedSwitchState) {
        ESP_LOGI(name, "Closed limit switch changed to %s.", current ? "ACTIVE" : "INACTIVE");
        prevClosedSwitchState = current;
    }
    return current;
}



// Private helper to start movement in a given direction.
void Gate::startMovement(bool opening) {
    ESP_LOGI(name, "Starting movement dir=%s...", opening ? "OPENING" : "CLOSING");
    // when relay is off we need to wait for vfd startup first...
    nextDirection = opening; // store target direction (used in case of waiting for VFD or when pause/resuming)
    if (!relayOn || ((esp_timer_get_time() - timestampRelayTurnedOnUs) < DELAY_VFD_STARTUP)) {
        startRelay();
        ESP_LOGI(name, "Waiting %d ms for VFD to boot up in state WAITING_FOR_VFD_STARTUP first...", DELAY_VFD_STARTUP);
        state = WAITING_FOR_VFD_STARTUP;
        return;
    }

    lastPositionUpdateTimestampUs = timestampStartUs;
    vfd->setFrequency(kDefaultVfdFrequency);
    esp_err_t err = vfd->start(opening); // start motor in desired direction
    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        ESP_LOGE(name, "VFD starting failed! switching to ERROR_STATE");
        buzzer->beep(5, 70, 100);
        // switch to ERROR state to prevent infinite loop of retries
        state = ERROR_STATE;
        return;
    }
    #endif
    timestampStartUs = esp_timer_get_time();

    if (opening) {
        state = MOVING_OPENING;
        ESP_LOGI(name, "Started opening movement.");
    } else {
        state = MOVING_CLOSING;
        ESP_LOGI(name, "Started closing movement.");
    }
}


// TODO: handle direct direction change - in start methods check current state, stop first

// Public method: runTo target percentage.
void Gate::runTo(float target) {
    ESP_LOGI(name, "Received command: running to pos %f %%.", target);
    // If target is 0 or 100, force full movement by using the full timeout.
    if (target <= 0.0f) {
        target = 0.0f;
        targetRunTimeMs = kMovingTimeout;
    } else if (target >= 100.0f) {
        target = 100.0f;
        targetRunTimeMs = kMovingTimeout ;
    } else {
        float delta = std::abs(target - positionPercent);
        targetRunTimeMs = (uint64_t)((delta / 100.0f) * runDurationMs );
    }
    if (target > positionPercent) {
        startMovement(true);
        ESP_LOGI(name, "Running to %.2f%% (opening). Target run time: %llu ms.", target, targetRunTimeMs);
    } else if (target < positionPercent) {
        startMovement(false);
        ESP_LOGI(name, "Running to %.2f%% (closing). Target run time: %llu ms.", target, targetRunTimeMs);
    } else {
        ESP_LOGW(name, "Already at target position (%.2f%%).", target);
    }
}



// Public method: open for a specified duration.
void Gate::openForMs(uint32_t ms) {
    ESP_LOGI(name, "Received command: Open for %ld ms.", ms);
    if(checkLimitSwitchOpenActive()){
        ESP_LOGE(name, "Received open command, but Limit-switch-open (pos=%.1f%%) - can not open further...", positionPercent);
        return;
    }
    targetRunTimeMs = ms;
    startMovement(true);
}


// Public method: open completely.
void Gate::openCompletely() {
    ESP_LOGI(name, "Received command: Open completely");
    if(checkLimitSwitchOpenActive()){
        ESP_LOGE(name, "Received open command, but Limit-switch-open (pos=%.1f%%) - can not open further...", positionPercent);
        return;
    }
    targetRunTimeMs = runDurationMs + 5000; // ensure gate gets fully opened with running for longer than necessary (stops at limit switch)
    startMovement(true);
}


// Public method: close for a specified duration.
void Gate::closeForMs(uint32_t ms) {
    ESP_LOGI(name, "Received command: Closing for %ld ms.", ms);
    if(checkLimitSwitchClosedActive()){
        ESP_LOGE(name, "Received close command, but Limit-switch-closed active and at %.1f%% - can not close further...", positionPercent);
        return;
    }
    targetRunTimeMs = ms ;
    startMovement(false);
}



// Public method: close completely.
void Gate::closeCompletely() {
    ESP_LOGI(name, "Received command: Close completely");
    if(checkLimitSwitchClosedActive()){
        ESP_LOGE(name, "Received close command, but Limit-switch-closed active and at %.1f%% - can not close further...", positionPercent);
        return;
    }
    targetRunTimeMs = runDurationMs + 5000; // ensure gate gets fully closed with running for longer than necessary (stops at limit switch)
    startMovement(false);
}


// Public method: update target run time e.g. while already opening
void Gate::updateTargetRunTime(uint32_t ms) {
    ESP_LOGI(name, "Received command: To update target run duration from %lld to %ld", targetRunTimeMs, ms);
    // TODO some checks required? only update when running? limit to range?
    targetRunTimeMs = ms;
}


void Gate::stop(bool forceStatePartialOpen){ // default true
    ESP_LOGI(name, "stopping gate, turning off motor (current state=%s)", GateState_str[(int)state]);
    esp_err_t err = vfd->stop();
    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        ESP_LOGE(name, "VFD stop failed! -> forcing relay off");
        buzzer->beep(5, 70, 100);
        forceStopRelay();
        state = ERROR_STATE;
        return;
    }
    #endif
    // buzzer->beep(1, 100, 200);
    softStopRelay();

    // by default set state to partially open, when stopped due to limit switch the state is set manually
    if (forceStatePartialOpen)
        state = IDLE_PARTIALLY_OPEN;

    // Do not change state here; let the state machine (handle()) update it.
}



// Public method: pause movement
void Gate::pause() {
    if (getIsMoving() == false) {
        ESP_LOGE(name, "Pause requested, but gate is not moving.");
        return;
    }
    ESP_LOGW(name, "Pause requested, stopping gate...");
    updatePosition();  // Record accurate position before pausing
    stop();
    wasOpeningBeforePause = (state == MOVING_OPENING);
    state = PAUSED_STATE;
    pauseStartTimestampUs = esp_timer_get_time();
    softStopRelay();
    ESP_LOGW(name, "Gate PAUSED. Remaining time to target: %llu ms", 
             (targetRunTimeMs * 1000 - (pauseStartTimestampUs - timestampStartUs)) / 1000);
}

// Public method: resume movement
void Gate::resume() {
    if (state != PAUSED_STATE) {
        ESP_LOGW(name, "Resume requested, but gate is not paused -> ignoring");
        return;
    }
    uint64_t currentTimeUs = esp_timer_get_time();
    uint64_t elapsedSinceStart = pauseStartTimestampUs - timestampStartUs;
    targetRunTimeMs = (targetRunTimeMs * 1000 - elapsedSinceStart) / 1000;
    if (targetRunTimeMs <= 0) {
        ESP_LOGW(name, "No remaining run time, cannot resume -> switching to idle");
        stop(false);
        state = IDLE_PARTIALLY_OPEN;
        return;
    }
    ESP_LOGI(name, "Resuming gate movement. Remaining target run time: %llu ms", targetRunTimeMs);
    startMovement(wasOpeningBeforePause);
}

// Public method: cancel movement
void Gate::cancel() {
    ESP_LOGI(name, "Cancel requested. Stopping gate immediately");
    stop(true);
    state = IDLE_PARTIALLY_OPEN;
}





void Gate::handle() {
    // debug logging
    ESP_LOGV(name, "handle() - state=%s,  pos=%.2f, limitOpen=%d, limitClosed=%d", GateState_str[(int)state], positionPercent, checkLimitSwitchOpenActive(), checkLimitSwitchClosedActive());

    uint64_t currentTimeUs = esp_timer_get_time();

    // Check for relay inactivity in idle states. TODO: verify in idle states?
    if (relayTimeoutActive && (currentTimeUs - lastActivityTimestampUs > kRelayInactivityTimeoutMs * 1000))
    {
        ESP_LOGW(name, "Relay inactivity timeout of %lds reached. Forcing relay OFF...", kRelayInactivityTimeoutMs / 1000);
        forceStopRelay();
    }

    // Main state machine for all gate states.
    switch (state)
    {
    case MOVING_OPENING:
    {
        updatePosition();
        // timeout
        if ((currentTimeUs - timestampStartUs) >= (kMovingTimeout * 1000))
        {
            ESP_LOGE(name, "Open movement timeout exceeded.");
            stop(false);
            state = ERROR_STATE;
            break;
        }
        // limit reached
        else if (checkLimitSwitchOpenActive())
        {
            ESP_LOGI(name, "Open limit switch triggered.");
            positionPercent = 100.0f; // Calibrate position.
            stop(false);
            state = IDLE_FULLY_OPEN;
            break;
        }
        // target reached
        else if ((currentTimeUs - timestampStartUs) >= targetRunTimeMs * 1000)
        {
            ESP_LOGI(name, "Target run time reached (while opening).");
            stop(false);
            state = IDLE_PARTIALLY_OPEN;
        }
        break;
    }
    case MOVING_CLOSING:
    {
        updatePosition();
        #ifdef LOG_VFD_CURRENT_WHEN_CLOSING
            float vfdCurrent;
            vfd->getCurrent(&vfdCurrent);
            ESP_LOGI(name, "Closing... VFD current: %05.2f A", vfdCurrent);
        #endif

        // timeout
        if ((currentTimeUs - timestampStartUs) >= (kMovingTimeout * 1000))
        {
            ESP_LOGE(name, "Close movement timeout exceeded.");
            stop(false);
            state = ERROR_STATE;
            break;
        }
        // limit reached
        else if (checkLimitSwitchClosedActive())
        {
            ESP_LOGI(name, "Close limit switch triggered.");
            positionPercent = 0.0f; // Calibrate position.
            stop(false);
            state = IDLE_FULLY_CLOSED;
            break;
        }
        // target reached
        else if ((currentTimeUs - timestampStartUs) >= targetRunTimeMs * 1000)
        {
            ESP_LOGI(name, "Target run time reached (while closing).");
            stop(false);
            state = IDLE_PARTIALLY_OPEN;
        }
        #ifdef CURRENT_MONITORING_ENABLED
        else if (checkCurrentLimitExceeded()){
            ESP_LOGE(name, "Max vfd current exceeded while closing! stopping");
            stop(false);
            buzzer->beep(1, 1500, 100);
            state = ERROR_STATE;
        }
        #endif
        break;
    }
    case IDLE_FULLY_OPEN:
        // check if gate was "manually moved away from limit"
        if (!checkLimitSwitchOpenActive())
        {
            state = IDLE_PARTIALLY_OPEN;
            positionPercent = 99;
            ESP_LOGI(name, "Gate moved away from fully open position -> switching to IDLE_PARTIALLY_OPEN");
            #ifdef BEEP_AT_LIMIT_SW_CHANGE
            buzzer->beep(1, 100, 50);
            #endif
        }
        break;
    case IDLE_FULLY_CLOSED:
        // check if gate was "manually moved away from limit"
        if (!checkLimitSwitchClosedActive())
        {
            state = IDLE_PARTIALLY_OPEN;
            positionPercent = 1;
            ESP_LOGW(name, "Gate moved away from fully closed position while off -> switching to IDLE_PARTIALLY_OPEN");
            #ifdef BEEP_AT_LIMIT_SW_CHANGE
            buzzer->beep(1, 100, 50);
            #endif
        }
        break;
    case IDLE_PARTIALLY_OPEN:
        // update state when gate was "manually moved to limit"
        if (checkLimitSwitchOpenActive())
        {
            state = IDLE_FULLY_OPEN;
            positionPercent = 100.0f;
            ESP_LOGW(name, "Gate reached fully open position while off -> switching to IDLE_FULLY_OPEN");
            #ifdef BEEP_AT_LIMIT_SW_CHANGE
            buzzer->beep(1, 100, 50);
            #endif

        }
        else if (checkLimitSwitchClosedActive())
        {
            state = IDLE_FULLY_CLOSED;
            positionPercent = 0.0f;
            ESP_LOGW(name, "Gate reached fully closed position while off -> switching to IDLE_FULLY_CLOSED");
            #ifdef BEEP_AT_LIMIT_SW_CHANGE
            buzzer->beep(1, 100, 50);
            #endif
        }
        break;
    case WAITING_FOR_VFD_STARTUP:
        if (currentTimeUs - timestampRelayTurnedOnUs > DELAY_VFD_STARTUP * 1000){
            ESP_LOGI(name, "VFD startup delay passed, starting motor");
            startMovement(nextDirection);
        }
        break;

    case PAUSED_STATE:
        // exit paused state after certain timeout
        if ((esp_timer_get_time() - pauseStartTimestampUs)/1000 > PAUSED_SWITCH_TO_IDLE_TIMEOUT_MS ){
            ESP_LOGE(name, "exceeded max time in PAUSED_STATE (%d ms) -> switching to IDLE", PAUSED_SWITCH_TO_IDLE_TIMEOUT_MS/1000);
            state = IDLE_PARTIALLY_OPEN;
        }
        break;

    case ERROR_STATE:
        ESP_LOGE(name, "currently in ERROR state, TODO how to use / recover from here? ==> switching to IDLE_PARTIALLY_OPEN for now...");
        state = IDLE_PARTIALLY_OPEN;
        // Nothing further to do.
        break;
    }
}


