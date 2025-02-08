#include "gate.hpp"
#include <esp_timer.h>

#define IGNORE_VFD_ERROR

Gate::Gate(const char* name, gpio_num_t limitSwitchOpen, gpio_num_t limitSwitchClosed,
           gpio_num_t relayPin, VFD* vfd, buzzer_t* buzzer, float defaultFrequency, 
           uint32_t timeoutMs, uint32_t runDurationMs, uint32_t openTimeoutMs, uint32_t closeTimeoutMs)
    : name(name), limitSwitchOpen(limitSwitchOpen), limitSwitchClosed(limitSwitchClosed),
      relayPin(relayPin), vfd(vfd), buzzer(buzzer), defaultFrequency(defaultFrequency),
      relayTimeoutMs(timeoutMs), runDurationMs(runDurationMs),
      openTimeoutMs(openTimeoutMs), closeTimeoutMs(closeTimeoutMs),
      state(IDLE_FULLY_CLOSED), relayTimeoutActive(false), relayOn(false),
      positionPercent(0.0f)
{
    gpio_set_direction(limitSwitchOpen, GPIO_MODE_INPUT);
    gpio_set_direction(limitSwitchClosed, GPIO_MODE_INPUT);
    gpio_set_direction(relayPin, GPIO_MODE_OUTPUT);
    // Initialize previous switch states.
    prevOpenSwitchState = (gpio_get_level(limitSwitchOpen) == 1);
    prevClosedSwitchState = (gpio_get_level(limitSwitchClosed) == 1);
    ESP_LOGW(name, "GPIO pins and variables initialized.");
}

void Gate::startRelay() {
    if (!relayOn) {
        ESP_LOGI(name, "Turning relay on...");
        gpio_set_level(relayPin, 1);
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
    gpio_set_level(relayPin, 0);
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
    bool current = (gpio_get_level(limitSwitchOpen) == 1);
    if (current != prevOpenSwitchState) {
        ESP_LOGI(name, "Open limit switch changed to %s.", current ? "ACTIVE" : "INACTIVE");
        prevOpenSwitchState = current;
    }
    return current;
}

bool Gate::checkLimitSwitchClosedActive() {
    bool current = (gpio_get_level(limitSwitchClosed) == 1);
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
    timestampStart = esp_timer_get_time();
    lastPositionUpdateTimestamp = timestampStart;
    vfd->setFrequency(defaultFrequency);
    esp_err_t err = vfd->start(opening); // start motor in desired direction
    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        ESP_LOGE(name, "VFD starting failed!");
        buzzer->beep(5, 100, 200);
        //state = ERROR_STATE;
        return;
    }
    #endif

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
        targetRunTime = closeTimeoutMs * 1000;
    } else if (target >= 100.0f) {
        target = 100.0f;
        targetRunTime = openTimeoutMs * 1000;
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
        ESP_LOGI(name, "Already at target position (%.2f%%).", target);
    }
}

// Public method: open for a specified duration.
void Gate::openForMs(uint32_t ms) {
    ESP_LOGI(name, "Received command: Opening for %ld ms.", ms);
    targetRunTime = ms * 1000;
    startMovement(true);
}

// Public method: close for a specified duration.
void Gate::closeForMs(uint32_t ms) {
    ESP_LOGI(name, "Received command: Closing for %ld ms.", ms);
    targetRunTime = ms * 1000;
    startMovement(false);
}

void Gate::stop() {
    ESP_LOGI(name, "stopping gate....");
    esp_err_t err = vfd->stop();
    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        ESP_LOGE(name, "VFD stop failed!");
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
    uint64_t currentTime = esp_timer_get_time();

    // In idle states, always check limit switches to detect manual movement.
    if (state == IDLE_FULLY_OPEN || state == IDLE_FULLY_CLOSED || state == IDLE_PARTIALLY_OPEN) {
        bool openActive = checkLimitSwitchOpenActive();
        bool closedActive = checkLimitSwitchClosedActive();
        if (state == IDLE_FULLY_OPEN && !openActive) {
            state = IDLE_PARTIALLY_OPEN;
            positionPercent = 99;
            ESP_LOGI(name, "Gate moved away from fully open position.");
        }
        if (state == IDLE_FULLY_CLOSED && !closedActive) {
            state = IDLE_PARTIALLY_OPEN;
            positionPercent = 1;
            ESP_LOGI(name, "Gate moved away from fully closed position.");
        }
        if (state == IDLE_PARTIALLY_OPEN) {
            if (openActive) {
                state = IDLE_FULLY_OPEN;
                positionPercent = 100.0f;
                ESP_LOGI(name, "Gate reached fully open position.");
            } else if (closedActive) {
                state = IDLE_FULLY_CLOSED;
                positionPercent = 0.0f;
                ESP_LOGI(name, "Gate reached fully closed position.");
            }
        }
        // Also check for relay inactivity in idle states.
        if (relayTimeoutActive && (currentTime - lastActivityTimestamp > relayTimeoutMs * 1000)) {
            ESP_LOGW(name, "Relay inactivity timeout reached. Forcing relay OFF...");
            forceStopRelay();
        }
    }

    // Main state machine for moving states.
    switch (state) {
        case MOVING_OPENING: {
            updatePosition();
            // timeout
            if ((currentTime - timestampStart) >= (openTimeoutMs * 1000)) {
                ESP_LOGE(name, "Open movement timeout exceeded.");
                stop();
                state = ERROR_STATE;
                break;
            }
            // limit reached
            else if (checkLimitSwitchOpenActive()) {
                ESP_LOGI(name, "Open limit switch triggered.");
                positionPercent = 100.0f; // Calibrate position.
                stop();
                state = IDLE_FULLY_OPEN;
                break;
            }
            // target reached
            else if ((currentTime - timestampStart) >= targetRunTime) {
                ESP_LOGI(name, "Target run time reached (opening).");
                stop();
                state = IDLE_PARTIALLY_OPEN;
            }
            break;
        }
        case MOVING_CLOSING: {
            updatePosition();
            // timeout
            if ((currentTime - timestampStart) >= (closeTimeoutMs * 1000)) {
                ESP_LOGE(name, "Close movement timeout exceeded.");
                stop();
                state = ERROR_STATE;
                break;
            }
            // limit reached
            else if (checkLimitSwitchClosedActive()) {
                ESP_LOGI(name, "Close limit switch triggered.");
                positionPercent = 0.0f; // Calibrate position.
                stop();
                state = IDLE_FULLY_CLOSED;
                break;
            }
            // target reached
            else if ((currentTime - timestampStart) >= targetRunTime) {
                ESP_LOGI(name, "Target run time reached (closing).");
                stop();
                state = IDLE_PARTIALLY_OPEN;
            }
            break;
        }
        case IDLE_FULLY_OPEN:
        case IDLE_FULLY_CLOSED:
        case IDLE_PARTIALLY_OPEN:
        case ERROR_STATE:
            // Nothing further to do.
            break;
    }
}

GateState Gate::getState() const {
    return state;
}
