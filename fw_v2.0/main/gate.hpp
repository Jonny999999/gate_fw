#pragma once
#ifndef GATE_HPP
#define GATE_HPP

#include "vfd.hpp"
#include "indicator.hpp"
#include <driver/gpio.h>
#include <esp_log.h>
#include <string>
#include <algorithm>  // for std::clamp
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Define a delay (in milliseconds) waited for the VFD startup after the relay is turned on.
#define DELAY_VFD_STARTUP 870
    // 900 worked well, but feels long...
    // 800 west gate unreliable sometimes it does not start...
    // 700 too short, west gate did not start properly
    // it seems VFD recognizes command, but needs some time to finish booting and only starts later anyways
// TODO: test individual delay for each VFD

// #define IGNORE_VFD_ERROR // if defined does not force realy off when stop command fails
// How long the VFD supply relay stays on after the last movement.
// 1 h was too short (the drives were often off during an active day, costing the ~870 ms
// startup delay every time). 3 h was better, 4 h is comfortable - keeping the drives
// powered is no longer a concern with PV excess.
// note: written as UL and multiplied out in 64 bit at the point of use, see handle().
//       (4 h in microseconds does not fit in 32 bits, which is what the 3 h setting silently
//        ran into: it wrapped and expired after ~37 minutes instead.)
#define RELAY_INACTIVITY_TIMEOUT_MS ((4UL*60 + 0)*60*1000UL)

// Log the motor current (with the speed it was measured at) on every check while closing.
// Enabled on this branch on purpose: the obstruction threshold below was measured at a
// single speed, and what the motor draws at SLOW_VFD_FREQUENCY is exactly the number this
// experiment still owes. See kCurrentLimitSlowAmpere.
#define LOG_VFD_CURRENT_WHEN_CLOSING
#define CURRENT_MONITORING_ENABLED
#define DEFAULT_VFD_CURRENT_LIMIT 0.60

//===============================
//======= Gate speed ============
//===============================
// Motor speed in Hz. 40 Hz is what the gate has always actually run at - the constant used
// to say 50 and was simply never read (see the note on kSpeedFullHz), while the movement
// timings in main.cpp were measured against the 40 Hz the drive was really given.
#define DEFAULT_VFD_FREQUENCY 40

// Variable gate speed - PROOF OF CONCEPT, see ROADMAP 2.6.
// Set to 0 to get exactly the previous behaviour back: one constant speed from standstill
// to limit switch.
//
// The gate starts gently, runs at full speed in the middle, and slows down again for the
// last stretch before the limit stop - which today takes the full 40 Hz on every single
// movement and is the part of the mechanics that suffers for it.
//
// Deliberately time-based and crude. The two slow stretches are only a second or so of
// travel each, and starting one too early merely costs a moment while starting one too
// late costs nothing that is not already the case today - so the position estimate does
// not have to be good, it only has to be roughly right. Encoders (ROADMAP 3.2) would make
// it exact, but they are not a prerequisite for finding out whether the feature is worth
// having at all.
#define VARIABLE_SPEED_ENABLED 1
#define SLOW_VFD_FREQUENCY 25 // speed for the gentle start and the final approach

#define BEEP_AT_LIMIT_SW_CHANGE

#define PAUSED_SWITCH_TO_IDLE_TIMEOUT_MS 30*1000

// Gate state strings for logging
extern const char *GateState_str [8];

// Gate state definitions.
enum GateState {
    IDLE_FULLY_OPEN = 0,
    IDLE_FULLY_CLOSED,
    IDLE_PARTIALLY_OPEN,
    WAITING_FOR_VFD_STARTUP,
    MOVING_OPENING,
    MOVING_CLOSING,
    PAUSED_STATE,
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

