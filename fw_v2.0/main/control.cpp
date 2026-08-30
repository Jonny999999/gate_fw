#include "control.hpp"
#include "esp_log.h"
#include "gate_task.hpp"
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

#define BARRIER_IS_IGNORED 0 // if 1 light-barrier is always considered free / not-obstructed
#define BARRIER_DELAY_BEFORE_RESTART_MS 4000 // time after which movement is resumed after barrier is free again
#define BARRIER_WAIT_FOR_FREE_TIMEOUT_MS 8000 // if barrier is obstructed longer than that continously the gate is no longer re-started after clear
#define BARRIER_BEEP_INTERVAL_MAX_MS 1000 // interval buzzer beeps when barrier just freed
#define BARRIER_BEEP_INTERVAL_MIN_MS 20  // interval buzzer beeps when movement is due

#define DEBUG_BARRIER_PASSTHROUGH_LED_IN_IDLE 1
#define DEBUG_BARRIER_BEEP_ON_CHANGE 0


//===============================
//========== Variables ==========
//===============================
// State definitions
enum class ControlState
{
    IDLE,
    WAIT_FOR_INPUT,
    MOVING_TO_TARGET,
    CLOSING_MOVEMENT_PAUSED
};
const char *controlStateStr[] = {"IDLE", "WAIT_FOR_INPUT", "MOVING_TO_TARGET", "CLOSING_MOVEMENT_PAUSED"};
// TODO: add and handle state e.g. LOCKED

// Control state variables
static ControlState ctlState = ControlState::IDLE;

// user input
static uint32_t timestampLastAction;
static uint8_t countPressed = 0;

// light barrier
static uint32_t timestampLastBarrierChange = 0;
static uint32_t timestampLastCountdownBeep;

// fault-led handling
uint32_t timestampLastLedBlink = 0;
bool ledBlinkState = false;

// gate + buzzer objects and GPIO assignment passed from main
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
            config->buzzer->beep(1, 30, 0);
        #endif
        ESP_LOGW(TAG_CTL, "Info: light-barrier changed state to '%s'", stateNew ? "obstructed" : "free");
        stateOld = stateNew;
        timestampLastBarrierChange = millis();
    }
    return stateNew;
