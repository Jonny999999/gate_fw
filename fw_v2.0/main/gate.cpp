#include "gate.hpp"
#include "timing.hpp"



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
           uint32_t runDurationMs) : name(name),
                                     kLimitSwitchOpenGpio(kLimitSwitchOpenGpio),
                                     kLimitSwitchOpen_ActiveLevel(kLimitSwitchOpen_ActiveLevel),
                                     kLimitSwitchClosedGpio(kLimitSwitchClosedGpio),
                                     kLimitSwitchClosed_ActiveLevel(kLimitSwitchClosed_ActiveLevel),
                                     kRelayPinGpio(kRelayPinGpio),
                                     vfd(vfd),
                                     runDurationMs(runDurationMs),

                                     state(IDLE_PARTIALLY_OPEN), relayTimeoutActive(false), relayOn(false), positionPercent(0.0f)
                                     // note: state and positionPercent are set properly from the limit switches in the body
{
    gpio_set_direction(kLimitSwitchOpenGpio, GPIO_MODE_INPUT);
    gpio_set_direction(kLimitSwitchClosedGpio, GPIO_MODE_INPUT);
    gpio_set_direction(kRelayPinGpio, GPIO_MODE_OUTPUT);
    gpio_set_level(kRelayPinGpio, 0); // VFD stays unpowered until a movement is requested

    // Determine the actual starting state from the limit switches.
    // The state used to be hard-coded to IDLE_FULLY_CLOSED, so whenever the gate was not
    // actually closed the first handle() "discovered" the difference and reported it as a
    // limit switch change - beeping on every boot. That is the normal case right after
    // flashing, which requires the east gate to be slightly open.
    const bool openSwitchActive = (gpio_get_level(kLimitSwitchOpenGpio) == kLimitSwitchOpen_ActiveLevel);
    const bool closedSwitchActive = (gpio_get_level(kLimitSwitchClosedGpio) == kLimitSwitchClosed_ActiveLevel);
    prevOpenSwitchState = openSwitchActive;
    prevClosedSwitchState = closedSwitchActive;

    if (openSwitchActive && closedSwitchActive)
    {
        // Physically impossible - a switch or its wiring is broken. Assume the gate is
        // somewhere in between, which is the assumption that allows movement in both
        // directions; the limit switches are checked again before and during every move.
        ESP_LOGE(name, "BOTH limit switches report active at startup - check switches/wiring!");
        state = IDLE_PARTIALLY_OPEN;
        positionPercent = 50.0f;
    }
    else if (openSwitchActive)
    {
        state = IDLE_FULLY_OPEN;
        positionPercent = 100.0f;
    }
    else if (closedSwitchActive)
    {
        state = IDLE_FULLY_CLOSED;
        positionPercent = 0.0f;
    }
    else
    {
        // Somewhere in between - the real position is unknown until a limit switch is
        // reached, so the estimate is a placeholder. Every full movement runs against a
        // limit switch, which recalibrates it.
        state = IDLE_PARTIALLY_OPEN;
        positionPercent = 50.0f;
    }

    ESP_LOGW(name, "GPIO pins and variables initialized - starting in state %s (pos ~%.0f%%)",
             GateState_str[(int)state], positionPercent);
    // initially start VFD immediately after startup (?):
    // startRelay();
}



void Gate::startRelay() {
    if (!relayOn) {
        ESP_LOGI(name, "Turning relay on...");
        gpio_set_level(kRelayPinGpio, 1);
        timestampRelayTurnedOnUs = micros();
        relayOn = true;
        //ESP_LOGI(name, "Waiting %d ms for VFD to boot up...", DELAY_VFD_STARTUP);
        //state = WAITING_FOR_VFD_STARTUP;
        // Delay to allow the VFD to boot up.
        //vTaskDelay(pdMS_TO_TICKS(DELAY_VFD_STARTUP));
        // note: optimized to not blocking delay, waiting in state WAITING_FOR_VFD_STARTUP
    }
    lastActivityTimestampUs = micros();
    relayTimeoutActive = false;
}


void Gate::softStopRelay() {
    relayTimeoutActive = true;
    lastActivityTimestampUs = micros();
    ESP_LOGI(name, "Relay soft stop initiated.");
}


