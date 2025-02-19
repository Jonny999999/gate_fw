#include "control.hpp"
#include "esp_log.h"

// State definitions
enum class ControlState { IDLE, WAIT_FOR_INPUT, MOVING_TO_TARGET };
const char* controlStateStr[] = {"IDLE", "WAIT_FOR_INPUT", "MOVING_TO_TARGET"};

// Static instances for input evaluation
static gpio_evaluatedSwitch buttonOpen(GPIO_NUM_MAX, false, false);
static gpio_evaluatedSwitch buttonClose(GPIO_NUM_MAX, false, false);
static gpio_evaluatedSwitch remoteOpen(GPIO_NUM_MAX, false, false);
static gpio_evaluatedSwitch remoteClose(GPIO_NUM_MAX, false, false);

// Control state variables
static ControlState ctlState = ControlState::IDLE;
static uint32_t timestampLastAction;
static uint8_t countPressed = 0;
static const char* TAG_CTL = "control";

void controlTask(void* param) {
    auto* config = static_cast<ControlConfig*>(param);

    // Initialize buttons with GPIOs from config
    buttonOpen = gpio_evaluatedSwitch(config->buttonOpenGpio, false, false);
    buttonClose = gpio_evaluatedSwitch(config->buttonCloseGpio, false, false);
    remoteOpen = gpio_evaluatedSwitch(config->remoteOpenGpio, false, false);
    remoteClose = gpio_evaluatedSwitch(config->remoteCloseGpio, false, false);

    ESP_LOGI(TAG_CTL, "Control task started");

    while (true) {
        // Handle button inputs
        buttonOpen.handle();
        buttonClose.handle();
        remoteOpen.handle();
        remoteClose.handle();

        switch (ctlState) {
            case ControlState::IDLE:
                if (buttonClose.risingEdge) {
                    ESP_LOGW(TAG_CTL, "Closing completely");
                    config->buzzer->beep(1, 1000, 0);
                    config->gateA->close(20000);
                    config->gateB->close(20000);
                    ctlState = ControlState::MOVING_TO_TARGET;

                } else if (buttonOpen.risingEdge) {
                    ESP_LOGW(TAG_CTL, "Opening (waiting for further input)");
                    config->buzzer->beep(1, 100, 0);
                    config->gateA->open(20000);
                    config->gateB->open(20000);
                    countPressed = 0;
                    timestampLastAction = esp_log_timestamp();
                    ctlState = ControlState::WAIT_FOR_INPUT;

                } else if (remoteClose.risingEdge) {
                    ESP_LOGW(TAG_CTL, "REMOTE: Closing completely");
                    config->buzzer->beep(2, 500, 100);
                    config->gateA->close(20000);
                    config->gateB->close(20000);
                    ctlState = ControlState::MOVING_TO_TARGET;

                } else if (remoteOpen.risingEdge) {
                    ESP_LOGW(TAG_CTL, "REMOTE: Opening completely");
                    config->buzzer->beep(1, 1000, 0);
                    config->gateA->open(20000);
                    config->gateB->open(20000);
                    ctlState = ControlState::MOVING_TO_TARGET;
                }
                break;

            case ControlState::WAIT_FOR_INPUT:
                if (buttonClose.state) {
                    config->gateA->stop();
                    config->gateB->stop();
                    config->buzzer->beep(1, 400, 0);
                    ctlState = ControlState::IDLE;

                } else if (buttonOpen.state && buttonOpen.msPressed > 800) {
                    ESP_LOGW(TAG_CTL, "Opening completely");
                    config->buzzer->beep(1, 1000, 0);
                    ctlState = ControlState::MOVING_TO_TARGET;

                } else if (buttonOpen.risingEdge) {
                    ESP_LOGI(TAG_CTL, "Incrementing open duration - total: %d", countPressed);
                    config->buzzer->beep(1, 60, 0);
                    countPressed++;
                    timestampLastAction = esp_log_timestamp();

                } else if (esp_log_timestamp() - timestampLastAction > 900) {
                    ESP_LOGW(TAG_CTL, "Timeout - applying target duration");
                    config->gateA->setDuration(1100 + (400 * countPressed));
                    config->gateB->setDuration(1100 + (400 * countPressed));

                    if (countPressed > 1) {
                        config->buzzer->beep(2, 40, 20);
                    }

                    ctlState = ControlState::MOVING_TO_TARGET;
                }
                break;

            case ControlState::MOVING_TO_TARGET:
                if (config->gateA->state == gateState::IDLE && config->gateB->state == gateState::IDLE) {
                    ESP_LOGW(TAG_CTL, "Done - both gates have stopped, returning to idle");
                    ctlState = ControlState::IDLE;

                } else if (buttonClose.risingEdge || buttonOpen.risingEdge || remoteOpen.risingEdge || remoteClose.risingEdge) {
                    config->gateA->stop();
                    config->gateB->stop();
                    config->buzzer->beep(1, 400, 0);
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Small delay to avoid busy loop
    }
}
