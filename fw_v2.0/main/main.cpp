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
}

#include "vfd.hpp"
#include "buzzer.hpp"
#include "gate.hpp"
#include "gate_task.hpp"

#include "control.hpp"


static const char *TAG = "main";
//create buzzer object 
// configured with 0ms gap between beep events
buzzer_t buzzer(CONFIG_BUZZER_GPIO, 0);

//======================================
//============ buzzer task =============
//======================================
//TODO: move the task creation to buzzer class (buzzer.cpp)
//e.g. only have function buzzer.createTask() in app_main
void task_buzzer( void * pvParameters ){
    ESP_LOGI("task_buzzer", "Start of buzzer task...");
        //run function that waits for a beep events to arrive in the queue
        //and processes them
        buzzer.processQueue();
}

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
        (1ULL << CONFIG_LED_GPIO) |
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

    //=== 2. logging ===
    esp_log_level_set("Modbus-RTU", ESP_LOG_WARN);
    esp_log_level_set("IO-test", ESP_LOG_INFO);
    esp_log_level_set("VFD", ESP_LOG_INFO);
    esp_log_level_set("Gate1_West", ESP_LOG_INFO);
    esp_log_level_set("Gate2_East", ESP_LOG_INFO);
    esp_log_level_set("buzzer", ESP_LOG_ERROR);
    esp_log_level_set("control", ESP_LOG_INFO);
    esp_log_level_set("gateTask", ESP_LOG_INFO);
    esp_log_level_set("input", ESP_LOG_INFO);

    //=== 3. buzzer ===
    xTaskCreate(&task_buzzer, "task_buzzer", 2048, NULL, 5, NULL);
    buzzer.beep(3, 50, 100); // beep at startup

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
    //             (gpio, active level), VFD supply relay gpio, VFD, buzzer,
    //             full run duration 0% -> 100% in ms
    //
    // Measured full run durations (at 50 Hz, VFD start setting 7):
    //   west gate: ~9 s, east gate: ~10 s   (1 s margin added below)
    static Gate gate1West("Gate1_West",
               CONFIG_SW_G1_OPEN_GPIO, 0,   // active low (switch NO to GND -> optocoupler ON)
               CONFIG_SW_G1_CLOSED_GPIO, 1, // active high (switch NC to GND -> optocoupler OFF)
               CONFIG_RELAY_VFD1_GPIO,
               &vfd1,
               &buzzer,
               9000 + 1000);

    static Gate gate2East("Gate2_East",
               CONFIG_SW_G2_OPEN_GPIO, 0,   // active low (switch NO to GND -> optocoupler ON)
               CONFIG_SW_G2_CLOSED_GPIO, 1, // active high (switch NC to GND -> optocoupler OFF)
               CONFIG_RELAY_VFD2_GPIO,
               &vfd2,
               &buzzer,
               10000 + 1000);

    //=== 6. tasks ===
    // The gate task owns the Gate objects and therefore the RS485 bus; the control task
    // only sends it commands. The input task is started by the control task.
    gateTaskStart(&gate1West, &gate2East);

    static ControlConfig controlConfig = {
        .remoteOpenGpio = CONFIG_REMOTE_OPEN_GPIO,
        .remoteCloseGpio = CONFIG_REMOTE_CLOSE_GPIO,
        .buttonOpenGpio = CONFIG_BTN_OPEN_GPIO,
        .buttonCloseGpio = CONFIG_BTN_CLOSE_GPIO,
        .faultLedGpio = CONFIG_LED_GPIO,
        .lightBarrierGpio = CONFIG_LIGHTBARRIER_GPIO,
        .buzzer = &buzzer
    };
    xTaskCreate(controlTask, "ControlTask", 4096 * 2, (void *)&controlConfig, 5, nullptr);

    // app_main may return here: the main task is deleted, all created tasks keep running.
    // (previously a 'while(1) vTaskDelay(portMAX_DELAY)' was needed because the gate, VFD
    //  and config objects were locals of app_main and would have been destroyed)
#endif
}