void Gate::forceStopRelay() {
    gpio_set_level(kRelayPinGpio, 0);
    relayOn = false;
    relayTimeoutActive = false;
    ESP_LOGW(name, "Relay forced OFF.");
}



void Gate::updateTravel() {
    const uint64_t currentTimeUs = micros();
    const uint64_t elapsedUs = currentTimeUs - lastPositionUpdateTimestampUs;
    lastPositionUpdateTimestampUs = currentTimeUs;

    if (state != MOVING_OPENING && state != MOVING_CLOSING)
        return;  // nothing is moving, so nothing was covered

    // Distance covered in this slice, measured at the reference speed.
    // At kSpeedReferenceHz this is simply the elapsed time, which is what the whole firmware
    // assumed so far; slower covers proportionally less ground per millisecond, faster more.
    //
    // Known inaccuracy: the drive does not change speed instantly, it ramps over its own
    // configured accel/decel time, while this counts the new speed from the moment the
    // command was sent. Each speed change therefore over- or under-states the distance by
    // roughly half the ramp time times the speed difference - a few hundred ms of travel at
    // most, and only twice per movement. Well inside what a time-based estimate is worth;
    // if it turns out to matter, that is an argument for encoders (ROADMAP 3.2), not for a
    // more elaborate model of the drive.
    const uint64_t distanceUs = elapsedUs * currentSpeedHz / kSpeedReferenceHz;
    travelledDistanceUs += distanceUs;

    const float deltaPercent = (distanceUs / 1000.0f / effectiveFullTravelMs()) * 100.0f;
    if (state == MOVING_OPENING) {
        positionPercent = std::min(100.0f, positionPercent + deltaPercent);
    } else {
        positionPercent = std::max(0.0f, positionPercent - deltaPercent);
    }
    ESP_LOGD(name, "Travel updated: pos=%.2f%%, distance=%lu ms at %u Hz (+%lu us)",
             positionPercent, (unsigned long)travelledDistanceMs(), currentSpeedHz,
             (unsigned long)distanceUs);
}


void Gate::setSpeed(uint16_t frequencyHz) {
    if (frequencyHz == currentSpeedHz)
        return;
    // Settle the distance covered so far BEFORE switching, so the stretch just completed is
    // counted at the speed it was actually driven at.
    updateTravel();
    ESP_LOGI(name, "Changing speed %u Hz -> %u Hz (at %lu ms travelled, pos ~%.0f%%)",
             currentSpeedHz, frequencyHz, (unsigned long)travelledDistanceMs(), positionPercent);
    // A failed frequency write is deliberately not fatal, for the same reason as in
    // startMovement(): the drive simply keeps the speed it has and the movement stays
    // correct, only less gentle. currentSpeedHz is therefore only updated on success,
    // so the distance integration keeps matching what the motor is really doing.
    if (vfd->setFrequency(frequencyHz) == ESP_OK)
        currentSpeedHz = frequencyHz;
    else
        ESP_LOGW(name, "Could not change the speed - continuing at %u Hz", currentSpeedHz);
}



void Gate::recomputeExpectedTravelDistance() {
    // Distance from where the gate is now to the end it is heading for.
    // nextDirection is set by startMovement() for every movement and stays valid for its
    // whole duration, including while the VFD is still booting.
    const float fractionLeftPercent = nextDirection ? (100.0f - positionPercent) : positionPercent;
    const uint32_t distanceToLimitMs = (uint32_t)(fractionLeftPercent / 100.0f * effectiveFullTravelMs());

    // The LIMIT SWITCH, and only the limit switch. Not the requested target.
    //
    // Slowing down is about how the gate arrives at the mechanical end stop - that is what
    // takes the abuse. A partial opening ends because the target distance was reached, with
    // nothing in front of the gate: there the motor simply switches off and the drive's own
    // deceleration ramp does the rest, exactly as it always has. Timing the approach against
    // the target instead made every partial movement crawl for no reason - the pedestrian
    // gap is shorter than the two slow phases together, so it ran slowly from end to end.
    //
    // A partial movement that happens to finish near a limit switch still slows down, which
    // is correct: what matters is the distance to the stop, not why the gate is moving.
    expectedTravelDistanceMs = travelledDistanceMs() + distanceToLimitMs;

    ESP_LOGD(name, "Expecting the limit switch after %lu ms of travel (%lu from here); target is %llu",
             (unsigned long)expectedTravelDistanceMs, (unsigned long)distanceToLimitMs, targetRunTimeMs);
}


