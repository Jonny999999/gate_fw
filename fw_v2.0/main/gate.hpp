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
         gpio_num_t limitSwitchOpen,
         gpio_num_t limitSwitchClosed,
         gpio_num_t relayPin,

         VFD *vfd,
         buzzer_t *buzzer,

         float defaultFrequency,
         uint32_t relayTimeoutMs,
         uint32_t runDurationMs,
         uint32_t openTimeoutMs,
         uint32_t closeTimeoutMs);

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
    const char* name;             // Gate name (used as the log tag)
    const gpio_num_t limitSwitchOpen;   // GPIO for the open limit switch
    const gpio_num_t limitSwitchClosed; // GPIO for the closed limit switch
    const gpio_num_t relayPin;          // GPIO for the VFD supply relay

    VFD* const vfd;       // Pointer to the associated VFD object
    buzzer_t* const buzzer; // Pointer to the buzzer object

    const float defaultFrequency = 40;  // Frequency (Hz) to use when running
    const uint32_t relayTimeoutMs;      // Inactivity timeout (for relay turn-off) in milliseconds
    const uint32_t runDurationMs;  // Full run duration (0% to 100%) in milliseconds
    const uint32_t openTimeoutMs;  // Maximum allowed time for an opening movement (ms)
    const uint32_t closeTimeoutMs; // Maximum allowed time for a closing movement (ms)

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
