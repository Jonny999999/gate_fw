#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "config.h"
#include "modbus.h"


static const char *TAG = "main";


#include "driver/gpio.h"
#include "config.h"

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



void ioTest_readAllInputs() {
    ESP_LOGI("TEST", "Reading input pins...");
    ESP_LOGI("TEST", "SW_G1_OPEN: %d", gpio_get_level(CONFIG_SW_G1_OPEN_GPIO));
    ESP_LOGI("TEST", "SW_G1_CLOSED: %d", gpio_get_level(CONFIG_SW_G1_CLOSED_GPIO));
    ESP_LOGI("TEST", "SW_G2_OPEN: %d", gpio_get_level(CONFIG_SW_G2_OPEN_GPIO));
    ESP_LOGI("TEST", "SW_G2_CLOSED: %d", gpio_get_level(CONFIG_SW_G2_CLOSED_GPIO));
    ESP_LOGI("TEST", "ENCODER1: %d", gpio_get_level(CONFIG_ENCODER1_GPIO));
    ESP_LOGI("TEST", "ENCODER2: %d", gpio_get_level(CONFIG_ENCODER2_GPIO));
    ESP_LOGI("TEST", "BTN_OPEN: %d", gpio_get_level(CONFIG_BTN_OPEN_GPIO));
    ESP_LOGI("TEST", "BTN_CLOSE: %d", gpio_get_level(CONFIG_BTN_CLOSE_GPIO));
    ESP_LOGI("TEST", "REMOTE_OPEN: %d", gpio_get_level(CONFIG_REMOTE_OPEN_GPIO));
    ESP_LOGI("TEST", "REMOTE_CLOSE: %d", gpio_get_level(CONFIG_REMOTE_CLOSE_GPIO));
    ESP_LOGI("TEST", "LIGHTBARRIER: %d", gpio_get_level(CONFIG_LIGHTBARRIER_GPIO));
    ESP_LOGI("TEST", "FN_BUTTON: %d", gpio_get_level(CONFIG_FN_BUTTON_GPIO));
    ESP_LOGI("TEST", "======================");
}


void ioTest_setAllOutputsHigh() {
    ESP_LOGI("TEST", "Turning all outputs ON...");
    gpio_set_level(CONFIG_SERVO_ENABLE_GPIO, 1);
    gpio_set_level(CONFIG_LIGHTBARRIER_EN_GPIO, 1);
    gpio_set_level(CONFIG_LED_GPIO, 1);
    gpio_set_level(CONFIG_BUZZER_GPIO, 1);
    gpio_set_level(CONFIG_RELAY_VFD1_GPIO, 1);
    gpio_set_level(CONFIG_RELAY_VFD2_GPIO, 1);
    gpio_set_level(CONFIG_SERVO_PWM_GPIO, 1);
    gpio_set_level(CONFIG_RS485_DIR_GPIO, 1);
}

    
void ioTest_setAllOutputsLow() {
    ESP_LOGI("TEST", "Turning all outputs OFF...");
    gpio_set_level(CONFIG_SERVO_ENABLE_GPIO, 0);
    gpio_set_level(CONFIG_LIGHTBARRIER_EN_GPIO, 0);
    gpio_set_level(CONFIG_LED_GPIO, 0);
    gpio_set_level(CONFIG_BUZZER_GPIO, 0);
    gpio_set_level(CONFIG_RELAY_VFD1_GPIO, 0);
    gpio_set_level(CONFIG_RELAY_VFD2_GPIO, 0);
    gpio_set_level(CONFIG_SERVO_PWM_GPIO, 0);
    gpio_set_level(CONFIG_RS485_DIR_GPIO, 0);
}


void ioTest_setOutputsToInputStates() {
    ESP_LOGI("TEST", "Passing through inputs to outputs... \n (order: screw-terminals-inputs: left-to-right => screw-terminals-outputs-openDrain: left-to-right)...");
    gpio_set_level(CONFIG_RELAY_VFD1_GPIO, gpio_get_level(CONFIG_REMOTE_OPEN_GPIO));
    gpio_set_level(CONFIG_RELAY_VFD2_GPIO, gpio_get_level(CONFIG_REMOTE_CLOSE_GPIO));
    gpio_set_level(CONFIG_BUZZER_GPIO, gpio_get_level(CONFIG_BTN_OPEN_GPIO));
    gpio_set_level(CONFIG_LIGHTBARRIER_EN_GPIO, gpio_get_level(CONFIG_BTN_CLOSE_GPIO));
    gpio_set_level(CONFIG_SERVO_ENABLE_GPIO, gpio_get_level(CONFIG_SW_G1_CLOSED_GPIO));
    gpio_set_level(CONFIG_LED_GPIO, gpio_get_level(CONFIG_SW_G1_OPEN_GPIO));
}







// Task to test Modbus communication with VFD
void modbus_task(void *arg)
{
    ESP_LOGW(TAG, "sending stop command");
    send_modbus_command(0x01, 0x06, 0x0003, 2);
    while (1)
    {
        // Example: Turn on VFD (slave address 0x01, function code 0x06, register 0x0003, value 0x0001)

        // send start command
        // ESP_LOGW(TAG, "sending start command");
        // send_modbus_command(0x01, 0x06, 0x0003, 0x0001);
        // vTaskDelay(pdMS_TO_TICKS(1000));

        // read voltage
        ESP_LOGW(TAG, "reading bus voltage...");
        uint16_t reg_value = 0;
        int status = read_modbus_register(0x01, 0x0008, &reg_value);
        ESP_LOGW(TAG, "  voltage: %d\n", reg_value);

        // read current
        ESP_LOGW(TAG, "reading bus current...");
        status = read_modbus_register(0x01, 0x0009, &reg_value);
        ESP_LOGW(TAG, "  current: %d\n", reg_value);

        // read temperature
        ESP_LOGW(TAG, "reading temperature...");
        status = read_modbus_register(0x01, 10, &reg_value);
        ESP_LOGW(TAG, "  temperature: %d\n", reg_value);

        //TODO verify status is 0 (success) before using the value


        // set frequency
        ESP_LOGW(TAG, "sending setFreq cmd...");
        status = send_modbus_command(0x01, 0x06, 0x0002, 100);
        ESP_LOGW(TAG, "  %s\n", status == 0 ? "success" : "failed");
        printf("=============\n");
        vTaskDelay(pdMS_TO_TICKS(2000));

        // send stop command
        // ESP_LOGW(TAG, "sending stop command");
        // send_modbus_command(0x01, 0x06, 0x0003, 2);
        // vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{

    // initialize gpio pins as inputs/outputs
    configure_gpio_pins();

    // GPIO Test
    ESP_LOGW(TAG, "starting GPIO test");
    while (1)
    {
        ioTest_readAllInputs();
        ioTest_setOutputsToInputStates();
        vTaskDelay(pdMS_TO_TICKS(500));
    }


    // MODBUS Test
    esp_log_level_set("Modbus-RTU", ESP_LOG_INFO);
    // Configure UART
    modbus_init();

    // Start Modbus task for testing
    ESP_LOGW(TAG, "starting modbus task...");
    xTaskCreate(modbus_task, "modbus_task", 4 * 2048, NULL, 10, NULL);
}