void Gate::updateSpeedProfile() {
#if VARIABLE_SPEED_ENABLED
    uint16_t desiredSpeedHz = kSpeedFullHz;

    // 1. gentle start - the first stretch out of standstill
    if (travelledDistanceMs() < kSlowStartDistanceMs) {
        desiredSpeedHz = kSpeedSlowHz;
    }
    // 2. final approach - the last stretch before the movement is expected to end.
    //    Written as '<' on the sum rather than a subtraction, so it is also true once the
    //    gate has travelled PAST where the end was expected: if the limit switch is late or
    //    missed entirely, the gate keeps creeping rather than accelerating back to full
    //    speed into the limit stop. That is the safe direction for the estimate to be wrong
    //    in, and the reason a rough position estimate is good enough here.
    else if (expectedTravelDistanceMs < travelledDistanceMs() + kSlowApproachDistanceMs) {
        desiredSpeedHz = kSpeedSlowHz;
    }

    setSpeed(desiredSpeedHz);
#endif
}


bool Gate::checkLimitSwitchOpenActive() {
    bool current = (gpio_get_level(kLimitSwitchOpenGpio) == kLimitSwitchOpen_ActiveLevel);
    if (current != prevOpenSwitchState) {
        ESP_LOGI(name, "Open limit switch changed to %s.", current ? "ACTIVE" : "INACTIVE");
        prevOpenSwitchState = current;
    }
    return current;
}


void Gate::reportFullTravelIfMeasured() {
    if (!movementStartedAtOppositeLimit)
        return;  // started somewhere in the middle - says nothing about the full travel
    movementStartedAtOppositeLimit = false;  // report once per movement

    const uint32_t measuredMs = (uint32_t)((micros() - timestampStartUs) / 1000);
    const uint32_t measuredDistanceMs = travelledDistanceMs();

    // Plausibility: a movement that was paused and resumed while the gate had not yet left
    // the limit switch also starts "on" it, but timestampStartUs then only covers the
    // resumed part. Anything far short of the expected travel is not a full run.
    // Checked on the distance, so the verdict does not depend on which speeds were used.
    if (measuredDistanceMs < effectiveFullTravelMs() / 2) {
        ESP_LOGD(name, "Ignoring implausible full travel measurement of %lu ms of travel",
                 (unsigned long)measuredDistanceMs);
        return;
    }

    // Deviation from whatever the gate was working with before this run.
    const uint32_t previousFullTravelMs = effectiveFullTravelMs();
    const float deviationPercent =
        ((float)measuredDistanceMs - (float)previousFullTravelMs) / previousFullTravelMs * 100.0f;

    // Running means, computed in place so no history has to be kept.
    measuredFullTravelCount++;
    measuredFullTravelAverageMs += ((int32_t)measuredMs - (int32_t)measuredFullTravelAverageMs)
                                   / (int32_t)measuredFullTravelCount;
    measuredFullTravelDistanceMs += ((int32_t)measuredDistanceMs - (int32_t)measuredFullTravelDistanceMs)
                                    / (int32_t)measuredFullTravelCount;

    ESP_LOGW(name, "FULL TRAVEL measured: %lu ms of travel (%lu ms wall clock), "
                   "was working with %lu -> %+.1f%%. Now using %lu ms (average of %lu runs)",
             (unsigned long)measuredDistanceMs, (unsigned long)measuredMs,
             (unsigned long)previousFullTravelMs, deviationPercent,
             (unsigned long)effectiveFullTravelMs(), (unsigned long)measuredFullTravelCount);
}