         uint32_t runDurationMs
    );

    // Public movement methods.
    // runTo() moves the gate to the target percentage.
    // If target is 0 or 100, a full movement is forced by using the full timeout.
    void runTo(float targetPercent);
    void openForMs(uint32_t ms);
    void openCompletely();
    void closeForMs(uint32_t ms);
    void closeCompletely();
    void updateTargetRunTime(uint32_t ms);
    void stop(bool forceStatePartialOpen = true);
    void pause();
    void resume();
    void cancel();

    // The state machine to be called periodically.
    void handle();

    // Retrieve the current state.
    GateState getState() const {return state;};
    bool getIsIdling() const {return state == IDLE_FULLY_OPEN || state == IDLE_FULLY_CLOSED || state == IDLE_PARTIALLY_OPEN;};
    bool getIsMoving() const {return state == MOVING_CLOSING || state == MOVING_OPENING || state == WAITING_FOR_VFD_STARTUP;};
    // "closing, or just about to": also true while the VFD is still booting for a closing
    // movement. The light-barrier safety check uses this, and it must react before the
    // motor turns, not after.
    bool getIsClosing() const {
        return state == MOVING_CLOSING ||
               (state == WAITING_FOR_VFD_STARTUP && nextDirection == false);
    };

    // True when the last movement command could not be carried out because the gate already
    // reports that end position. Not an error - pressing close on a closed gate is normal -
    // but the control task uses it to acknowledge that nothing is going to happen, instead
    // of playing the usual start signal and then standing still. Also the symptom of a limit
    // switch stuck in the active position.
    // Cleared as soon as a movement does start.
    bool getMovementWasRefused() const {return movementWasRefused;};

    // Error reporting.
    // ERROR_STATE only lasts a single handle() cycle before the gate returns to
    // IDLE_PARTIALLY_OPEN, so an observer running in another task would almost always miss
    // it. The flag below is therefore latched until it is explicitly cleared, which is what
    // the fault LED is driven from. (ROADMAP.md B13)
    bool getErrorLatched() const {return errorLatched;};
    void clearErrorLatch() {errorLatched = false;};


private:
    // ==== CONFIG ====
    // Speed the gate runs at. Note this is 40 Hz, NOT the 50 Hz that DEFAULT_VFD_FREQUENCY
    // and several comments still claim - the unused 'defaultFrequency' member below is what
    // that macro feeds, and nothing ever read it. The measured run durations in main.cpp
    // belong to THIS value.
    static constexpr uint16_t kSpeedFullHz = DEFAULT_VFD_FREQUENCY;
    static constexpr uint16_t kSpeedSlowHz = SLOW_VFD_FREQUENCY;

    // Shape of the speed profile, given as DISTANCE (full-speed ms, see below) rather than
    // wall-clock time - "the first 700 ms worth of travel", not "the first 700 ms".
    // Distances, because that is what stays constant when the speed changes.
    //
    // A movement shorter than the sum of the two runs entirely at the slow speed, which is
    // what happens to the pedestrian gap (1900 ms of travel). That is intentional: the gap
    // ends up exactly as wide as before, it just opens more gently.
    static constexpr uint32_t kSlowStartDistanceMs = 700;    // gentle start
    static constexpr uint32_t kSlowApproachDistanceMs = 1500; // slow before the limit stop
    static constexpr float kCurrentLimitAmpere = DEFAULT_VFD_CURRENT_LIMIT;  // Max VFD current when closing for gate to stop
    // Same threshold at the slow speed, until it has been measured. A V/f drive does not
    // draw the same current for the same load at a different frequency, so this almost
    // certainly wants its own value - but guessing one would either weaken the obstruction
    // detection exactly where the gate is nearly closed, or produce nuisance trips. Read
    // the LOG_VFD_CURRENT_WHEN_CLOSING output from a few runs and set it from that.
    static constexpr float kCurrentLimitSlowAmpere = DEFAULT_VFD_CURRENT_LIMIT;
    static constexpr uint32_t kRelayInactivityTimeoutMs = RELAY_INACTIVITY_TIMEOUT_MS;      // Inactivity timeout (for relay turn-off)

    // Hard safety backstop: a movement that has not finished by then is aborted with an error.
    // The only constant in the firmware that is genuinely wall-clock rather than distance,
    // which is why it is also the only one the speed profile forces to change: the two slow
    // stretches add roughly (kSlowStartDistanceMs + kSlowApproachDistanceMs) *
    // (kSpeedFullHz / kSpeedSlowHz - 1) ~ 1.3 s to every full movement. Raised by 3 s so
    // the margin over a full run stays what it was at constant speed.
    // 15000 is the value that has been in service since V2.0 - restored when the speed
    // profile is switched off, so that path is unchanged.
