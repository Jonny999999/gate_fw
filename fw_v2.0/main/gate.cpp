#include "gate.hpp"
#include <esp_timer.h>

Gate::Gate(const char* name, gpio_num_t limitSwitchOpen, gpio_num_t limitSwitchClosed,
           gpio_num_t relayPin, VFD* vfd, Buzzer* buzzer, float defaultFrequency, 
           uint32_t timeoutMs, uint32_t runDurationMs)
    : name(name), limitSwitchOpen(limitSwitchOpen), limitSwitchClosed(limitSwitchClosed),
      relayPin(relayPin), vfd(vfd), buzzer(buzzer), defaultFrequency(defaultFrequency),
      timeoutMs(timeoutMs), runDurationMs(runDurationMs), state(IDLE_FULLY_CLOSED),
      relayTimeoutActive(false), positionPercent(0.0f) {

    gpio_set_direction(limitSwitchOpen, GPIO_MODE_INPUT);
    gpio_set_direction(limitSwitchClosed, GPIO_MODE_INPUT);
    gpio_set_direction(relayPin, GPIO_MODE_OUTPUT);
    ESP_LOGI(name, "Gate initialized.");
}

void Gate::startRelay() {
    gpio_set_level(relayPin, 1);
    lastActivityTimestamp = esp_timer_get_time();
    relayTimeoutActive = false;
    ESP_LOGI(name, "Relay turned ON.");
}

void Gate::softStopRelay() {
    relayTimeoutActive = true;
    lastActivityTimestamp = esp_timer_get_time();
    ESP_LOGI(name, "Relay soft stop initiated.");
}

void Gate::forceStopRelay() {
    gpio_set_level(relayPin, 0);
    relayTimeoutActive = false;
    ESP_LOGW(name, "Relay forced OFF.");
}

void Gate::updatePosition(bool isOpening, uint64_t duration) {
    float deltaPercent = (duration / (float)runDurationMs) * 100.0f;
    positionPercent += isOpening ? deltaPercent : -deltaPercent;
    positionPercent = std::clamp(positionPercent, 0.0f, 100.0f);
    ESP_LOGI(name, "Position updated: %.2f%%", positionPercent);
}

bool Gate::checkLimitSwitch(gpio_num_t pin) {
    return gpio_get_level(pin) == 1;
}

void Gate::updateStateBasedOnLimits() {
    if (checkLimitSwitch(limitSwitchOpen)) {
        state = IDLE_FULLY_OPEN;
        positionPercent = 100.0f;
    } else if (checkLimitSwitch(limitSwitchClosed)) {
        state = IDLE_FULLY_CLOSED;
        positionPercent = 0.0f;
    }
}

void Gate::open() {
    startRelay();
    vfd->start(defaultFrequency, true); // Start opening
    state = MOVING_OPENING;
    buzzer->beep(2, 100, 100);
    ESP_LOGI(name, "Opening gate.");
}

void Gate::close() {
    startRelay();
    vfd->start(defaultFrequency, false); // Start closing
    state = MOVING_CLOSING;
    buzzer->beep(3, 100, 100);
    ESP_LOGI(name, "Closing gate.");
}

void Gate::runTo(float targetPercent) {
    if (targetPercent > positionPercent) {
        open();
    } else if (targetPercent < positionPercent) {
        close();
    }
}

void Gate::openForMs(uint32_t ms) {
    targetRunTime = ms * 1000; // Convert to microseconds
    timestampStart = esp_timer_get_time();
    open(); // Start the gate
}

// FIXME: this wont work, set state + calculate timeout in handle

void Gate::closeForMs(uint32_t ms) {
    targetRunTime = ms * 1000; // Convert to microseconds
    timestampStart = esp_timer_get_time();
    close();
}

void Gate::stop() {
    if (vfd->stop() != 0) {
        ESP_LOGE(name, "VFD stop failed!");
        buzzer->beep(5, 300, 300);
        forceStopRelay();
        return;
    }

    state = PARTIALLY_OPEN;
    buzzer->beep(1, 200, 200);
    softStopRelay();
    ESP_LOGI(name, "Gate stopped.");
}

void Gate::handle() {
    uint64_t currentTime = esp_timer_get_time();

    if (relayTimeoutActive && currentTime - lastActivityTimestamp > timeoutMs * 1000) {
        forceStopRelay();
    }

    switch (state) {
    case IDLE_FULLY_CLOSED:
        // Nothing to do; remain idle
        break;

    case IDLE_FULLY_OPEN:
        // Nothing to do; remain idle
        break;

    case MOVING_OPENING:
        updatePosition();
        if (esp_timer_get_time() - startTimestamp >= targetRunTime || checkLimitSwitchOpen()) {
            stop();
            state = STATE_IDLE_FULLY_OPEN;
        }
        break;

    case MOVING_CLOSING:
        updatePosition();
        if (esp_timer_get_time() - startTimestamp >= targetRunTime || checkLimitSwitchClosed()) {
            stop();
            state = STATE_IDLE_FULLY_CLOSED;
        }
        break;

    // Add other states as needed
}


}

GateState Gate::getState() const {
    return state;
}
