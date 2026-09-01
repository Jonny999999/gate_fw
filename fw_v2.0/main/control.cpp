#include "control.hpp"
#include "esp_log.h"
#include "gate_task.hpp"
#include "indicator.hpp"
#include "input.hpp"
#include "timing.hpp"
#include <stdlib.h>

// TODO more delay in IDLE state / only fast when running?
#define CONTROL_LOOP_HANDLE_DELAY_MS 10

#define BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS 700 // V1: 400
#define BUTTON_PRESS_INITIAL_OPEN_TIME_MS 1900    // V1: 1100

// note: the 'open button held = open completely' threshold now lives in input.cpp
// (OPEN_BUTTON_LONG_PRESS_MS), where it is evaluated by the input sampling task.
// It must stay smaller than the input timeout, which is the min of the two values above.

//--- automatic closing ("let me through, then close behind me") ---
// Armed with: 1 short press (gate starts opening) + 1 long press.
//
// The gate does not simply wait a fixed time and then close. It waits until the light
// barrier has been CONTINUOUSLY FREE for this long, which is the actual question worth
// asking: has everybody gone through and is nothing else coming? Walking in and out a few
// times, or taking a while with a trailer, restarts the wait instead of racing a deadline.
//
// Two values, because the two openings are used for very different things:
//   partial - somebody walks through and is gone; a short clear time feels responsive
//   full    - the gate is opened, then one walks to the car, starts it and drives out.
//             The barrier may not be interrupted at all for a minute or more, so the clear
//             time has to be generous or the gate would close in front of the car.
#define AUTO_CLOSE_BARRIER_FREE_PARTIAL_MS 10000
#define AUTO_CLOSE_BARRIER_FREE_FULL_MS 120000
// The last part of that free period is announced with the same accelerating beep countdown
// that is used when the gate resumes after the light barrier cleared, so 'the gate is
// about to move by itself' always sounds the same.
#define AUTO_CLOSE_COUNTDOWN_MS 4000
// ... but do not wait forever: if the barrier stays obstructed this long without a break,
// whoever is there is clearly busy (unloading, parked in the gateway). Give up and leave
// the gate open rather than closing on them later.
#define AUTO_CLOSE_GIVE_UP_OBSTRUCTED_MS 20000

#define BARRIER_IS_IGNORED 0 // if 1 light-barrier is always considered free / not-obstructed
#define BARRIER_DELAY_BEFORE_RESTART_MS 4000 // time after which movement is resumed after barrier is free again
#define BARRIER_WAIT_FOR_FREE_TIMEOUT_MS 8000 // if barrier is obstructed longer than that continously the gate is no longer re-started after clear
#define BARRIER_BEEP_INTERVAL_MAX_MS 1000 // interval buzzer beeps when barrier just freed
#define BARRIER_BEEP_INTERVAL_MIN_MS 20  // interval buzzer beeps when movement is due

#define SHOW_BARRIER_STATE_ON_LED_IN_IDLE 1
#define DEBUG_BARRIER_BEEP_ON_CHANGE 0 // beep on every light-barrier change (sensor debugging)


//===============================
//========== Variables ==========
//===============================
// State definitions
enum class ControlState
{
    IDLE,
    WAIT_FOR_INPUT,
    MOVING_TO_TARGET,
    CLOSING_MOVEMENT_PAUSED,
    WAIT_AUTO_CLOSE // gate is open and closes again once the way stayed clear
};
const char *controlStateStr[] = {"IDLE", "WAIT_FOR_INPUT", "MOVING_TO_TARGET",
                                 "CLOSING_MOVEMENT_PAUSED", "WAIT_AUTO_CLOSE"};
// TODO: add and handle state e.g. LOCKED

// Control state variables
static ControlState ctlState = ControlState::IDLE;

// user input
static uint32_t timestampLastAction;
static uint8_t countPressed = 0;

// light barrier
static uint32_t timestampLastBarrierChange = 0;
static uint32_t timestampLastCountdownBeep;
static bool barrierIsObstructedWhileIdle = false; // updated in IDLE, shown on the LED

