#include "control.hpp"
#include "esp_log.h"
#include "gpio_evaluateSwitch.hpp"
#include <stdlib.h>

// TODO more delay in IDLE state / only fast when running?
#define CONTROL_LOOP_HANDLE_DELAY_MS 10

#define BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS 700 // V1: 400
#define BUTTON_PRESS_INITIAL_OPEN_TIME_MS 1900    // V1: 1100

// duration open button has to be pressed continously to trigger full open
// note this must be smaller than the input timeout which is the min of the above two values
#define FULLY_OPEN_LONG_PRESS_DURATION_MS 600

#define BARRIER_IS_IGNORED 0 // if 1 light-barrier is always considered free / not-obstructed
#define BARRIER_DELAY_BEFORE_RESTART_MS 4000 // time after which movement is resumed after barrier is free again
#define BARRIER_WAIT_FOR_FREE_TIMEOUT_MS 8000 // if barrier is obstructed longer than that continously the gate is no longer re-started after clear
#define BARRIER_BEEP_INTERVAL_MAX_MS 1000 // interval buzzer beeps when barrier just freed
#define BARRIER_BEEP_INTERVAL_MIN_MS 20  // interval buzzer beeps when movement is due


//===============================
//========== Variables ==========
//===============================
// State definitions
enum class ControlState
{
    IDLE,
    WAIT_FOR_INPUT,
    MOVING_TO_TARGET,
    MOVEMENT_PAUSED
};
const char *controlStateStr[] = {"IDLE", "WAIT_FOR_INPUT", "MOVING_TO_TARGET", "MOVEMENT_PAUSED"};
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
// helper function to determine whether light-barrier is obstructed, also updates global timestampLastBarrierChange
bool lightBarrierIsObstructed()
{
    // when ignoring the light barrier it is always considered free / not obstructed
#if (BARRIER_IS_IGNORED)
    return false
#endif
    static bool stateOld = false;
    // when obstructed:
    // - light barrier pulls 12V input to GND
    // - optocoupler turns on -> pulls 3V3 to GND
    bool stateNew = !(gpio_get_level(config->lightBarrierGpio));
    // track last change
    if (stateNew != stateOld)
    {
        ESP_LOGW(TAG_CTL, "Info: light-barrier changed state to '%s'", stateNew ? "obstructed" : "free");
        stateOld = stateNew;
        timestampLastBarrierChange = esp_log_timestamp();
    }
    return stateNew;
}



