#pragma once
#ifndef GATE_HPP
#define GATE_HPP

#include "config_behaviour.h"
#include "vfd.hpp"
#include "indicator.hpp"
#include <driver/gpio.h>
#include <esp_log.h>
#include <string>
#include <algorithm>  // for std::clamp
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    // See config_behaviour.h for what each of these means and why it has that value.
    // kSpeedReferenceHz is the unit every distance constant is measured in, NOT a speed the
    // gate is ever asked to run at - which is why changing kSpeedFullHz does not invalidate
    // runDurationMs, the pedestrian gap, or the two distances below.
    static constexpr uint16_t kSpeedReferenceHz = VFD_FREQUENCY_REFERENCE_HZ;
    static constexpr uint16_t kSpeedFullHz = VFD_FREQUENCY_FULL_HZ;
    static constexpr uint16_t kSpeedSlowHz = VFD_FREQUENCY_SLOW_HZ;
    static constexpr uint32_t kSlowStartDistanceMs = GATE_SLOW_START_DISTANCE_MS;
    static constexpr uint32_t kSlowApproachDistanceMs = GATE_SLOW_APPROACH_DISTANCE_MS;
    // Max VFD current when closing for the gate to stop - one per speed,
    // see config_behaviour.h.
    static constexpr float kCurrentLimitAmpere = VFD_CURRENT_LIMIT_AMPERE;
    static constexpr float kCurrentLimitSlowAmpere = VFD_CURRENT_LIMIT_SLOW_AMPERE;
    static constexpr uint32_t kRelayInactivityTimeoutMs = RELAY_INACTIVITY_TIMEOUT_MS;      // Inactivity timeout (for relay turn-off)

    // Hard safety backstop: a movement that has not finished by then is aborted with an error.
    // The only constant in the firmware that is genuinely wall-clock rather than distance,
    // so it is also the only one that has to be checked whenever a speed changes.
    // Deliberately left at the value that has been in service since V2.0 - it still fits:
    //
    //   east gate, 11000 ms of rail, profile on at 60/25 Hz
    //     slow  2200 * 40/25 = 3520 ms  +  fast 8800 * 40/60 = 5867 ms  ~  9.4 s
    //   same with the full speed back at 40 Hz          3520 + 8800  ~ 12.3 s
    //   profile off, 40 Hz (what ran until now)                        ~ 11.0 s
    //
    // The tightest of those still leaves 2.7 s. Recheck this arithmetic before lowering
    // VFD_FREQUENCY_FULL_HZ or widening the slow phases much further.
    static constexpr uint32_t kMovementTimeoutMs = 15000;

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
    // number of milliseconds the gate would need to cover it at kSpeedReferenceHz.
    //
    // At a constant speed the two are the same thing, which is why the firmware could get
    // away with comparing elapsed time against targetRunTimeMs so far. As soon as the speed
    // can change during a movement they part ways, and every constant in the firmware
    // ('open for 1900 ms', 'a full run takes 10000 ms') means the distance, never the clock.
    // Integrating speed x time keeps all of those constants valid unchanged.
    //
    // This is also exactly the quantity encoders would provide directly (ROADMAP 3.2): swap
    // the integration in updateTravel() for a pulse count and nothing else has to change.
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

    // note: a 'defaultFrequency' member used to sit here, claiming 50 Hz. Nothing ever read
    //       it - the speed really written to the drive came from a separate constant saying
    //       40. Removed; the speeds above are the ones.
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
    // Running mean of the same runs measured as DISTANCE (travel-ms at kSpeedReferenceHz).
    // This is the one the speed profile needs: wall-clock time depends on which speeds the
    // movement happened to use, distance does not.
    uint32_t measuredFullTravelDistanceMs = 0;

public:
    // How far this gate really travels, limit switch to limit switch, in travel-ms.
    // The measured average as soon as there is one, the configured constant until then.
    //
    // Everything that needs to know where the gate is goes through here rather than reading
    // runDurationMs directly. That constant is hand-measured and was 13-21% too large, which
    // put the whole 'last N ms' window past the point where the gate actually stops - so the
    // final approach barely happened. Measuring it makes that self-correcting.
    uint32_t effectiveFullTravelMs() const {
        return (measuredFullTravelCount > 0) ? measuredFullTravelDistanceMs : runDurationMs;
    };

private:

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
