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

    // How far this gate travels between its limit switches, in travel-ms at the reference
    // speed. The learned value, which starts out as the configured constant - see the notes
    // on the learning members below.
    uint32_t effectiveFullTravelMs() const {return fullTravelMs;};

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

    // The slowest speed a movement can legitimately run at. What the wall-clock backstop
    // below has to allow for - a movement is not late just because it ran slowly.
#if VARIABLE_SPEED_ENABLED
    static constexpr uint16_t kSlowestSpeedHz = kSpeedSlowHz;
#else
    // no profile, but an uncalibrated gate is still capped to the reference speed
    static constexpr uint16_t kSlowestSpeedHz = std::min(kSpeedFullHz, kSpeedReferenceHz);
#endif

    // Guaranteed gap between the target run time and the safety backstop.
    // A full movement that misses its limit switch should stop cleanly on its target
    // distance, not trip the error path at the same instant. (ROADMAP.md B11)
    static constexpr uint32_t kMovementTimeoutMarginMs = 2000;

    // How much further than the full travel a full open/close is allowed to run, so the
    // limit switch is definitely reached.
    //
    // Was 5000, sized for a hand-measured travel constant that turned out to be 13-21% off.
    // effectiveFullTravelMs() is measured now and errs on the long side by construction (the
    // integration counts each commanded speed from the moment it was asked for, while the
    // drive still has to ramp to it), so the allowance no longer has to cover a bad guess -
    // and it is the distance the gate may spend pushing against the end stop if a limit
    // switch fails. At the slow speed 5000 would have been 8 s of that.
    static constexpr uint32_t kFullMovementExtraTimeMs = 2000;

    // Absolute ceiling for the computed backstop, so a nonsense learned travel value can
    // never stretch it indefinitely.
    static constexpr uint32_t kMovementTimeoutCeilingMs = 25000;

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
    // Whether positionPercent traces back to a limit switch, or is only a guess.
    //
    // False after a boot with the gate parked somewhere in the middle (the normal state
    // right after flashing, which needs the east gate slightly open), and again whenever a
    // gate leaves a limit switch while the motor is off - it was pushed by hand, and how
    // far is not knowable. In both cases the estimate can be wrong by any amount, so the
    // final approach may simply not happen and the gate would meet the end stop at the full
    // speed. See updateSpeedProfile(): until a limit switch confirms where the gate is, it
    // runs no faster than the reference speed it ran at for years.
    bool positionIsCalibrated = false;
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

    //=====================================================
    //===== Learned full travel (ROADMAP 2.6) =============
    //=====================================================
    // How far this gate really travels, limit switch to limit switch, in travel-ms at
    // kSpeedReferenceHz. EVERYTHING that scales a position goes through
    // effectiveFullTravelMs() rather than reading the configured constant, because that
    // constant is hand-measured and was 13-21% too large - which put the whole final
    // approach past the point where the gate actually stops.
    //
    // A movement that starts on one limit switch and ends on the other has covered exactly
    // the rail, so its travelled distance IS this value, measured under real conditions.
    // Three rules guard what is allowed to become one:
    //
    //   1. only an UNINTERRUPTED full run counts (measurementIsValid). Started on one limit
    //      switch, ended on the other, nothing in between - no light barrier pause, no stop,
    //      no cancel. A resumed movement only measures the part after the resume, and a
    //      pause while the gate still sits on the switch would otherwise pass unnoticed
    //      because the movement still "starts on a limit switch".
    //   2. only a measurement inside a tolerance band around the CONFIGURED constant counts.
    //      Anchoring the band to the constant rather than to the current estimate is what
    //      makes drift impossible: the learned value cannot walk away from the hand-measured
    //      reality one small accepted step at a time, however many runs go by. It also means
    //      correcting the constant in main.cpp re-bounds a stored value, which is the reset
    //      mechanism if one is ever needed.
    //   3. an accepted measurement is weighted in, never adopted outright, so one odd run
    //      cannot move the gate's behaviour much.
    bool movementStartedAtOppositeLimit = false; // set when the motor starts, see startMovement()
    bool measurementIsValid = false;             // cleared by anything that interrupts the run

    uint32_t fullTravelMs;                    // current best estimate (set in the constructor)
    uint32_t measuredFullTravelCount = 0;     // accepted measurements since boot
    uint32_t measuredFullTravelAverageMs = 0; // the same runs as wall-clock ms, for the log

    // Weight of a new measurement: 1/8, so the estimate converges within about ten runs but
    // no single run moves it by more than ~12%. A plain running mean was worse in both
    // directions - it adopted the very first measurement outright, and after a few thousand
    // runs a new one shifted it by well under a millisecond, freezing it at whatever the
    // gate was like in its first weeks.
    static constexpr int32_t kFullTravelAverageDivisor = 8;

    // How far a measurement may be from the configured constant and still be believed. Wide
    // enough for real seasonal and wear-related change, narrow enough that a mismeasured run
    // is rejected rather than learned.
    static constexpr float kFullTravelToleranceFraction = 0.25f;
    uint32_t minPlausibleFullTravelMs() const {
        return (uint32_t)(runDurationMs * (1.0f - kFullTravelToleranceFraction));
    };
    uint32_t maxPlausibleFullTravelMs() const {
        return (uint32_t)(runDurationMs * (1.0f + kFullTravelToleranceFraction));
    };

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

    // Target run time for a full open / close: far enough to reach the limit switch.
    // A DISTANCE, so it does not depend on the speeds the movement happens to use.
    uint32_t getFullMovementRunTimeMs() const {
        return effectiveFullTravelMs() + kFullMovementExtraTimeMs;
    };

    // Hard safety backstop: a movement that has not finished by then is aborted with an
    // error. The one genuinely wall-clock quantity in the movement logic, which is why it
    // has to be derived rather than fixed: how long the target distance takes depends on
    // the speed, and it is now up to 1.6x longer at the slow speed than at the reference.
    //
    // A fixed 15000 no longer worked. Two ways it broke:
    //   - a full movement whose position estimate says "nearly there" runs the whole rail
    //     at the slow speed: 9123 * 40/25 = 14.6 s, i.e. 0.4 s short of the old backstop.
    //     Real case - a gate pushed open by hand from the closed switch reports 1%.
    //   - the target distance itself was no longer reachable in time (13000 travel-ms at
    //     the slow speed is 20.8 s), so the error path fired instead of the clean stop on
    //     target that B11 was fixed to guarantee.
    //
    // Derived from the target distance at the slowest speed a movement can use, so that
    // guarantee holds again by construction at any speed. Note this bounds nothing else:
    // how far the gate may travel past a missed limit switch is kFullMovementExtraTimeMs,
    // not this. The backstop only has to catch a drive that is not doing what it was told.
    uint32_t getMovementTimeoutMs() const {
        const uint64_t worstCaseWallMs =
            (uint64_t)getFullMovementRunTimeMs() * kSpeedReferenceHz / kSlowestSpeedHz;
        return (uint32_t)std::min<uint64_t>(worstCaseWallMs + kMovementTimeoutMarginMs,
                                            kMovementTimeoutCeilingMs);
    };
    // Give up on measuring the current run, with a reason for the log. Called from
    // everything that can stop a movement before it reaches the far limit switch.
    void invalidateMeasurement(const char *reason);
    // Judge and, if it survives all three rules, learn the travel of the movement that just
    // ended. Called from handle() the moment the far limit switch is reached.
    void reportFullTravelIfMeasured();
    bool checkLimitSwitchOpenActive();
    bool checkLimitSwitchClosedActive();
    bool checkCurrentLimitExceeded();

    // Private movement helper: start movement in the given direction.
    // 'opening' = true means start opening, false means start closing.
    void startMovement(bool opening);

};

#endif // GATE_HPP
