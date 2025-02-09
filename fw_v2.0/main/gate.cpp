#include "gate.hpp"
#include <esp_timer.h>

#define IGNORE_VFD_ERROR

// Gate state strings for logging
const char *GateState_str [] = {
    "IDLE_FULLY_OPEN", 
    "IDLE_FULLY_CLOSED",
    "IDLE_PARTIALLY_OPEN",
    "MOVING_OPENING",
    "MOVING_CLOSING",
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
}



void Gate::startRelay() {
    if (!relayOn) {
        ESP_LOGI(name, "Turning relay on...");
        gpio_set_level(kRelayPinGpio, 1);
        relayOn = true;
        ESP_LOGI(name, "Waiting %d ms for VFD to boot up...", DELAY_VFD_STARTUP);
        // Delay to allow the VFD to boot up.
        vTaskDelay(pdMS_TO_TICKS(DELAY_VFD_STARTUP));
    }
    lastActivityTimestamp = esp_timer_get_time();
    relayTimeoutActive = false;
}


void Gate::softStopRelay() {
    relayTimeoutActive = true;
    lastActivityTimestamp = esp_timer_get_time();
    ESP_LOGI(name, "Relay soft stop initiated.");
}


void Gate::forceStopRelay() {
    gpio_set_level(kRelayPinGpio, 0);
    relayOn = false;
    relayTimeoutActive = false;
    ESP_LOGW(name, "Relay forced OFF.");
}



void Gate::updatePosition() {
    uint64_t currentTime = esp_timer_get_time();
    uint64_t elapsed = currentTime - lastPositionUpdateTimestamp;
    float deltaPercent = (elapsed / (float)(runDurationMs * 1000)) * 100.0f;
    if (state == MOVING_OPENING) {
        positionPercent = std::min(100.0f, positionPercent + deltaPercent);
    } else if (state == MOVING_CLOSING) {
        positionPercent = std::max(0.0f, positionPercent - deltaPercent);
    }
    lastPositionUpdateTimestamp = currentTime;
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
    startRelay();
    lastPositionUpdateTimestamp = timestampStart;
    vfd->setFrequency(kDefaultVfdFrequency);
    esp_err_t err = vfd->start(opening); // start motor in desired direction
    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        ESP_LOGE(name, "VFD starting failed!");
        buzzer->beep(5, 100, 200);
        //state = ERROR_STATE;
        return;
    }
    #endif
    timestampStart = esp_timer_get_time();

    if (opening) {
        state = MOVING_OPENING;
        ESP_LOGI(name, "Started opening movement.");
    } else {
        state = MOVING_CLOSING;
        ESP_LOGI(name, "Started closing movement.");
    }
}



// Public method: runTo target percentage.
void Gate::runTo(float target) {
    ESP_LOGI(name, "Received command: running to pos %f %%.", target);
    // If target is 0 or 100, force full movement by using the full timeout.
    if (target <= 0.0f) {
        target = 0.0f;
        targetRunTime = kMovingTimeout * 1000;
    } else if (target >= 100.0f) {
        target = 100.0f;
        targetRunTime = kMovingTimeout * 1000;
    } else {
        float delta = std::abs(target - positionPercent);
        targetRunTime = (uint64_t)((delta / 100.0f) * runDurationMs * 1000);
    }
    if (target > positionPercent) {
        startMovement(true);
        ESP_LOGI(name, "Running to %.2f%% (opening). Target run time: %llu us.", target, targetRunTime);
    } else if (target < positionPercent) {
        startMovement(false);
        ESP_LOGI(name, "Running to %.2f%% (closing). Target run time: %llu us.", target, targetRunTime);
    } else {
        ESP_LOGW(name, "Already at target position (%.2f%%).", target);
    }
}



// Public method: open for a specified duration.
void Gate::openForMs(uint32_t ms) {
    ESP_LOGI(name, "Received command: Opening for %ld ms.", ms);
    if(checkLimitSwitchOpenActive()){
        ESP_LOGE(name, "Received open command, but Limit-switch-open (pos=%.1f%%) - can not open further...", positionPercent);
        return;
    }
    targetRunTime = ms * 1000;
    startMovement(true);
}



// Public method: close for a specified duration.
void Gate::closeForMs(uint32_t ms) {
    ESP_LOGI(name, "Received command: Closing for %ld ms.", ms);
    if(checkLimitSwitchClosedActive()){
        ESP_LOGE(name, "Received close command, but Limit-switch-closed active and at %.1f%% - can not close further...", positionPercent);
        return;
    }
    targetRunTime = ms * 1000;
    startMovement(false);
}