//================================
//========= Control Task =========
//================================
void controlTask(void *param)
{
    // extract parameters passed at task creation
    config = static_cast<ControlConfig *>(param);

    // Initialize buttons with GPIOs from config
    ESP_LOGI(TAG_CTL, "Initializing evaluated switch instances for buttons...");
    gpio_evaluatedSwitch buttonOpen(config->buttonOpenGpio, false, false);
    gpio_evaluatedSwitch buttonClose(config->buttonCloseGpio, false, false);
    gpio_evaluatedSwitch remoteOpen(config->remoteOpenGpio, false, false);
    gpio_evaluatedSwitch remoteClose(config->remoteCloseGpio, false, false);
    gpio_set_direction(config->lightBarrierGpio, GPIO_MODE_INPUT);
    gpio_set_direction(config->faultLedGpio, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG_CTL, "Control task started");

    // control loop
    while (true)
    {
        // Handle button inputs
        buttonOpen.handle();
        buttonClose.handle();
        remoteOpen.handle();
        remoteClose.handle();

        // State machine - control with button input according to current state
        switch (ctlState)
        {
            //--------------------
            //------- IDLE -------
            //--------------------
            // wait for initial user input
        case ControlState::IDLE:
            //--- button close ---
            // close gates completely
            if (buttonClose.risingEdge)
            {
                // TODO: prevent closing start when light barrier obstructed, or is queuing that event intended? e.g. start already while walking through
                ESP_LOGW(TAG_CTL, "Closing completely");
                config->buzzer->beep(1, 1000, 0);
                config->gateA->closeCompletely();
                config->gateB->closeCompletely();
                ctlState = ControlState::MOVING_TO_TARGET;
                // clear fault led (indicates previous error during last run)
                gpio_set_level(config->faultLedGpio, 0);
            }
            //--- button open ---
            // start opening, wait for further input
            else if (buttonOpen.risingEdge)
            {
                ESP_LOGW(TAG_CTL, "Opening (waiting for further input)");
                config->buzzer->beep(1, 100, 0);
                config->gateA->openCompletely();
                config->gateB->openCompletely();
                countPressed = 0;
                timestampLastAction = esp_log_timestamp();
                ctlState = ControlState::WAIT_FOR_INPUT;
            }
            //--- remote close ---
            // close gates completely
            else if (remoteClose.risingEdge)
            {
                ESP_LOGW(TAG_CTL, "REMOTE: Closing completely");
                config->buzzer->beep(2, 500, 100);
                config->gateA->closeCompletely();
                config->gateB->closeCompletely();
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            //--- remote open ---
            // open gates completely
            else if (remoteOpen.risingEdge)
            {
                ESP_LOGW(TAG_CTL, "REMOTE: Opening completely");
                config->buzzer->beep(1, 1000, 0);
                config->gateA->openCompletely();
                config->gateB->openCompletely();
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            break;

            //--------------------------
            //----- WAIT_FOR_INPUT -----
            //--------------------------
            // wait for and process additional input
            //(decide what the user wants exactly while the gate already moves)
        case ControlState::WAIT_FOR_INPUT:
            //--- stop ---
            if (buttonClose.state)
            { // close button is pressed while waiting for input
                ESP_LOGW(TAG_CTL, "Close button while waiting for input -> stopping gates");
                config->gateA->stop();
                config->gateB->stop();
                config->buzzer->beep(1, 400, 0);
                ctlState = ControlState::IDLE;
            }
            //--- open completely ---
            else if (buttonOpen.state && buttonOpen.msPressed > FULLY_OPEN_LONG_PRESS_DURATION_MS)
            { // open button is pressed longer than 800ms
                ESP_LOGW(TAG_CTL, "long press -> Opening completely");
                config->buzzer->beep(1, 1000, 0);
                ctlState = ControlState::MOVING_TO_TARGET;
            }
            //--- increment open duration ---
            else if (buttonOpen.risingEdge)
            { // open button high again
                ESP_LOGI(TAG_CTL, "Additional press -> Incrementing open duration - total: %d", countPressed);
                config->buzzer->beep(1, 60, 0);
                countPressed++;
                timestampLastAction = esp_log_timestamp();
            }
            //--- timeout ---
            else if (esp_log_timestamp() - timestampLastAction > std::min(BUTTON_PRESS_INITIAL_OPEN_TIME_MS, BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS) - CONTROL_LOOP_HANDLE_DELAY_MS)
            { // no input for more than almost the currently desired runtime
                ESP_LOGW(TAG_CTL, "Timeout waiting for further input - applying target duration");
                config->gateA->updateTargetRunTime(BUTTON_PRESS_INITIAL_OPEN_TIME_MS + (BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS * countPressed));
                config->gateB->updateTargetRunTime(BUTTON_PRESS_INITIAL_OPEN_TIME_MS + (BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS * countPressed));

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
            if ((config->gateA->getState() == ERROR_STATE) || (config->gateB->getState() == ERROR_STATE))
            {
                // note: gate is in error state for one handle() cycle only - currently it switches to IDLE in the next cycle
                ESP_LOGE(TAG_CTL, "At least one gate is currently in ERROR_STATE, turning on fault led...");
                gpio_set_level(config->faultLedGpio, 1);
                // note: led is reset/turned off at next start event
            }

            //--- idle when gates stopped at target or timeout ---
            if (config->gateA->getIsIdling() && config->gateB->getIsIdling())
            { // both do not move and are ready to receive new commands
                ESP_LOGW(TAG_CTL, "Done - both gates have stopped, returning to idle");
                ctlState = ControlState::IDLE;
            }
            //--- stop with any user input ---
            else if (buttonClose.risingEdge || buttonOpen.risingEdge || remoteOpen.risingEdge || remoteClose.risingEdge) // any remote input or button is pressed while moving to target
            {
                ESP_LOGW(TAG_CTL, "User event received while moving => stopping movement");
                config->gateA->stop();
                config->gateB->stop();
                config->buzzer->beep(1, 400, 0);
                // note: controlState gets switched in above case when WAIT_LOCK is actually over (both gates IDLE)
            }

            //--- light-barrier obstructed while closing ---
            else if ((config->gateA->getIsClosing() || config->gateB->getIsClosing()) && lightBarrierIsObstructed())
            {
                config->buzzer->beep(4, 100, 50);
                ESP_LOGE(TAG_CTL, "Lightbarrier got obstructed while a gate is closing => pausing movement");
                gpio_set_level(config->faultLedGpio, 1);
                ctlState = ControlState::MOVEMENT_PAUSED;
                // additionally manually reset timestamp so the timeout starts when entering the PAUSED mode 
                // otherwise immediately timeouts when it was already active before pressing start button (e.g stand in gate some time and start)
                timestampLastBarrierChange = esp_log_timestamp();
                config->gateA->pause();
                config->gateB->pause();
            }
            break;

            //-----------------------------
            //------ MOVEMENT_PAUSED ------
            //-----------------------------
        case ControlState::MOVEMENT_PAUSED:
            // always blink fault led slowly while in MOVEMENT_PAUSED state
            #define LED_PAUSED_STATE_BLINK_INTERVAL 200
            uint32_t now = esp_log_timestamp();
            // toggle LED if interval passed
            if (now - timestampLastLedBlink >= LED_PAUSED_STATE_BLINK_INTERVAL)
            {
                ledBlinkState = !ledBlinkState;
                gpio_set_level(config->faultLedGpio, ledBlinkState);
                timestampLastLedBlink = now;
            }

            // --- cancel pending movement at any user input ---
            if (buttonClose.risingEdge || buttonOpen.risingEdge || remoteOpen.risingEdge || remoteClose.risingEdge)
            {
                ESP_LOGW(TAG_CTL, "User event received while waiting for barrier => cancel pending movement");
                config->gateA->cancel();
                config->gateB->cancel();
                config->buzzer->beep(1, 1000, 0);
                gpio_set_level(config->faultLedGpio, 0); // turn off fault led
                ctlState = ControlState::IDLE;
            }
            // light barrier is no longer obstructed -> decide whether to restart
            else if (!lightBarrierIsObstructed())
            {
                int timeRemaining = BARRIER_DELAY_BEFORE_RESTART_MS - (esp_log_timestamp() - timestampLastBarrierChange);

                // --- Buzzer countdown ---
                // calculate beep interval based on remaning time (beep faster when closer to start)
                uint32_t beepInterval = BARRIER_BEEP_INTERVAL_MIN_MS +
                                        (timeRemaining * (BARRIER_BEEP_INTERVAL_MAX_MS - BARRIER_BEEP_INTERVAL_MIN_MS)) /
                                            BARRIER_DELAY_BEFORE_RESTART_MS;
                // trigger next beep if due
                if (esp_log_timestamp() - timestampLastCountdownBeep >= beepInterval && timeRemaining > BARRIER_BEEP_INTERVAL_MIN_MS)
                {
                    uint32_t buzzerOnDuration = std::min(beepInterval, (uint32_t)70);
                    ESP_LOGD(TAG_CTL, "remaining wait time: %d, current buzzer on-duration: %ld, triggering next beep...", timeRemaining, buzzerOnDuration);
                    config->buzzer->beep(1, buzzerOnDuration, 0); // trigger 1 beep no delay
                    timestampLastCountdownBeep = esp_log_timestamp();
                }

                // --- continue movement ---
                if (timeRemaining <= 0)
                {
                    ESP_LOGW(TAG_CTL, "Barrier no longer obstructed for longer than %d -> resume movement", BARRIER_DELAY_BEFORE_RESTART_MS);
                    config->gateA->resume();
                    config->gateB->resume();
                    ctlState = ControlState::MOVING_TO_TARGET;
                    gpio_set_level(config->faultLedGpio, 0); // turn off fault led
                }
            }
            // --- cancel movement entirely when obstructed for too long ---
            else if ((esp_log_timestamp() - timestampLastBarrierChange) > BARRIER_WAIT_FOR_FREE_TIMEOUT_MS)
            {
                ESP_LOGE(TAG_CTL, "Barrier obstructed longer than %d ms -> wont continue automatically after clearing the barrier -> switching to IDLE", BARRIER_WAIT_FOR_FREE_TIMEOUT_MS);
                config->gateA->cancel();
                config->gateB->cancel();
                config->buzzer->beep(1, 1000, 0);
                ctlState = ControlState::IDLE;
                gpio_set_level(config->faultLedGpio, 1); // turn on fault led
            }
            // --- debug log ---
            else
            {
                ESP_LOGD(TAG_CTL, "Lightbarrier is still obstructed, waiting for barrier clear or timeout in MOVEMENT_PAUSED state");
            }

            break;
        } // end switch


        // handle gates (update pos, handle limits, turn on/off...)
        config->gateB->handle();
        config->gateA->handle();

        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_HANDLE_DELAY_MS)); // Small delay to avoid busy loop
    } // end control loop
}