#if VARIABLE_SPEED_ENABLED
    static constexpr uint32_t kMovementTimeoutMs = 18000;
#else
    static constexpr uint32_t kMovementTimeoutMs = 15000;
#endif

    // Guaranteed gap between the target run time and the safety backstop.
    // Previously 'runDurationMs + 5000' could equal or exceed kMovementTimeoutMs (exactly
    // equal for the west gate), so a full movement that missed its limit switch hit the
    // timeout branch at the very same instant and reported an error instead of simply
    // stopping. (ROADMAP.md B11)
    static constexpr uint32_t kMovementTimeoutMarginMs = 2000;

    // How much longer than the measured travel time a full open/close is allowed to run,
    // so the limit switch is definitely reached.
    static constexpr uint32_t kFullMovementExtraTimeMs = 5000;

    // ==== Movement distance bookkeeping ====
    // A movement's target is a DISTANCE, not a wall-clock duration - expressed as the
    // number of milliseconds the gate would need to cover it at kSpeedFullHz.
    //
    // At a constant speed the two are the same thing, which is why the firmware could get
    // away with comparing elapsed time against targetRunTimeMs so far. As soon as the speed
    // can change during a movement they part ways, and every constant in the firmware
    // ('open for 1900 ms', 'a full run takes 10000 ms') means the distance, never the clock.
    // Integrating speed x time keeps all of those constants valid unchanged.
    //
    // This is also exactly the quantity encoders would provide directly (ROADMAP 3.2): swap
    // the integration in updateTravel() for a pulse count and nothing else has to change.
    static constexpr uint32_t kDistancePerMsAtFullSpeed = 1;  // by definition, see above

    // How often the start command is sent before giving up on the drive.
    // The command is idempotent ("run in this direction"), so a lost or garbled reply can
    // simply be repeated - which is far more likely than the drive actually refusing to
    // start. Each attempt is itself retried once inside send_modbus_command().
    static constexpr int kStartAttempts = 3;


    const char* name;             // Gate name (used as the log tag)
    const gpio_num_t kLimitSwitchOpenGpio;   // GPIO for the open limit switch
    const bool kLimitSwitchOpen_ActiveLevel; // Voltage level when switch is active (high/low)
    const gpio_num_t kLimitSwitchClosedGpio; // GPIO for the closed limit switch
    const bool kLimitSwitchClosed_ActiveLevel; // Voltage level when switch is active (high/low)
    const gpio_num_t kRelayPinGpio;          // GPIO for the VFD supply relay

    VFD* const vfd;       // Pointer to the associated VFD object
    // note: buzzer / LED are no longer reached through a pointer - the indicator task is
    // addressed by free functions from indicator.hpp

    // note: a 'defaultFrequency' member used to sit here, fed from DEFAULT_VFD_FREQUENCY.
    //       Nothing ever read it - the speed really written to the drive came from a
    //       separate constant with a different value. Removed; kSpeedFullHz is the one.
    const uint32_t runDurationMs;  // Full run duration (0% to 100%) in milliseconds

    // note: all members below are given a defined default here.
    // Several of them used to be read before ever being written (e.g. the relay startup
    // check and the limit-switch change detection ran on garbage during the first cycles
    // after boot).
    uint64_t timestampStartUs = 0; // Timestamp (in microseconds) when movement started
    uint64_t targetRunTimeMs = 0;  // Distance the current movement should cover, in full-speed ms

    GateState state;               // Current state of the gate (set in the constructor)
    bool nextDirection = false;    // Store desired gate direction while waiting for vfd startup
    uint64_t lastActivityTimestampUs = 0; // For relay timeout control
    bool relayTimeoutActive;       // Flag indicating a soft-stop request for the relay
    bool relayOn;                  // Whether the relay is currently on
    uint64_t timestampRelayTurnedOnUs = 0; // Timestamp the relay was last switched on

    float positionPercent;         // Current estimated position (0% = closed, 100% = open)
    uint64_t lastPositionUpdateTimestampUs = 0; // Timestamp of last position update

    // Distance covered since the motor started turning (see above). Accumulated in
    // MICROseconds so the speed weighting does not lose a fraction of a millisecond on
    // every cycle - the gate task steps this every 10 ms, and at the slow speed a
    // millisecond-granular sum would drop a few percent of the travel over a movement.
    // Reset in startMovement() at the moment the motor actually starts, not when the
    // command is received - the VFD startup delay must not count as travel.
    uint64_t travelledDistanceUs = 0;
    uint32_t travelledDistanceMs() const { return (uint32_t)(travelledDistanceUs / 1000); }
    // Speed the drive is currently asked to run at. Only ever changed through setSpeed(),
    // so updateTravel() can weight the elapsed time with the speed it was covered at.
    uint16_t currentSpeedHz = kSpeedFullHz;
    // Where this movement is expected to end, as a distance from its start: the target, or
    // the limit switch, whichever comes first. This is what the final approach is timed
    // against - and the only place the position estimate is used for anything but logging.
    uint32_t expectedTravelDistanceMs = 0;

    //--- measured travel time (diagnostic, ROADMAP 2.6) ---
    // A movement that starts on one limit switch and ends on the other has covered the
    // whole rail, so its duration IS the real runDurationMs for this gate - the one value
    // the time-based position estimate stands or falls with, and currently a constant
    // measured by hand once. Measuring it on every full run costs nothing and answers the
    // question the variable-speed experiment depends on: how repeatable is it really?
    //
    // Deliberately only logged, never fed back into runDurationMs. Learning the value at
    // runtime (and storing it in NVS) is a separate decision that should be made with these
    // numbers in hand, not before - and it is largely pointless if encoders arrive (3.2).
    bool movementStartedAtOppositeLimit = false;  // set when the motor starts, see startMovement()
    uint32_t measuredFullTravelCount = 0;         // how many full runs have been measured
    uint32_t measuredFullTravelAverageMs = 0;     // running mean of those, wall-clock ms

    // Variables to track previous state of limit switches (for logging changes)
    bool prevOpenSwitchState = false;
    bool prevClosedSwitchState = false;

    // Latched error indication, see getErrorLatched(). Written by the gate task, read by
    // the control task - std::atomic makes that explicit.
    std::atomic<bool> errorLatched{false};
    std::atomic<bool> movementWasRefused{false};

    // Variables for pause/resume support
    bool wasOpeningBeforePause = false;
    uint64_t pauseStartTimestampUs = 0;
    uint64_t remainingRunTimeAtPauseMs = 0; // distance left when pause() was called (0 = target already reached)

    // Private helper methods.
    void startRelay();
    void softStopRelay();
    void forceStopRelay();
    // Integrate the time since the last call into travelledDistanceUs and positionPercent,
    // weighted by the speed it was actually covered at.
    void updateTravel();
    // Ask the drive for a new frequency and remember it. Travel is settled first, so the
    // stretch already covered is accounted for at the old speed.
    void setSpeed(uint16_t frequencyHz);
    // Pick the speed for where the gate currently is in its movement. Called every cycle
    // while moving; setSpeed() ignores a request for the speed already running, so this
    // costs one modbus write per phase change, not one per cycle.
    void updateSpeedProfile();
    // Work out expectedTravelDistanceMs from the current target and position. Has to be
    // redone whenever either changes - at the start of a movement, and when the control
    // task shortens a movement that is already running (the pedestrian gap does exactly
    // that: open completely, then set a target run time).
    void recomputeExpectedTravelDistance();

    // Target run time for a full open / close: long enough to reach the limit switch,
    // but always clearly below the safety backstop.
    uint32_t getFullMovementRunTimeMs() const {
        return std::min(runDurationMs + kFullMovementExtraTimeMs,
                        kMovementTimeoutMs - kMovementTimeoutMarginMs);
    };
    // Log the real travel time if this movement ran from one limit switch to the other.
    // Called from handle() the moment the far limit switch is reached.
    void reportFullTravelIfMeasured();
    bool checkLimitSwitchOpenActive();
    bool checkLimitSwitchClosedActive();
    bool checkCurrentLimitExceeded();

    // Private movement helper: start movement in the given direction.
    // 'opening' = true means start opening, false means start closing.
    void startMovement(bool opening);

};

#endif // GATE_HPP