// automatic closing
static bool autoCloseIsArmed = false; // set while the current movement ends in WAIT_AUTO_CLOSE
static uint32_t autoCloseBarrierFreeMs = 0; // clear time required, depends on the opening size
// Set once the long press announced "open completely", while the button is still held.
// Releasing then simply opens; holding on escalates to opening AND closing again.
static bool fullOpenAnnounced = false;
// Light barrier as seen while waiting to close automatically, and when it last changed.
// One timestamp serves both directions: while free it measures how long the way has been
// clear, while obstructed it measures how long to keep waiting before giving up.
static bool autoCloseBarrierIsObstructed = false;
static uint32_t timestampAutoCloseBarrierChange = 0;

// GPIO assignment passed from main
ControlConfig *config;

// logging
static const char *TAG_CTL = "control";




//===============================
//========== Functions ==========
//===============================
// Light-barrier state as seen by the control logic.
// The raw pin is sampled and debounced by the input task; this wrapper only tracks WHEN
// the state last changed, because the control logic times its restart countdown from that
// and deliberately resets it when entering the paused state.
static bool lightBarrierIsObstructed(const InputState &input)
{
#if (BARRIER_IS_IGNORED)
    (void)input;
    return false;
#else
    static bool stateOld = false;
    const bool stateNew = input.lightBarrierIsObstructed;
    if (stateNew != stateOld)
    {
        #if DEBUG_BARRIER_BEEP_ON_CHANGE
            indicatorBeepCustom(1, 30, 0);
        #endif
        ESP_LOGW(TAG_CTL, "Info: light-barrier changed state to '%s'", stateNew ? "obstructed" : "free");
        stateOld = stateNew;
        timestampLastBarrierChange = millis();
    }
    return stateNew;
#endif
}



// Target run time for a partial opening: a base gap, widened by every additional press.
static uint32_t openTargetRunTimeMs(uint8_t additionalPresses)
{
    return BUTTON_PRESS_INITIAL_OPEN_TIME_MS + (BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS * additionalPresses);
}


// Begin closing both gates, from wherever the request came from (button, remote, or the
// automatic close). If the light barrier is interrupted the movement is not started yet -
// the gate waits in CLOSING_MOVEMENT_PAUSED until the way is clear.
// Returns the control state to continue in.
//
// playStartWarning is false for the automatic close: the beep countdown leading up to it
// has already announced the movement, a second warning tone would only be noise.
static ControlState startClosingGates(const InputState &input, bool playStartWarning)
{
    // a new movement clears the indication of the previous one
    clearGateErrors();
    indicatorClearFault();

    if (playStartWarning)
        indicatorBeep(BuzzerSignal::MOVEMENT_START_WARNING);

    if (lightBarrierIsObstructed(input))
    {
        indicatorBeep(BuzzerSignal::BARRIER_BLOCKED);
        ESP_LOGE(TAG_CTL, "Close requested but the barrier is obstructed -> waiting for it to clear");
        // Start the obstruction timeout now, not from whenever the barrier was last
        // interrupted - otherwise requesting a close while already standing in the barrier
        // times out immediately.
        timestampLastBarrierChange = millis();
        return ControlState::CLOSING_MOVEMENT_PAUSED;
    }

    gateSendCommand(GateCommandType::CLOSE_COMPLETELY);
    return ControlState::MOVING_TO_TARGET;
}


