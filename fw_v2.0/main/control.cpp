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
// How long the gate stays open before it closes again on its own.
#define AUTO_CLOSE_HOLD_OPEN_MS 20000
// The last part of that hold time is announced with the same accelerating beep countdown
// that is used when the gate resumes after the light barrier cleared, so 'the gate is
// about to move by itself' always sounds the same.
#define AUTO_CLOSE_COUNTDOWN_MS 4000

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
    WAIT_AUTO_CLOSE // gate is open and will close again on its own, see AUTO_CLOSE_HOLD_OPEN_MS
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
static bool autoCloseIsArmed = false;        // set while the current movement ends in WAIT_AUTO_CLOSE
static uint32_t timestampAutoCloseStart = 0; // when the hold-open time started counting

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
            // start opening, wait for further input
            else if (input.openButtonPressed)
            {
                ESP_LOGW(TAG_CTL, "Opening (waiting for further input)");
                indicatorBeep(BuzzerSignal::BUTTON_ACKNOWLEDGED);
                gateSendCommand(GateCommandType::OPEN_COMPLETELY);
                clearGateErrors();
                indicatorClearFault();
                countPressed = 0;
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
                    ESP_LOGW(TAG_CTL, "long press on the first press -> Opening completely");
                    indicatorBeep(BuzzerSignal::MOVEMENT_START_WARNING);
                }
                else
                {
                    // The press that turned into a long press picked the mode, it should not
                    // also widen the gap - so undo the increment it caused.
                    countPressed--;
                    gateSendCommand(GateCommandType::SET_TARGET_RUN_TIME, openTargetRunTimeMs(countPressed));
                    autoCloseIsArmed = true;
                    ESP_LOGW(TAG_CTL, "short press + long press -> opening for %lu ms, closing automatically %d ms later",
                             openTargetRunTimeMs(countPressed), AUTO_CLOSE_HOLD_OPEN_MS);
                    indicatorBeep(BuzzerSignal::AUTO_CLOSE_ARMED);
                }
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
                    ESP_LOGW(TAG_CTL, "Done - gate open, closing automatically in %d ms", AUTO_CLOSE_HOLD_OPEN_MS);
                    timestampAutoCloseStart = millis();
                    ctlState = ControlState::WAIT_AUTO_CLOSE;
                }
                else
                {
                    ESP_LOGW(TAG_CTL, "Done - both gates have stopped, returning to idle");
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

            //--- light-barrier obstructed while closing ---
            else if (anyGateIsClosing() && lightBarrierIsObstructed(input))
            {
                indicatorBeep(BuzzerSignal::BARRIER_BLOCKED);
                ESP_LOGE(TAG_CTL, "Lightbarrier got obstructed while a gate is closing => pausing movement");
                ctlState = ControlState::CLOSING_MOVEMENT_PAUSED;
                // additionally manually reset timestamp so the timeout starts when entering the PAUSED mode 
                // otherwise immediately timeouts when it was already active before pressing start button (e.g stand in gate some time and start)
                timestampLastBarrierChange = millis();
                gateSendCommand(GateCommandType::PAUSE);
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
            //--- close button: do not make the user wait out the rest of the hold time ---
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

            //--- somebody is still in the gateway -> keep it open and start over ---
            // The hold time restarts as long as the barrier is interrupted, so the gate
            // never begins to close while someone is standing in it.
            if (lightBarrierIsObstructed(input))
            {
                timestampAutoCloseStart = millis();
                break;
            }

            const int32_t timeRemaining =
                (int32_t)AUTO_CLOSE_HOLD_OPEN_MS - (int32_t)(millis() - timestampAutoCloseStart);

            //--- announce the close with the usual countdown ---
            // Same accelerating beeps as when the gate resumes after the barrier cleared,
            // so "it is about to move by itself" always sounds the same.
            if (timeRemaining <= (int32_t)AUTO_CLOSE_COUNTDOWN_MS)
                handleCountdownBeeps(timeRemaining, AUTO_CLOSE_COUNTDOWN_MS);

            //--- hold time is over -> close ---
            if (timeRemaining <= 0)
            {
                ESP_LOGW(TAG_CTL, "Hold-open time over -> closing automatically");
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