#endif
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

    gpio_set_direction(config->faultLedGpio, GPIO_MODE_OUTPUT);
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
                config->buzzer->beep(1, 1000, 50);
                if (lightBarrierIsObstructed(input)){
                    // barrier is obstructed -> wait for clear in CLOSING_MOVEMENT_PAUSED state before starting
                    config->buzzer->beep(4, 50, 50);
                    ESP_LOGE(TAG_CTL, "Close command received but barrier currently obstructed saving close request -> switching to PAUSED");
                    ctlState = ControlState::CLOSING_MOVEMENT_PAUSED;
                } else {
                    // barrier not obstructed -> close immediately
                    gateSendCommand(GateCommandType::CLOSE_COMPLETELY);
                    ctlState = ControlState::MOVING_TO_TARGET;
                    // clear fault led (indicates previous error during last run)
                    clearGateErrors();
                    gpio_set_level(config->faultLedGpio, 0);
                }
            }
            //--- button open ---
            // start opening, wait for further input
            else if (input.openButtonPressed)
            {
                ESP_LOGW(TAG_CTL, "Opening (waiting for further input)");
                config->buzzer->beep(1, 100, 0);
                gateSendCommand(GateCommandType::OPEN_COMPLETELY);
                clearGateErrors();
                gpio_set_level(config->faultLedGpio, 0);
                countPressed = 0;
                timestampLastAction = millis();
                ctlState = ControlState::WAIT_FOR_INPUT;
            }
            //--- remote open ---
            // open gates completely
            else if (input.remoteOpenPressed)
            {
                ESP_LOGW(TAG_CTL, "REMOTE: Opening completely");
                config->buzzer->beep(1, 1000, 0);
                gateSendCommand(GateCommandType::OPEN_COMPLETELY);
                clearGateErrors();
                gpio_set_level(config->faultLedGpio, 0);
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            //--- debug barrier ---
            // pass through light-barrier state to fault led for debugging
            #if DEBUG_BARRIER_PASSTHROUGH_LED_IN_IDLE
                gpio_set_level(config->faultLedGpio, lightBarrierIsObstructed(input));
            #endif

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
                config->buzzer->beep(1, 400, 0);
                ctlState = ControlState::IDLE;
            }
            //--- open completely ---
            else if (input.openButtonLongPress)
            { // open button held past the long-press threshold (detected by the input task)
                ESP_LOGW(TAG_CTL, "long press -> Opening completely");
                config->buzzer->beep(1, 1000, 0);
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            //--- increment open duration ---
            else if (input.openButtonPressed)
            { // open button pressed again
                ESP_LOGI(TAG_CTL, "Additional press -> Incrementing open duration - total: %d", countPressed);
                config->buzzer->beep(1, 60, 0);
                countPressed++;
                timestampLastAction = millis();
            }
            //--- timeout ---
            else if (millis() - timestampLastAction > std::min(BUTTON_PRESS_INITIAL_OPEN_TIME_MS, BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS) - CONTROL_LOOP_HANDLE_DELAY_MS)
            { // no input for more than almost the currently desired runtime
                ESP_LOGW(TAG_CTL, "Timeout waiting for further input - applying target duration");
                gateSendCommand(GateCommandType::SET_TARGET_RUN_TIME,
                                BUTTON_PRESS_INITIAL_OPEN_TIME_MS + (BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS * countPressed));

                if (countPressed > 1)
                {
                    config->buzzer->beep(2, 40, 20);
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
            if (anyGateHadError())
            {
                // note: the flag is latched by the Gate, because ERROR_STATE itself only
                // lasts a single handle() cycle and would otherwise be missed from here
                ESP_LOGE(TAG_CTL, "At least one gate reported an error, turning on fault led...");
                gpio_set_level(config->faultLedGpio, 1);
                // note: led and latch are reset at the next start event
            }

            //--- idle when gates stopped at target or timeout ---
            if (gatesAreIdle())
            { // both do not move and are ready to receive new commands
                ESP_LOGW(TAG_CTL, "Done - both gates have stopped, returning to idle");
                ctlState = ControlState::IDLE;
            }
            //--- stop with any user input ---
            else if (input.anyButtonPressed) // any remote input or button is pressed while moving to target
            {
                ESP_LOGW(TAG_CTL, "User event received while moving => stopping movement");
                gateSendCommand(GateCommandType::STOP);
                config->buzzer->beep(1, 400, 0);
                // note: controlState gets switched in above case when WAIT_LOCK is actually over (both gates IDLE)
            }

            //--- light-barrier obstructed while closing ---
            else if (anyGateIsClosing() && lightBarrierIsObstructed(input))
            {
                config->buzzer->beep(4, 100, 50);
                ESP_LOGE(TAG_CTL, "Lightbarrier got obstructed while a gate is closing => pausing movement");
                gpio_set_level(config->faultLedGpio, 1);
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
            // always blink fault led slowly while in CLOSING_MOVEMENT_PAUSED state
            #define LED_PAUSED_STATE_BLINK_INTERVAL 200
            uint32_t now = millis();
            // toggle LED if interval passed
            if (now - timestampLastLedBlink >= LED_PAUSED_STATE_BLINK_INTERVAL)
            {
                ledBlinkState = !ledBlinkState;
                gpio_set_level(config->faultLedGpio, ledBlinkState);
                timestampLastLedBlink = now;
            }

            // --- cancel pending movement at any user input ---
            if (input.anyButtonPressed)
            {
                ESP_LOGW(TAG_CTL, "User event received while waiting for barrier => cancel pending movement");
                gateSendCommand(GateCommandType::CANCEL);
                config->buzzer->beep(1, 1000, 0);
                gpio_set_level(config->faultLedGpio, 0); // turn off fault led
                ctlState = ControlState::IDLE;
            }
            // light barrier is no longer obstructed -> decide whether to restart
            else if (!lightBarrierIsObstructed(input))
            {
                int timeRemaining = BARRIER_DELAY_BEFORE_RESTART_MS - (millis() - timestampLastBarrierChange);

                // --- Buzzer countdown ---
                // calculate beep interval based on remaning time (beep faster when closer to start)
                uint32_t beepInterval = BARRIER_BEEP_INTERVAL_MIN_MS +
                                        (timeRemaining * (BARRIER_BEEP_INTERVAL_MAX_MS - BARRIER_BEEP_INTERVAL_MIN_MS)) /
                                            BARRIER_DELAY_BEFORE_RESTART_MS;
                // trigger next beep if due
                if (millis() - timestampLastCountdownBeep >= beepInterval && timeRemaining > BARRIER_BEEP_INTERVAL_MIN_MS)
                {
                    uint32_t buzzerOnDuration = std::min(beepInterval, (uint32_t)70);
                    ESP_LOGD(TAG_CTL, "remaining wait time: %d, current buzzer on-duration: %ld, triggering next beep...", timeRemaining, buzzerOnDuration);
                    config->buzzer->beep(1, buzzerOnDuration, 0); // trigger 1 beep no delay
                    timestampLastCountdownBeep = millis();
                }

                // --- continue movement ---
                if (timeRemaining <= 0)
                {
                    ESP_LOGW(TAG_CTL, "Barrier no longer obstructed for longer than %d -> resume movement", BARRIER_DELAY_BEFORE_RESTART_MS);
                    // - in case movement was stopped we need to resume
                    // - in case gate did not start (obstructed at button press) we need to start movement initially

                    gateSendCommand(GateCommandType::CONTINUE_CLOSING);

                    ctlState = ControlState::MOVING_TO_TARGET;
                    gpio_set_level(config->faultLedGpio, 0); // turn off fault led
                }
            }
            // --- cancel movement entirely when obstructed for too long ---
            else if ((millis() - timestampLastBarrierChange) > BARRIER_WAIT_FOR_FREE_TIMEOUT_MS)
            {
                ESP_LOGE(TAG_CTL, "Barrier obstructed longer than %d ms -> wont continue automatically after clearing the barrier -> switching to IDLE", BARRIER_WAIT_FOR_FREE_TIMEOUT_MS);
                gateSendCommand(GateCommandType::CANCEL);
                config->buzzer->beep(1, 1000, 0);
                ctlState = ControlState::IDLE;
                gpio_set_level(config->faultLedGpio, 1); // turn on fault led
            }
            // --- debug log ---
            else
            {
                ESP_LOGD(TAG_CTL, "Lightbarrier is still obstructed, waiting for barrier clear or timeout in CLOSING_MOVEMENT_PAUSED state");
            }

            break;
        } // end switch


        // note: the gates are stepped by their own task (gate_task.cpp), so this loop
        // now really runs at CONTROL_LOOP_HANDLE_DELAY_MS and never blocks on modbus
        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_HANDLE_DELAY_MS)); // Small delay to avoid busy loop
    } // end control loop
}