bool Gate::checkCurrentLimitExceeded() {
    // note: on a failed modbus read getCurrent() leaves the output untouched.
    // It used to be read uninitialised here and the error was ignored, so random stack
    // content could exceed the limit and abort a closing movement with ERROR_STATE.
    // A failed reading is now treated as 'limit not exceeded' - the movement timeout and
    // the limit switches still protect the gate.
    float vfdCurrent = 0.0f;
    esp_err_t err = vfd->getCurrent(&vfdCurrent);
    if (err != ESP_OK) {
        ESP_LOGW(name, "Could not read VFD current (error 0x%x) - skipping current limit check", err);
        return false;
    }

    // The threshold belongs to the speed the motor is running at, not to the gate.
    const float limitAmpere = (currentSpeedHz == kSpeedFullHz) ? kCurrentLimitAmpere
                                                              : kCurrentLimitSlowAmpere;
    #ifdef LOG_VFD_CURRENT_WHEN_CLOSING
        ESP_LOGI(name, "Closing at %u Hz (%lu ms travelled): current %05.2f A, limit %05.2f A",
                 currentSpeedHz, (unsigned long)travelledDistanceMs(), vfdCurrent, limitAmpere);
    #else
        ESP_LOGD(name, "VFD current: %05.2f A at %u Hz", vfdCurrent, currentSpeedHz);
    #endif
    return (vfdCurrent > limitAmpere);
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
    movementWasRefused = false; // a movement is actually happening
    // when relay is off we need to wait for vfd startup first...
    nextDirection = opening; // store target direction (used in case of waiting for VFD or when pause/resuming)
    // wait for the VFD to finish booting when the relay is off, or was switched on only just now
    // (note: timestampRelayTurnedOnUs is in microseconds, DELAY_VFD_STARTUP in milliseconds)
    if (!relayOn || ((micros() - timestampRelayTurnedOnUs) < (uint64_t)DELAY_VFD_STARTUP * 1000)) {
        startRelay();
        ESP_LOGI(name, "Waiting %d ms for VFD to boot up in state WAITING_FOR_VFD_STARTUP first...", DELAY_VFD_STARTUP);
        state = WAITING_FOR_VFD_STARTUP;
        // Nothing has been travelled yet, and the leftover from the PREVIOUS movement must
        // not be readable in the meantime: the control task shortens a movement right after
        // requesting it (the pedestrian gap), and updateTargetRunTime() computes the
        // expected end from the distance covered so far.
        travelledDistanceUs = 0;
        recomputeExpectedTravelDistance();
        return;
    }

    // 1. Set the speed the movement starts at.
    //    With the speed profile that is the slow one, set here rather than left to the
    //    first updateSpeedProfile(): the gate should leave standstill gently, not jerk into
    //    motion and step down a cycle later. Without the profile it is the full speed,
    //    exactly as before.
    //    A failure here is deliberately NOT fatal: the drive keeps the frequency it already
    //    has, and that is the same value written on every start, so the movement is still
    //    correct. Refusing to move because of it would turn a harmless glitch into a gate
    //    that does not open. currentSpeedHz is therefore left at what was requested - the
    //    drive is expected to have it, and the next phase change writes it again anyway.
#if VARIABLE_SPEED_ENABLED
    currentSpeedHz = kSpeedSlowHz;
#else
    currentSpeedHz = kSpeedFullHz;
#endif
    if (vfd->setFrequency(currentSpeedHz) != ESP_OK)
        ESP_LOGW(name, "Could not set the VFD frequency - continuing with the one the drive already has");

    // 2. Start the motor, repeating the command if the drive does not confirm it.
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= kStartAttempts; attempt++) {
        err = vfd->start(opening);
        if (err == ESP_OK)
            break;
        ESP_LOGW(name, "VFD start attempt %d of %d failed (error 0x%x)", attempt, kStartAttempts, err);
    }

    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        // After this many attempts we no longer know what the drive is doing: it may well
        // have received a start command whose reply was lost, in which case the motor is
        // turning and nothing here is tracking it any more - no target run time, no limit
        // switch check. Cutting the supply is the only action that is correct either way.
        ESP_LOGE(name, "VFD did not start after %d attempts -> cutting the VFD supply", kStartAttempts);
        indicatorBeep(BuzzerSignal::FAULT);
        indicatorSetFault(FaultCode::VFD_COMMUNICATION);
        forceStopRelay();
        // switch to ERROR state to prevent an infinite loop of retries
        state = ERROR_STATE;
        errorLatched = true;
        return;
    }
    #endif
    // Take both timestamps AFTER the (blocking) modbus commands returned, so they refer to
    // the moment the motor actually starts turning.
    // Note: lastPositionUpdateTimestampUs must be seeded here. It used to be assigned from
    // timestampStartUs BEFORE that was refreshed, i.e. from the previous movement, so the
    // first updateTravel() integrated the whole idle time in between and slammed the
    // position estimate to 0% / 100%.
    timestampStartUs = micros();
    lastPositionUpdateTimestampUs = timestampStartUs;
    // The distance covered belongs to THIS movement only, and counts from the moment the
    // motor starts - not from when the command arrived, which may have been a VFD startup
    // delay earlier.
    travelledDistanceUs = 0;

    // Only a movement that starts ON the far limit switch can measure the whole rail.
    // Note this reads the switch the gate is moving AWAY from - opening starts at closed.
    movementStartedAtOppositeLimit = opening ? checkLimitSwitchClosedActive()
                                             : checkLimitSwitchOpenActive();

    // Where this movement is expected to end - the final approach is timed against it.
    recomputeExpectedTravelDistance();

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
        targetRunTimeMs = getFullMovementRunTimeMs();
    } else if (target >= 100.0f) {
        target = 100.0f;
        targetRunTimeMs = getFullMovementRunTimeMs();
    } else {
        float delta = std::abs(target - positionPercent);
        targetRunTimeMs = (uint64_t)((delta / 100.0f) * effectiveFullTravelMs() );
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
        movementWasRefused = true;
        return;
    }
    targetRunTimeMs = getFullMovementRunTimeMs(); // run longer than necessary, the limit switch stops the movement
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
        movementWasRefused = true;
        return;
    }
    targetRunTimeMs = getFullMovementRunTimeMs(); // run longer than necessary, the limit switch stops the movement
    startMovement(false);
}


