#pragma once

#include "esp_log.h"
#include "driver/gpio.h"

#include "config.h"


static const char *TAG_iotest = "IO-test";

void ioTest_readAllInputs() {
    // order and grouping matches screw terminals from left to right
    ESP_LOGI(TAG_iotest, "======================");
    ESP_LOGI(TAG_iotest, "Reading input pins...");
    ESP_LOGI(TAG_iotest, "REMOTE_OPEN: %d", gpio_get_level(CONFIG_REMOTE_OPEN_GPIO));
    ESP_LOGI(TAG_iotest, "REMOTE_CLOSE: %d", gpio_get_level(CONFIG_REMOTE_CLOSE_GPIO));
    ESP_LOGI(TAG_iotest, "");
    ESP_LOGI(TAG_iotest, "BTN_OPEN: %d", gpio_get_level(CONFIG_BTN_OPEN_GPIO));
    ESP_LOGI(TAG_iotest, "BTN_CLOSE: %d", gpio_get_level(CONFIG_BTN_CLOSE_GPIO));
    ESP_LOGI(TAG_iotest, "");
    ESP_LOGI(TAG_iotest, "SW_G1_CLOSED: %d", gpio_get_level(CONFIG_SW_G1_CLOSED_GPIO));
    ESP_LOGI(TAG_iotest, "SW_G1_OPEN: %d", gpio_get_level(CONFIG_SW_G1_OPEN_GPIO));
    ESP_LOGI(TAG_iotest, "SW_G2_CLOSED: %d", gpio_get_level(CONFIG_SW_G2_CLOSED_GPIO));
    ESP_LOGI(TAG_iotest, "SW_G2_OPEN: %d", gpio_get_level(CONFIG_SW_G2_OPEN_GPIO));
    ESP_LOGI(TAG_iotest, "");
    ESP_LOGI(TAG_iotest, "LIGHTBARRIER: %d", gpio_get_level(CONFIG_LIGHTBARRIER_GPIO));
    ESP_LOGI(TAG_iotest, "FN_BUTTON: %d", gpio_get_level(CONFIG_FN_BUTTON_GPIO));
    ESP_LOGI(TAG_iotest, "");
    ESP_LOGI(TAG_iotest, "ENCODER1: %d", gpio_get_level(CONFIG_ENCODER1_GPIO));
    ESP_LOGI(TAG_iotest, "ENCODER2: %d", gpio_get_level(CONFIG_ENCODER2_GPIO));
    ESP_LOGI(TAG_iotest, "======================");
}


void ioTest_setAllOutputsHigh() {
    ESP_LOGI(TAG_iotest, "Turning all outputs ON...");
    gpio_set_level(CONFIG_SERVO_ENABLE_GPIO, 1);
    gpio_set_level(CONFIG_LIGHTBARRIER_EN_GPIO, 1);
    gpio_set_level(CONFIG_FAULT_LED_GPIO, 1);
    gpio_set_level(CONFIG_BUZZER_GPIO, 1);
    gpio_set_level(CONFIG_RELAY_VFD1_GPIO, 1);
    gpio_set_level(CONFIG_RELAY_VFD2_GPIO, 1);
    gpio_set_level(CONFIG_SERVO_PWM_GPIO, 1);
    gpio_set_level(CONFIG_RS485_DIR_GPIO, 1);
}

    
void ioTest_setAllOutputsLow() {
    ESP_LOGI(TAG_iotest, "Turning all outputs OFF...");
    gpio_set_level(CONFIG_SERVO_ENABLE_GPIO, 0);
    gpio_set_level(CONFIG_LIGHTBARRIER_EN_GPIO, 0);
    gpio_set_level(CONFIG_FAULT_LED_GPIO, 0);
    gpio_set_level(CONFIG_BUZZER_GPIO, 0);
    gpio_set_level(CONFIG_RELAY_VFD1_GPIO, 0);
    gpio_set_level(CONFIG_RELAY_VFD2_GPIO, 0);
    gpio_set_level(CONFIG_SERVO_PWM_GPIO, 0);
    gpio_set_level(CONFIG_RS485_DIR_GPIO, 0);
}


void ioTest_setOutputsToInputStates() {
    ESP_LOGI(TAG_iotest, "Passing through inputs to outputs... \n (order: screw-terminals-inputs: left-to-right => screw-terminals-outputs-openDrain: left-to-right)...");
    gpio_set_level(CONFIG_RELAY_VFD1_GPIO, !gpio_get_level(CONFIG_REMOTE_OPEN_GPIO));
    gpio_set_level(CONFIG_RELAY_VFD2_GPIO, !gpio_get_level(CONFIG_REMOTE_CLOSE_GPIO));
    gpio_set_level(CONFIG_BUZZER_GPIO, !gpio_get_level(CONFIG_BTN_OPEN_GPIO));
    gpio_set_level(CONFIG_LIGHTBARRIER_EN_GPIO, !gpio_get_level(CONFIG_BTN_CLOSE_GPIO));
    gpio_set_level(CONFIG_SERVO_ENABLE_GPIO, !gpio_get_level(CONFIG_ENCODER1_GPIO));
    gpio_set_level(CONFIG_FAULT_LED_GPIO, !gpio_get_level(CONFIG_SW_G1_OPEN_GPIO));
}