// Accelerating beep countdown, played whenever the gate is about to start moving on its
// own: the shorter the remaining time, the shorter the gap between beeps.
// Used both when resuming after the light barrier cleared and before an automatic close,
// so "the gate will move by itself in a moment" always sounds the same.
// Call once per control loop while waiting; it beeps when the next one is due.
static void handleCountdownBeeps(int32_t timeRemainingMs, uint32_t countdownDurationMs)
{
    if (timeRemainingMs <= (int32_t)BARRIER_BEEP_INTERVAL_MIN_MS || countdownDurationMs == 0)
        return;

    const uint32_t beepIntervalMs =
        BARRIER_BEEP_INTERVAL_MIN_MS +
        ((uint32_t)timeRemainingMs * (BARRIER_BEEP_INTERVAL_MAX_MS - BARRIER_BEEP_INTERVAL_MIN_MS)) /
            countdownDurationMs;

    if (millis() - timestampLastCountdownBeep < beepIntervalMs)
        return;

    const uint32_t buzzerOnDurationMs = std::min(beepIntervalMs, (uint32_t)70);
    ESP_LOGD(TAG_CTL, "countdown: %ld ms remaining, beep interval %lu ms", (long)timeRemainingMs, beepIntervalMs);
    indicatorBeepCustom(1, buzzerOnDurationMs, 0);
    timestampLastCountdownBeep = millis();
}