// Public method: update target run time e.g. while already opening
void Gate::updateTargetRunTime(uint32_t ms) {
    // Clamp to what a full movement is allowed to take. Without this, enough repeated
    // presses (each adding BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS) produce a target longer
    // than a full movement, so the gate would stop with a MOVEMENT_TIMEOUT fault
    // instead of simply opening as far as it can.
    const uint32_t maxRunTimeMs = getFullMovementRunTimeMs();
    if (ms > maxRunTimeMs) {
        ESP_LOGW(name, "Requested run time %lu ms is longer than a full movement -> clamping to %lu ms",
                 (unsigned long)ms, (unsigned long)maxRunTimeMs);
        ms = maxRunTimeMs;
    }
    ESP_LOGI(name, "Received command: To update target run duration from %llu to %lu",
             targetRunTimeMs, (unsigned long)ms);
    targetRunTimeMs = ms;
    // The movement now ends somewhere else, so the final approach has to be re-timed.
    // This is the normal case for a pedestrian gap: the control task opens completely and
    // shortens the movement immediately afterwards.
    recomputeExpectedTravelDistance();
}


void Gate::stop(bool forceStatePartialOpen){ // default true
    ESP_LOGI(name, "stopping gate, turning off motor (current state=%s)", GateState_str[(int)state]);
    esp_err_t err = vfd->stop();
    #ifndef IGNORE_VFD_ERROR
    if (err != ESP_OK) {
        ESP_LOGE(name, "VFD stop failed! -> forcing relay off");
        indicatorBeep(BuzzerSignal::FAULT);
        indicatorSetFault(FaultCode::VFD_COMMUNICATION);
        forceStopRelay();
        state = ERROR_STATE;
        errorLatched = true;
        return;
    }
    #endif
    softStopRelay();

    // by default set state to partially open, when stopped due to limit switch the state is set manually
    if (forceStatePartialOpen)
        state = IDLE_PARTIALLY_OPEN;

    // Do not change state here; let the state machine (handle()) update it.
}