void Gate::stop() {
    ESP_LOGI(name, "stopping gate, turning off motor (current state=%s)", GateState_str[(int)state]);
    esp_err_t err = vfd->stop();
    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        ESP_LOGE(name, "VFD stop failed! -> forcing relay off");
        buzzer->beep(5, 100, 200);
        forceStopRelay();
        state = ERROR_STATE;
        return;
    }
    #endif
    buzzer->beep(1, 100, 200);
    softStopRelay();
    // Do not change state here; let the state machine (handle()) update it.
}



void Gate::handle() {
    // debug logging
    ESP_LOGV(name, "handle() - state=%s,  pos=%.2f, limitOpen=%d, limitClosed=%d", GateState_str[(int)state], positionPercent, checkLimitSwitchOpenActive(), checkLimitSwitchClosedActive());

    uint64_t currentTime = esp_timer_get_time();

    // Check for relay inactivity in idle states. TODO: verify in idle states?
    if (relayTimeoutActive && (currentTime - lastActivityTimestamp > kRelayInactivityTimeoutMs * 1000))
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
        if ((currentTime - timestampStart) >= (kMovingTimeout * 1000))
        {
            ESP_LOGE(name, "Open movement timeout exceeded.");
            stop();
            state = ERROR_STATE;
            break;
        }
        // limit reached
        else if (checkLimitSwitchOpenActive())
        {
            ESP_LOGI(name, "Open limit switch triggered.");
            positionPercent = 100.0f; // Calibrate position.
            stop();
            state = IDLE_FULLY_OPEN;
            break;
        }
        // target reached
        else if ((currentTime - timestampStart) >= targetRunTime)
        {
            ESP_LOGI(name, "Target run time reached (while opening).");
            stop();
            state = IDLE_PARTIALLY_OPEN;
        }
        break;
    }
    case MOVING_CLOSING:
    {
        updatePosition();
        // timeout
        if ((currentTime - timestampStart) >= (kMovingTimeout * 1000))
        {
            ESP_LOGE(name, "Close movement timeout exceeded.");
            stop();
            state = ERROR_STATE;
            break;
        }
        // limit reached
        else if (checkLimitSwitchClosedActive())
        {
            ESP_LOGI(name, "Close limit switch triggered.");
            positionPercent = 0.0f; // Calibrate position.
            stop();
            state = IDLE_FULLY_CLOSED;
            break;
        }
        // target reached
        else if ((currentTime - timestampStart) >= targetRunTime)
        {
            ESP_LOGI(name, "Target run time reached (while closing).");
            stop();
            state = IDLE_PARTIALLY_OPEN;
        }
        break;
    }
    case IDLE_FULLY_OPEN:
        // check if gate was "manually moved away from limit"
        if (!checkLimitSwitchOpenActive())
        {
            state = IDLE_PARTIALLY_OPEN;
            positionPercent = 99;
            ESP_LOGI(name, "Gate moved away from fully open position -> switching to IDLE_PARTIALLY_OPEN");
        }
        break;
    case IDLE_FULLY_CLOSED:
        // check if gate was "manually moved away from limit"
        if (!checkLimitSwitchClosedActive())
        {
            state = IDLE_PARTIALLY_OPEN;
            positionPercent = 1;
            ESP_LOGW(name, "Gate moved away from fully closed position while off -> switching to IDLE_PARTIALLY_OPEN");
        }
        break;
    case IDLE_PARTIALLY_OPEN:
        // update state when gate was "manually moved to limit"
        if (checkLimitSwitchOpenActive())
        {
            state = IDLE_FULLY_OPEN;
            positionPercent = 100.0f;
            ESP_LOGW(name, "Gate reached fully open position while off -> switching to IDLE_FULLY_OPEN");
        }
        else if (checkLimitSwitchClosedActive())
        {
            state = IDLE_FULLY_CLOSED;
            positionPercent = 0.0f;
            ESP_LOGW(name, "Gate reached fully closed position while off -> switching to IDLE_FULLY_CLOSED");
        }
        break;
    case ERROR_STATE:
        ESP_LOGE(name, "currently in ERROR state, TODO how to use / recover from here?");
        // Nothing further to do.
        break;
    }
}



GateState Gate::getState() const {
    return state;
}
