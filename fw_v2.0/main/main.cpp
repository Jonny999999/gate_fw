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


static const char *TAG = "main";


// Configure all GPIO pins according to config.h
void configure_gpio_pins() {
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






//#define RUN_GPIO_TEST
#define RUN_MODBUS_TEST

extern "C" void app_main(void)
{

    // initialize gpio pins as inputs/outputs
    configure_gpio_pins();

    // Configure UART
    modbus_init();

    // set loglevel
    esp_log_level_set("Modbus-RTU", ESP_LOG_INFO);
    esp_log_level_set("IO-test", ESP_LOG_INFO);
    esp_log_level_set("VFD", ESP_LOG_INFO);


#ifdef RUN_MODBUS_TEST

// Create VFD instances
    VFD vfd1(0x01); // VFD 1 with address 0x01
    VFD vfd2(0x02); // VFD 2 with address 0x02

    while (1)
    {
        uint16_t temperature;
        float voltage, current;

        // Control VFD 1
        vfd1.setFrequency(20); // Set frequency to 200 Hz
        vfd1.start();
        vTaskDelay(pdMS_TO_TICKS(2000));
        vfd1.getVoltage(&voltage);
        vfd1.getCurrent(&current);
        vfd1.getTemperature(&temperature);
        ESP_LOGI("Main", "VFD1 -> Voltage: %.1f V, Current: %.1f A, Temperature: %d C", voltage, current, temperature);
        vfd1.stop();
        vTaskDelay(pdMS_TO_TICKS(500));


        //// Control VFD 2
        //vfd2.setFrequency(10); // Set frequency to 150 Hz
        //vfd2.start();
        //vTaskDelay(pdMS_TO_TICKS(1500));
        //vfd2.getVoltage(&voltage);
        //vfd2.getCurrent(&current);
        //vfd2.getTemperature(&temperature);
        //ESP_LOGI("Main", "VFD2 -> Voltage: %d, Current: %d, Temperature: %d", voltage, current, temperature);
        //vfd2.stop();

        vTaskDelay(pdMS_TO_TICKS(5000));
    }

#endif


#ifdef RUN_GPIO_TEST
    // GPIO Test
    ESP_LOGW(TAG, "starting GPIO test");
    while (1)
    {
        ioTest_readAllInputs();
        ioTest_setOutputsToInputStates();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
#endif



}
