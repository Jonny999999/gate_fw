#pragma once
#ifndef GATE_HPP
#define GATE_HPP

#include "vfd.hpp"
#include "buzzer.hpp"
#include <driver/gpio.h>
#include <esp_log.h>
#include <string>
#include <algorithm>  // for std::clamp
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "buzzer.hpp"

// Define a delay (in milliseconds) for the VFD startup after the relay is turned on.
#define DELAY_VFD_STARTUP 500

// Gate state strings for logging
extern const char *GateState_str [6];

// Gate state definitions.
enum GateState {
    IDLE_FULLY_OPEN,
    IDLE_FULLY_CLOSED,
    IDLE_PARTIALLY_OPEN,
    MOVING_OPENING,
    MOVING_CLOSING,
    ERROR_STATE
};

class Gate {
public:
    // Constructor.
    Gate(const char *name,
         gpio_num_t kLimitSwitchOpenGpio,
         bool kLimitSwitchOpen_ActiveLevel,
         gpio_num_t kLimitSwitchClosedGpio,
         bool kLimitSwitchClosed_ActiveLevel,
         gpio_num_t kRelayPinGpio,

         VFD *vfd,
         buzzer_t *buzzer,

         uint32_t runDurationMs
    );

    // Public movement methods.
    // runTo() moves the gate to the target percentage.
    // If target is 0 or 100, a full movement is forced by using the full timeout.
    void runTo(float targetPercent);
    void openForMs(uint32_t ms);
    void closeForMs(uint32_t ms);
    void stop();

    // The state machine to be called periodically.
    void handle();

    // Retrieve the current state.
    GateState getState() const;


private:
    // ==== CONFIG ====
    static constexpr float kDefaultVfdFrequency = 40.0f;  // Frequency (Hz) to use when running
    static constexpr uint32_t kRelayInactivityTimeoutMs = 60e3;      // Inactivity timeout (for relay turn-off)
    static constexpr uint32_t kMovingTimeout = 15e3;  // Max allowed time for continuous opening / closing movement without reaching limit


    const char* name;             // Gate name (used as the log tag)
    const gpio_num_t kLimitSwitchOpenGpio;   // GPIO for the open limit switch
    const bool kLimitSwitchOpen_ActiveLevel; // Voltage level when switch is active (high/low)
    const gpio_num_t kLimitSwitchClosedGpio; // GPIO for the closed limit switch
    const bool kLimitSwitchClosed_ActiveLevel; // Voltage level when switch is active (high/low)
    const gpio_num_t kRelayPinGpio;          // GPIO for the VFD supply relay

    VFD* const vfd;       // Pointer to the associated VFD object
    buzzer_t* const buzzer; // Pointer to the buzzer object

    const float defaultFrequency = 40;  // Frequency (Hz) to use when running
    const uint32_t runDurationMs;  // Full run duration (0% to 100%) in milliseconds

    uint64_t timestampStart; // Timestamp (in microseconds) when movement started
    uint64_t targetRunTime;  // Desired movement duration (in microseconds) for partial moves

    GateState state;               // Current state of the gate
    uint64_t lastActivityTimestamp; // For relay timeout control
    bool relayTimeoutActive;       // Flag indicating a soft-stop request for the relay
    bool relayOn;                  // Whether the relay is currently on

    float positionPercent;         // Current estimated position (0% = closed, 100% = open)
    uint64_t lastPositionUpdateTimestamp; // Timestamp of last position update

    // Variables to track previous state of limit switches (for logging changes)
    bool prevOpenSwitchState;
    bool prevClosedSwitchState;

    // Private helper methods.
    void startRelay();
    void softStopRelay();
    void forceStopRelay();
    void updatePosition();
    bool checkLimitSwitchOpenActive();
    bool checkLimitSwitchClosedActive();

    // Private movement helper: start movement in the given direction.
    // 'opening' = true means start opening, false means start closing.
    void startMovement(bool opening);

};

#endif // GATE_HPP