// Public method: pause movement
// Stops the motor but remembers direction and remaining run time, so resume() can
// continue the same movement (used while the light barrier is obstructed).
void Gate::pause() {
    if (getIsMoving() == false) {
        ESP_LOGE(name, "Pause requested, but gate is not moving.");
        return;
    }
    ESP_LOGW(name, "Pause requested, stopping gate...");

    // 1. capture everything that describes the interrupted movement.
    //    This MUST happen before stop(), because stop() overwrites state with
    //    IDLE_PARTIALLY_OPEN - reading the direction afterwards always yielded false,
    //    so resume() used to restart every paused movement in the CLOSING direction.
    const bool movementHadAlreadyStarted = (state == MOVING_OPENING || state == MOVING_CLOSING);
    //    while waiting for the VFD to boot the direction is only known via nextDirection
    wasOpeningBeforePause = movementHadAlreadyStarted ? (state == MOVING_OPENING) : nextDirection;
    pauseStartTimestampUs = micros();

    // 2. work out how much of the movement is left before the motor is switched off.
    //    'Left' is a DISTANCE, so settle what has been covered and subtract it from the
    //    target - which stays correct no matter what speed the movement was running at.
    //    If the motor never actually started (still waiting for the VFD), the full target
    //    is still ahead; the travelled distance would still belong to the PREVIOUS movement.
    if (!movementHadAlreadyStarted) {
        remainingRunTimeAtPauseMs = targetRunTimeMs;
    } else {
        updateTravel();
        remainingRunTimeAtPauseMs = (targetRunTimeMs > travelledDistanceMs())
                                        ? (targetRunTimeMs - travelledDistanceMs())
                                        : 0;
    }

    // 3. stop the motor
    stop();
    state = PAUSED_STATE;
    softStopRelay();

    ESP_LOGW(name, "Gate PAUSED while %s. Remaining distance to target: %llu ms",
             wasOpeningBeforePause ? "OPENING" : "CLOSING", remainingRunTimeAtPauseMs);
}

// Public method: resume movement
// Continues the movement that pause() interrupted, for the remaining run time.
void Gate::resume() {
    if (state != PAUSED_STATE) {
        ESP_LOGW(name, "Resume requested, but gate is not paused -> ignoring");
        return;
    }

    // note: the remaining run time was calculated in pause() - recomputing it here from
    // targetRunTimeMs would underflow (unsigned!) whenever the elapsed time already
    // exceeded the target, resulting in an enormous run time instead of a stop.
    if (remainingRunTimeAtPauseMs == 0) {
        ESP_LOGW(name, "No remaining run time, cannot resume -> switching to idle");
        state = IDLE_PARTIALLY_OPEN;
        return;
    }

    targetRunTimeMs = remainingRunTimeAtPauseMs;
    ESP_LOGI(name, "Resuming gate movement (%s). Remaining target run time: %llu ms",
             wasOpeningBeforePause ? "OPENING" : "CLOSING", targetRunTimeMs);
    startMovement(wasOpeningBeforePause);
}

// Public method: cancel movement
void Gate::cancel() {
    ESP_LOGI(name, "Cancel requested. Stopping gate immediately");
    stop(true);
    state = IDLE_PARTIALLY_OPEN;
}





