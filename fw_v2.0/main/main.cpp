extern "C" {
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "modbus.h"
#include "iotest.h"
#include "config.h"
#include "nvs_flash.h"
}

#include "vfd.hpp"
#include "indicator.hpp"
#include "gate.hpp"
#include "gate_task.hpp"

#include "control.hpp"


static const char *TAG = "main";

// Configure all GPIO pins according to config.h
void configure_gpio_pins() {
    // TODO: most pins are initialized in the task they are used, thus can be dropped here -> move to iotest only?
    // configuration struct for gpio pins
    gpio_config_t io_conf;

    ESP_LOGW(TAG, "configuring GPIO pins...");

    // configure all inputs at once
    uint64_t input_pins = 
        (1ULL << CONFIG_SW_G1_OPEN_GPIO) |
        (1ULL << CONFIG_SW_G1_CLOSED_GPIO) |
        (1ULL << CONFIG_SW_G2_OPEN_GPIO) |
        (1ULL << CONFIG_SW_G2_CLOSED_GPIO) |
        (1ULL << CONFIG_ENCODER1_GPIO) |
        (1ULL << CONFIG_ENCODER2_GPIO) |
        (1ULL << CONFIG_BTN_OPEN_GPIO) |
        (1ULL << CONFIG_BTN_CLOSE_GPIO) |
        (1ULL << CONFIG_REMOTE_OPEN_GPIO) |
        (1ULL << CONFIG_REMOTE_CLOSE_GPIO) |
        (1ULL << CONFIG_LIGHTBARRIER_GPIO) |
        (1ULL << CONFIG_FN_BUTTON_GPIO);
    io_conf.intr_type = GPIO_INTR_DISABLE; // Disable interrupts
    io_conf.mode = GPIO_MODE_INPUT;       // Set as input
    io_conf.pin_bit_mask = input_pins;    // Pin mask for inputs
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // No internal pulldown (external pullup present)
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     // No internal pullup (optocoupler pulls low when active)
    gpio_config(&io_conf);

    // configure all outputs at once
    uint64_t output_pins = 
        (1ULL << CONFIG_SERVO_ENABLE_GPIO) |
        (1ULL << CONFIG_LIGHTBARRIER_EN_GPIO) |
        (1ULL << CONFIG_FAULT_LED_GPIO) |
        (1ULL << CONFIG_BUZZER_GPIO) |
        (1ULL << CONFIG_RELAY_VFD1_GPIO) |
        (1ULL << CONFIG_RELAY_VFD2_GPIO) |
        (1ULL << CONFIG_SERVO_PWM_GPIO) |
        (1ULL << CONFIG_RS485_DIR_GPIO) |
        (1ULL << CONFIG_RS485_TX_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;      // Set as output
    io_conf.pin_bit_mask = output_pins;  // Pin mask for outputs
    gpio_config(&io_conf);

    ESP_LOGW(TAG, "done.");
}






// Set to 1 to skip the normal control logic and only mirror the inputs to the outputs.
// Useful for checking the wiring of the control cabinet, see iotest.h.
#define RUN_GPIO_TEST 0

extern "C" void app_main(void)
{
    //=== 1. hardware init ===
    // initialize gpio pins as inputs/outputs
    configure_gpio_pins();
    // Configure UART for the RS485 / modbus bus
    modbus_init();
    // NVS, where each gate keeps the travel it has learned about itself.
    // Must be up before the Gate objects are constructed, since they read it there.
    // A partition that is full or was written by an older format is erased rather than
    // given up on: the only thing stored is a value the gates re-learn within a few
    // movements, so losing it costs nothing and refusing to boot would cost a lot.
    esp_err_t nvsStatus = nvs_flash_init();
    if (nvsStatus == ESP_ERR_NVS_NO_FREE_PAGES || nvsStatus == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs to be erased (%s) - doing that now", esp_err_to_name(nvsStatus));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvsStatus = nvs_flash_init();
    }
    if (nvsStatus != ESP_OK)
        ESP_LOGE(TAG, "NVS unavailable (%s) - the gates will start from their configured "
                      "travel constants on every boot", esp_err_to_name(nvsStatus));

    //=== 2. logging ===
    esp_log_level_set("Modbus-RTU", ESP_LOG_WARN);
    esp_log_level_set("IO-test", ESP_LOG_INFO);
    esp_log_level_set("VFD", ESP_LOG_INFO);
    esp_log_level_set("Gate1_West", ESP_LOG_INFO);
    esp_log_level_set("Gate2_East", ESP_LOG_INFO);
    esp_log_level_set("indicator", ESP_LOG_WARN);
    esp_log_level_set("control", ESP_LOG_INFO);
    esp_log_level_set("gateTask", ESP_LOG_INFO);
    esp_log_level_set("input", ESP_LOG_INFO);

    //=== 3. buzzer + fault LED ===
    const IndicatorPinConfig indicatorPins = {
        .buzzerGpio = CONFIG_BUZZER_GPIO,
        .faultLedGpio = CONFIG_FAULT_LED_GPIO,
    };
    indicatorStart(indicatorPins);
    indicatorBeep(BuzzerSignal::STARTUP);

#if RUN_GPIO_TEST
    ESP_LOGW(TAG, "RUN_GPIO_TEST is enabled - not starting the gate control!");
    while (1)
    {
        ioTest_readAllInputs();
        ioTest_setOutputsToInputStates();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
#else

    //=== 4. VFDs ===
    // note: these are function-local statics on purpose. They must be constructed AFTER
    // modbus_init() (the VFD constructor reads a register), but still outlive app_main
    // because the gate task keeps using them.
    // Higher / more distinctive addresses than 1 and 2, so an address can not be confused
    // with a modbus function code while debugging.
    static VFD vfd1(11);
    static VFD vfd2(77);

    //=== 5. gates ===
    // Parameters: name, limit switch open (gpio, active level), limit switch closed
    //             (gpio, active level), VFD supply relay gpio, VFD,
    //             full run duration 0% -> 100% in ms
    //
    // Full travel of each gate, as a DISTANCE in milliseconds at VFD_FREQUENCY_REFERENCE_HZ
    // (config_behaviour.h) - not a wall-clock time. Changing the speed the gate runs at does
    // not change these.
    //
    // These are the STARTING values and the anchor for the plausibility band. Every clean
    // limit-switch-to-limit-switch movement measures the real distance, the gate weights it
    // into its estimate (Gate::effectiveFullTravelMs) and keeps that across restarts in NVS.
    //
    // A measurement, and a stored value, is only believed within +/-25% of the number here -
    // so this is also the reset: correct it and any stale stored value is discarded on the
    // next boot.
    //
    // Measured 2026-09-01 with the speed profile active:
    //   west 8826 ms, east 9123 ms of travel
    // The previous hand-measured guesses were 10000 and 11000, i.e. 13% and 21% too long.
    // That mattered: the final approach is timed against the expected end, so it started
    // almost at the limit switch instead of 2 s before it, and the gate hit the stop at
    // full speed.
    static Gate gate1West("Gate1_West",
               CONFIG_SW_G1_OPEN_GPIO, 0,   // active low (switch NO to GND -> optocoupler ON)
               CONFIG_SW_G1_CLOSED_GPIO, 1, // active high (switch NC to GND -> optocoupler OFF)
               CONFIG_RELAY_VFD1_GPIO,
               &vfd1,
               8800);

    static Gate gate2East("Gate2_East",
               CONFIG_SW_G2_OPEN_GPIO, 0,   // active low (switch NO to GND -> optocoupler ON)
               CONFIG_SW_G2_CLOSED_GPIO, 1, // active high (switch NC to GND -> optocoupler OFF)
               CONFIG_RELAY_VFD2_GPIO,
               &vfd2,
               9100);

    //=== 6. tasks ===
    // The gate task owns the Gate objects and therefore the RS485 bus; the control task
    // only sends it commands. The input task is started by the control task.
    gateTaskStart(&gate1West, &gate2East);

    static ControlConfig controlConfig = {
        .remoteOpenGpio = CONFIG_REMOTE_OPEN_GPIO,
        .remoteCloseGpio = CONFIG_REMOTE_CLOSE_GPIO,
        .buttonOpenGpio = CONFIG_BTN_OPEN_GPIO,
        .buttonCloseGpio = CONFIG_BTN_CLOSE_GPIO,
        .lightBarrierGpio = CONFIG_LIGHTBARRIER_GPIO,
    };
    xTaskCreate(controlTask, "ControlTask", 4096 * 2, (void *)&controlConfig, 5, nullptr);

    // app_main may return here: the main task is deleted, all created tasks keep running.
    // (previously a 'while(1) vTaskDelay(portMAX_DELAY)' was needed because the gate, VFD
    //  and config objects were locals of app_main and would have been destroyed)
#endif
}
