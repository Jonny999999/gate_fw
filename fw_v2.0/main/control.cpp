#include "control.hpp"
#include "esp_log.h"
#include "gpio_evaluateSwitch.hpp"

#define CONTROL_LOOP_HANDLE_DELAY_MS 25

#define BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS 1500 // V1: 400
#define BUTTON_PRESS_INITIAL_OPEN_TIME_MS 2000    // V1: 1100



//===============================
//========== Variables ==========
//===============================
// State definitions
enum class ControlState
{
    IDLE,
    WAIT_FOR_INPUT,
    MOVING_TO_TARGET
};
const char *controlStateStr[] = {"IDLE", "WAIT_FOR_INPUT", "MOVING_TO_TARGET"};
// TODO: add and handle state e.g. LOCKED

// Control state variables
static ControlState ctlState = ControlState::IDLE;
static uint32_t timestampLastAction;
static uint8_t countPressed = 0;
static const char *TAG_CTL = "control";





//================================
//========= Control Task =========
//================================
void controlTask(void *param)
{
    auto *config = static_cast<ControlConfig *>(param);

    // Initialize buttons with GPIOs from config
    ESP_LOGI(TAG_CTL, "Initializing evaluated switch instances for buttons...");
    gpio_evaluatedSwitch buttonOpen(config->buttonOpenGpio, false, false);
    gpio_evaluatedSwitch buttonClose(config->buttonCloseGpio, false, false);
    gpio_evaluatedSwitch remoteOpen(config->remoteOpenGpio, false, false);
    gpio_evaluatedSwitch remoteClose(config->remoteCloseGpio, false, false);
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
                ESP_LOGW(TAG_CTL, "Closing completely");
                config->buzzer->beep(1, 1000, 0);
                config->gateA->closeCompletely();
                config->gateB->closeCompletely();
                ctlState = ControlState::MOVING_TO_TARGET;
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
            else if (buttonOpen.state && buttonOpen.msPressed > 800)
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
            break;
        } // end switch


        // handle gates (update pos, handle limits, turn on/off...)
        config->gateB->handle();
        config->gateA->handle();

        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_HANDLE_DELAY_MS)); // Small delay to avoid busy loop
    } // end control loop
}