void Gate::handle() {
    // Read both limit switches into locals first.
    // checkLimitSwitch*Active() have side effects (they log changes and update
    // prevOpen/prevClosedSwitchState), so they must not be called from inside a log macro:
    // ESP_LOGV is compiled out at the configured log level, which would silently change
    // behaviour depending on the build configuration.
    const bool limitSwitchOpenActive = checkLimitSwitchOpenActive();
    const bool limitSwitchClosedActive = checkLimitSwitchClosedActive();
    ESP_LOGV(name, "handle() - state=%s,  pos=%.2f, limitOpen=%d, limitClosed=%d", GateState_str[(int)state], positionPercent, limitSwitchOpenActive, limitSwitchClosedActive);

    uint64_t currentTimeUs = micros();

    // Check for relay inactivity in idle states. TODO: verify in idle states?
    // note: the cast matters. kRelayInactivityTimeoutMs * 1000 is 32 bit arithmetic and
    //       wraps for anything above ~71 minutes - the 3 h setting actually expired after
    //       ~37 minutes while the log dutifully printed 10800 s.
    if (relayTimeoutActive && (currentTimeUs - lastActivityTimestampUs > (uint64_t)kRelayInactivityTimeoutMs * 1000))
    {
        ESP_LOGW(name, "Relay inactivity timeout of %lu s reached. Forcing relay OFF...", (unsigned long)(kRelayInactivityTimeoutMs / 1000));
        forceStopRelay();
    }

    // Main state machine for all gate states.
    switch (state)
    {
    case MOVING_OPENING:
    {
        updateTravel();
        updateSpeedProfile();
        // timeout
        if ((currentTimeUs - timestampStartUs) >= ((uint64_t)getMovementTimeoutMs() * 1000))
        {
            ESP_LOGE(name, "Open movement timeout exceeded.");
            indicatorSetFault(FaultCode::MOVEMENT_TIMEOUT);
            stop(false);
            state = ERROR_STATE;
            errorLatched = true;
            break;
        }
        // limit reached
        else if (checkLimitSwitchOpenActive())
        {
            ESP_LOGI(name, "Open limit switch triggered.");
            // measure before stop(), which spends ~100 ms on modbus
            reportFullTravelIfMeasured();
            positionPercent = 100.0f; // Calibrate position.
            stop(false);
            state = IDLE_FULLY_OPEN;
            break;
        }
        // target reached - compared against the DISTANCE covered, not the time elapsed,
        // so a movement that ran slower than full speed still travels the same distance
        else if (travelledDistanceMs() >= targetRunTimeMs)
        {
            ESP_LOGI(name, "Target distance reached (while opening).");
            stop(false);
            state = IDLE_PARTIALLY_OPEN;
        }
        break;
    }
    case MOVING_CLOSING:
    {
        updateTravel();
        updateSpeedProfile();
        // note: the motor current is logged by checkCurrentLimitExceeded() below, which
        // already reads it. A second read here only doubled the modbus traffic on the one
        // path that is busiest with it.

        // timeout
        if ((currentTimeUs - timestampStartUs) >= ((uint64_t)getMovementTimeoutMs() * 1000))
        {
            ESP_LOGE(name, "Close movement timeout exceeded.");
            indicatorSetFault(FaultCode::MOVEMENT_TIMEOUT);
            stop(false);
            state = ERROR_STATE;
            errorLatched = true;
            break;
        }
        // limit reached
        else if (checkLimitSwitchClosedActive())
        {
            ESP_LOGI(name, "Close limit switch triggered.");
            // measure before stop(), which spends ~100 ms on modbus
            reportFullTravelIfMeasured();
            positionPercent = 0.0f; // Calibrate position.
            stop(false);
            state = IDLE_FULLY_CLOSED;
            break;
        }
        // target reached - see the note in MOVING_OPENING
        else if (travelledDistanceMs() >= targetRunTimeMs)
        {
            ESP_LOGI(name, "Target distance reached (while closing).");
            stop(false);
            state = IDLE_PARTIALLY_OPEN;
        }
        #ifdef CURRENT_MONITORING_ENABLED
        else if (checkCurrentLimitExceeded()){
            ESP_LOGE(name, "Max vfd current exceeded while closing! stopping");
            stop(false);
            indicatorBeep(BuzzerSignal::OBSTRUCTION_DETECTED);
            indicatorSetFault(FaultCode::OBSTRUCTION);
            state = ERROR_STATE;
            errorLatched = true;
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
            indicatorBeep(BuzzerSignal::LIMIT_SWITCH_REACHED);
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
            indicatorBeep(BuzzerSignal::LIMIT_SWITCH_REACHED);
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
            indicatorBeep(BuzzerSignal::LIMIT_SWITCH_REACHED);
            #endif

        }
        else if (checkLimitSwitchClosedActive())
        {
            state = IDLE_FULLY_CLOSED;
            positionPercent = 0.0f;
            ESP_LOGW(name, "Gate reached fully closed position while off -> switching to IDLE_FULLY_CLOSED");
            #ifdef BEEP_AT_LIMIT_SW_CHANGE
            indicatorBeep(BuzzerSignal::LIMIT_SWITCH_REACHED);
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
        if ((micros() - pauseStartTimestampUs)/1000 > PAUSED_SWITCH_TO_IDLE_TIMEOUT_MS ){
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