//================================
//========= Control Task =========
//================================
void controlTask(void *param)
{
    // extract parameters passed at task creation
    config = static_cast<ControlConfig *>(param);

    // Start the dedicated input sampling task (buttons, remote, light barrier)
    ESP_LOGI(TAG_CTL, "Starting input sampling task...");
    const InputPinConfig inputPins = {
        .openButtonGpio = config->buttonOpenGpio,
        .closeButtonGpio = config->buttonCloseGpio,
        .remoteOpenGpio = config->remoteOpenGpio,
        .remoteCloseGpio = config->remoteCloseGpio,
        .lightBarrierGpio = config->lightBarrierGpio,
    };
    inputStart(inputPins);

    ESP_LOGI(TAG_CTL, "Control task started");

    // control loop
    while (true)
    {
        // Collect everything that happened since the previous iteration.
        // Press events are queued by the input task, so none are lost even when the
        // modbus calls further down block this loop for a few hundred milliseconds.
        const InputState input = inputPoll();

        //--- the gate task stopped a closing movement because of the light barrier ---
        // The stop itself already happened, in the task that owns the motors (see
        // handleLightBarrierSafety() in gate_task.cpp). This only picks up the handling.
        //
        // Checked here rather than inside a single state on purpose: the whole point of
        // doing the stop in the gate task is that it does not depend on this state machine
        // being where we expect. Reacting to it from only one state would put that
        // assumption straight back in.
        if (gatesArePausedByLightBarrier() && ctlState != ControlState::CLOSING_MOVEMENT_PAUSED)
        {
            ESP_LOGE(TAG_CTL, "Gate was stopped by the light barrier (was in state %s) => waiting for the way to clear",
                     controlStateStr[(int)ctlState]);
            indicatorBeep(BuzzerSignal::BARRIER_BLOCKED);
            // start the obstruction timeout now, not from whenever the barrier last changed,
            // otherwise it can expire immediately
            timestampLastBarrierChange = millis();
            autoCloseIsArmed = false; // an interrupted close is not resumed automatically later
            ctlState = ControlState::CLOSING_MOVEMENT_PAUSED;
        }

        // State machine - control with button input according to current state
        switch (ctlState)
        {
            //--------------------
            //------- IDLE -------
            //--------------------
            // wait for initial user input
        case ControlState::IDLE:
            //--- button/remote close ---
            // close gates completely
            if (input.closeButtonPressed || input.remoteClosePressed)
            {
                ESP_LOGW(TAG_CTL, "%sclose Button pressed - Closing completely", 
                    input.remoteClosePressed ? "REMOTE-control " : "");
                timestampLastAction = millis();
                ctlState = startClosingGates(input, true);
            }
            //--- button open ---
            // Start opening straight away, then work out in WAIT_FOR_INPUT how far.
            //
            // The movement command is deliberately issued here, on the press, and NOT once
            // the gesture is known. Two reasons, both about not making the user wait:
            //   - the gate visibly reacts the moment the button is touched
            //   - more importantly it starts the VFDs charging up. The relay switches on
            //     immediately and DELAY_VFD_STARTUP (~870 ms) begins running now, so the
            //     whole gesture decision happens WHILE the drives boot instead of after.
            // Only the target run time is decided later, and that is measured from when the
            // motor actually starts turning, so a short gap stays a short gap regardless.
            // Do not "tidy this up" by deferring the command until the gesture is resolved.
            else if (input.openButtonPressed)
            {
                ESP_LOGW(TAG_CTL, "Opening (waiting for further input)");
                indicatorBeep(BuzzerSignal::BUTTON_ACKNOWLEDGED);
                gateSendCommand(GateCommandType::OPEN_COMPLETELY);
                clearGateErrors();
                indicatorClearFault();
                countPressed = 0;
                fullOpenAnnounced = false;
                timestampLastAction = millis();
                ctlState = ControlState::WAIT_FOR_INPUT;
            }
            //--- remote open ---
            // open gates completely
            else if (input.remoteOpenPressed)
            {
                ESP_LOGW(TAG_CTL, "REMOTE: Opening completely");
                indicatorBeep(BuzzerSignal::MOVEMENT_START_WARNING);
                gateSendCommand(GateCommandType::OPEN_COMPLETELY);
                clearGateErrors();
                indicatorClearFault();
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            //--- keep tracking the barrier while idle ---
            // (the call also maintains timestampLastBarrierChange, and the LED indication
            //  is derived from the control state at the end of the loop)
            barrierIsObstructedWhileIdle = lightBarrierIsObstructed(input);

            break;

            //--------------------------
            //----- WAIT_FOR_INPUT -----
            //--------------------------
            // wait for and process additional input
            //(decide what the user wants exactly while the gate already moves)
        case ControlState::WAIT_FOR_INPUT:
            //--- stop ---
            if (input.closeButtonIsHeld)
            { // close button is pressed while waiting for input
                ESP_LOGW(TAG_CTL, "Close button while waiting for input -> stopping gates");
                gateSendCommand(GateCommandType::STOP);
                indicatorBeep(BuzzerSignal::MOVEMENT_STOPPED);
                ctlState = ControlState::IDLE;
            }
            //--- keep holding the FIRST press: open completely AND close again afterwards ---
            // The second threshold on the same press. The user has already heard the
            // "opening completely" tone at the first threshold and chose to hold on.
            else if (input.openButtonVeryLongPress && countPressed == 0)
            {
                autoCloseIsArmed = true;
                autoCloseBarrierFreeMs = AUTO_CLOSE_BARRIER_FREE_FULL_MS;
                fullOpenAnnounced = false;
                ESP_LOGW(TAG_CTL, "very long press -> Opening completely, closing once the barrier stayed free for %d ms",
                         AUTO_CLOSE_BARRIER_FREE_FULL_MS);
                indicatorBeep(BuzzerSignal::AUTO_CLOSE_ARMED);
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            //--- long press: open completely, or arm the automatic close ---
            // Which one depends on whether this is still the FIRST press:
            //   press and keep holding            -> open completely (as before)
            //   short press, release, press+hold  -> "let me through, close behind me"
            // The two are told apart by countPressed, so the familiar "hold it down to open
            // the gate fully" gesture keeps working exactly as it did.
            else if (input.openButtonLongPress)
            {
                if (countPressed == 0)
                {
                    // Announce it, but stay here while the button is still down: holding on
                    // escalates to "and close again afterwards" (handled above). Releasing
                    // now simply opens, see the branch further down.
                    ESP_LOGW(TAG_CTL, "long press on the first press -> Opening completely");
                    indicatorBeep(BuzzerSignal::MOVEMENT_START_WARNING);
                    fullOpenAnnounced = true;
                }
                else
                {
                    // The press that turned into a long press picked the mode, it should not
                    // also widen the gap - so undo the increment it caused.
                    countPressed--;
                    gateSendCommand(GateCommandType::SET_TARGET_RUN_TIME, openTargetRunTimeMs(countPressed));
                    autoCloseIsArmed = true;
                    autoCloseBarrierFreeMs = AUTO_CLOSE_BARRIER_FREE_PARTIAL_MS;
                    ESP_LOGW(TAG_CTL, "short press + long press -> opening for %lu ms, closing once the barrier stayed free for %d ms",
                             openTargetRunTimeMs(countPressed), AUTO_CLOSE_BARRIER_FREE_PARTIAL_MS);
                    indicatorBeep(BuzzerSignal::AUTO_CLOSE_ARMED);
                    ctlState = ControlState::MOVING_TO_TARGET;
                }
            }
            //--- released after the full-open announcement -> just open, stay open ---
            else if (fullOpenAnnounced && !input.openButtonIsHeld)
            {
                fullOpenAnnounced = false;
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            //--- increment open duration ---
            else if (input.openButtonPressed)
            { // open button pressed again
                ESP_LOGI(TAG_CTL, "Additional press -> Incrementing open duration - total: %d", countPressed);
                indicatorBeep(BuzzerSignal::ADDITIONAL_PRESS);
                countPressed++;
                timestampLastAction = millis();
            }
            //--- timeout ---
            // note: suspended while the open button is still held down. The user is clearly
            // still deciding, and without this the timeout would race the long-press
            // threshold and could end the sequence just before the long press is reported.
            else if (!input.openButtonIsHeld &&
                     millis() - timestampLastAction > std::min(BUTTON_PRESS_INITIAL_OPEN_TIME_MS, BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS) - CONTROL_LOOP_HANDLE_DELAY_MS)
            { // no input for more than almost the currently desired runtime
                ESP_LOGW(TAG_CTL, "Timeout waiting for further input - applying target duration");
                gateSendCommand(GateCommandType::SET_TARGET_RUN_TIME, openTargetRunTimeMs(countPressed));

                if (countPressed > 1)
                {
                    indicatorBeep(BuzzerSignal::OPEN_FURTHER_CONFIRMED);
                }

                ctlState = ControlState::MOVING_TO_TARGET;
            }
            break;

            //------------------------------
            //------ MOVING_TO_TARGET ------
            //------------------------------
            // while gate moves to target, stop with buttons
            // or reset to idle when gates have stopped
        case ControlState::MOVING_TO_TARGET:
            // if any gate entered error state during this handle run, turn on fault led
            // note: a gate that runs into an error also sets the matching fault code on
            // the indicator itself, so nothing has to be done here - the LED keeps showing
            // it until the next start command clears it.
            // An automatic close is called off though: after a failed movement the gate
            // should not start moving again unattended.
            if (autoCloseIsArmed && anyGateHadError())
            {
                ESP_LOGE(TAG_CTL, "Gate reported an error -> cancelling the pending automatic close");
                autoCloseIsArmed = false;
            }

            //--- idle when gates stopped at target or timeout ---
            if (gatesAreIdle())
            { // both do not move and are ready to receive new commands
                if (autoCloseIsArmed)
                {
                    ESP_LOGW(TAG_CTL, "Done - gate open, closing once the barrier stayed free for %lu ms",
                             autoCloseBarrierFreeMs);
                    autoCloseBarrierIsObstructed = lightBarrierIsObstructed(input);
                    timestampAutoCloseBarrierChange = millis();
                    ctlState = ControlState::WAIT_AUTO_CLOSE;
                }
                else
                {
                    if (anyGateRefusedMovement())
                    {
                        // The gate is already at that end - or a limit switch is stuck
                        // reporting it. Either way nothing moved, so say so instead of
                        // leaving the user with a start signal and a gate that did not budge.
                        ESP_LOGW(TAG_CTL, "Nothing to do - a gate already reports that end position");
                        indicatorBeep(BuzzerSignal::ADDITIONAL_PRESS);
                    }
                    else
                    {
                        ESP_LOGW(TAG_CTL, "Done - both gates have stopped, returning to idle");
                    }
                    ctlState = ControlState::IDLE;
                }
            }
            //--- stop with any user input ---
            else if (input.anyButtonPressed) // any remote input or button is pressed while moving to target
            {
                ESP_LOGW(TAG_CTL, "User event received while moving => stopping movement");
                gateSendCommand(GateCommandType::STOP);
                indicatorBeep(BuzzerSignal::MOVEMENT_STOPPED);
                // taking over manually also calls off a pending automatic close
                autoCloseIsArmed = false;
                // note: controlState gets switched in above case when WAIT_LOCK is actually over (both gates IDLE)
            }

            break;

            //-------------------------------------
            //------ CLOSING_MOVEMENT_PAUSED ------
            //-------------------------------------
        case ControlState::CLOSING_MOVEMENT_PAUSED:
            // note: the blinking LED for this state is driven by the indicator task,
            // requested once when the state was entered (StatusIndication::WAITING_FOR_BARRIER)

            // --- cancel pending movement at any user input ---
            if (input.anyButtonPressed)
            {
                ESP_LOGW(TAG_CTL, "User event received while waiting for barrier => cancel pending movement");
                gateSendCommand(GateCommandType::CANCEL);
                indicatorBeep(BuzzerSignal::MOVEMENT_STOPPED);
                ctlState = ControlState::IDLE;
            }
            // light barrier is no longer obstructed -> decide whether to restart
            else if (!lightBarrierIsObstructed(input))
            {
                const int32_t timeRemaining =
                    (int32_t)BARRIER_DELAY_BEFORE_RESTART_MS - (int32_t)(millis() - timestampLastBarrierChange);

                // --- announce the restart ---
                handleCountdownBeeps(timeRemaining, BARRIER_DELAY_BEFORE_RESTART_MS);

                // --- continue movement ---
                if (timeRemaining <= 0)
                {
                    ESP_LOGW(TAG_CTL, "Barrier no longer obstructed for longer than %d -> resume movement", BARRIER_DELAY_BEFORE_RESTART_MS);
                    // - in case movement was stopped we need to resume
                    // - in case gate did not start (obstructed at button press) we need to start movement initially

                    gateSendCommand(GateCommandType::CONTINUE_CLOSING);

                    ctlState = ControlState::MOVING_TO_TARGET;
                }
            }
            // --- cancel movement entirely when obstructed for too long ---
            else if ((millis() - timestampLastBarrierChange) > BARRIER_WAIT_FOR_FREE_TIMEOUT_MS)
            {
                ESP_LOGE(TAG_CTL, "Barrier obstructed longer than %d ms -> wont continue automatically after clearing the barrier -> switching to IDLE", BARRIER_WAIT_FOR_FREE_TIMEOUT_MS);
                gateSendCommand(GateCommandType::CANCEL);
                indicatorBeep(BuzzerSignal::MOVEMENT_STOPPED);
                indicatorSetFault(FaultCode::BARRIER_BLOCKED_TOO_LONG);
                ctlState = ControlState::IDLE;
            }
            // --- debug log ---
            else
            {
                ESP_LOGD(TAG_CTL, "Lightbarrier is still obstructed, waiting for barrier clear or timeout in CLOSING_MOVEMENT_PAUSED state");
            }

            break;

            //-------------------------------
            //------ WAIT_AUTO_CLOSE --------
            //-------------------------------
            // Gate stands open after the "let me through" gesture and closes again on its
            // own once nobody is in the way any more.
        case ControlState::WAIT_AUTO_CLOSE:
        {
            //--- close button: do not make the user wait for the barrier-free time to pass ---
            if (input.closeButtonPressed || input.remoteClosePressed)
            {
                ESP_LOGW(TAG_CTL, "Close pressed while waiting to close automatically => closing now");
                autoCloseIsArmed = false;
                ctlState = startClosingGates(input, true);
                break;
            }

            //--- open button: call the automatic close off, leave the gate open ---
            if (input.openButtonPressed || input.remoteOpenPressed)
            {
                ESP_LOGW(TAG_CTL, "Open pressed while waiting to close automatically => cancelled, gate stays open");
                autoCloseIsArmed = false;
                indicatorBeep(BuzzerSignal::AUTO_CLOSE_CANCELLED);
                ctlState = ControlState::IDLE;
                break;
            }

            //--- track the barrier: the wait is about how long the way has been clear ---
            const bool barrierIsObstructed = lightBarrierIsObstructed(input);
            if (barrierIsObstructed != autoCloseBarrierIsObstructed)
            {
                autoCloseBarrierIsObstructed = barrierIsObstructed;
                timestampAutoCloseBarrierChange = millis();
                ESP_LOGI(TAG_CTL, "Waiting to close automatically - barrier is now %s",
                         barrierIsObstructed ? "obstructed" : "free");
            }
            const uint32_t msSinceBarrierChange = millis() - timestampAutoCloseBarrierChange;

            //--- somebody is in the gateway ---
            if (barrierIsObstructed)
            {
                // The clear-time requirement starts over once they are through, so walking
                // in and out, or taking a while with a trailer, simply postpones the close
                // instead of racing a deadline.
                // But do not hold the gate open indefinitely for someone who stays put.
                if (msSinceBarrierChange > AUTO_CLOSE_GIVE_UP_OBSTRUCTED_MS)
                {
                    ESP_LOGW(TAG_CTL, "Barrier obstructed for more than %d ms => giving up, gate stays open",
                             AUTO_CLOSE_GIVE_UP_OBSTRUCTED_MS);
                    autoCloseIsArmed = false;
                    indicatorBeep(BuzzerSignal::AUTO_CLOSE_CANCELLED);
                    ctlState = ControlState::IDLE;
                }
                break;
            }

            //--- the way is clear, count how long it stays that way ---
            const int32_t timeRemaining = (int32_t)autoCloseBarrierFreeMs - (int32_t)msSinceBarrierChange;

            //--- announce the close with the usual countdown ---
            // Same accelerating beeps as when the gate resumes after the barrier cleared,
            // so "it is about to move by itself" always sounds the same.
            // Interrupting the barrier during the countdown restarts the whole wait, which
            // is audible: the beeping simply stops.
            if (timeRemaining <= (int32_t)AUTO_CLOSE_COUNTDOWN_MS)
                handleCountdownBeeps(timeRemaining, AUTO_CLOSE_COUNTDOWN_MS);

            //--- clear for long enough -> close ---
            if (timeRemaining <= 0)
            {
                ESP_LOGW(TAG_CTL, "Barrier free for %lu ms -> closing automatically", autoCloseBarrierFreeMs);
                autoCloseIsArmed = false;
                ctlState = startClosingGates(input, false);
            }
            break;
        }
        } // end switch


        //--- keep the LED status indication in sync with the control state ---
        // Derived in one place instead of being set at every transition, so it can not get
        // out of sync. A latched fault outranks this in the indicator task.
        switch (ctlState)
        {
        case ControlState::CLOSING_MOVEMENT_PAUSED:
            indicatorSetStatus(StatusIndication::WAITING_FOR_BARRIER);
            break;
        case ControlState::WAIT_AUTO_CLOSE:
            indicatorSetStatus(StatusIndication::AUTO_CLOSE_PENDING);
            break;
        case ControlState::IDLE:
#if SHOW_BARRIER_STATE_ON_LED_IN_IDLE
            // handy when aligning the sensor
            indicatorSetStatus(barrierIsObstructedWhileIdle ? StatusIndication::BARRIER_OBSTRUCTED
                                                            : StatusIndication::IDLE);
#else
            indicatorSetStatus(StatusIndication::IDLE);
#endif
            break;
        default:
            indicatorSetStatus(StatusIndication::IDLE);
            break;
        }

        // note: the gates are stepped by their own task (gate_task.cpp), so this loop
        // now really runs at CONTROL_LOOP_HANDLE_DELAY_MS and never blocks on modbus
        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_HANDLE_DELAY_MS)); // Small delay to avoid busy loop
    } // end control loop
}
