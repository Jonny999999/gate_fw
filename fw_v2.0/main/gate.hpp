#ifndef GATE_HPP
#define GATE_HPP

#include "vfd.hpp"
#include "buzzer.hpp"
#include <driver/gpio.h>
#include <esp_log.h>
#include <string>

enum GateState {
    IDLE_FULLY_OPEN,
    IDLE_FULLY_CLOSED,
    MOVING_OPENING,
    MOVING_CLOSING,
    PARTIALLY_OPEN,
    ERROR
};

class Gate {
private:
    const char* name;
    gpio_num_t limitSwitchOpen;
    gpio_num_t limitSwitchClosed;
    gpio_num_t relayPin;

    VFD* vfd;
    Buzzer* buzzer;

    uint32_t timeoutMs;
    uint32_t runDurationMs;
    float defaultFrequency;

    uint32_t timestampStart;
    uint32_t targetRunTime;

    GateState state;
    uint64_t lastActivityTimestamp;
    bool relayTimeoutActive;

    float positionPercent; // 0% = closed, 100% = open

    void startRelay();
    void softStopRelay();
    void forceStopRelay();
    void updatePosition(bool isOpening, uint64_t duration);

    bool checkLimitSwitch(gpio_num_t pin);
    void updateStateBasedOnLimits();

public:
    Gate(const char* name, gpio_num_t limitSwitchOpen, gpio_num_t limitSwitchClosed, 
         gpio_num_t relayPin, VFD* vfd, Buzzer* buzzer, float defaultFrequency, 
         uint32_t timeoutMs, uint32_t runDurationMs);

    void open();
    void close();
    void runTo(float targetPercent);
    void openForMs(uint32_t ms);
    void closeForMs(uint32_t ms);
    void stop();
    void handle();

    GateState getState() const;
};

#endif // GATE_HPP
