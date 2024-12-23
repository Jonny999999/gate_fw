#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

// UART settings
#define UART_NUM            UART_NUM_1
#define TXD_PIN             17 // TX pin
#define RXD_PIN             16 // RX pin
#define RTS_PIN             18 // DE pin
#define BAUD_RATE           9600
#define BUF_SIZE            256

static const char *TAG = "MODBUS";

// Function to calculate Modbus CRC16
uint16_t modbus_crc16(const uint8_t *data, uint8_t length) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

// Function to send a Modbus RTU frame
void send_modbus_command(uint8_t slave_addr, uint8_t function_code, uint16_t reg_addr, uint16_t value) {
    ESP_LOGI(TAG, "send_modbus_command: addr %d, func: %d, regAddr: %d, value: %d", slave_addr, function_code, reg_addr, value);

    uint8_t frame[8];
    frame[0] = slave_addr;              // Slave address
    frame[1] = function_code;           // Function code (Write single register)
    frame[2] = (reg_addr >> 8) & 0xFF;  // Register address high byte
    frame[3] = reg_addr & 0xFF;         // Register address low byte
    frame[4] = (value >> 8) & 0xFF;     // Value high byte
    frame[5] = value & 0xFF;            // Value low byte
    uint16_t crc = modbus_crc16(frame, 6);
    frame[6] = crc & 0xFF;              // CRC low byte
    frame[7] = (crc >> 8) & 0xFF;       // CRC high byte

    // Send frame
    uart_write_bytes(UART_NUM, (const char *)frame, sizeof(frame));
    ESP_LOGI(TAG, "Sent Modbus frame");
}

// Task to test Modbus communication
void modbus_task(void *arg) {
    while (1) {
        // Example: Turn on VFD (slave address 0x01, function code 0x06, register 0x0003, value 0x0001)
        ESP_LOGW(TAG, "sending start command");
        send_modbus_command(0x01, 0x06, 0x0003, 0x0001);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGW(TAG, "sending setFreq cmd");
        send_modbus_command(0x01, 0x06, 0x0002, 330);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGW(TAG, "sending start command");
        send_modbus_command(0x01, 0x06, 0x0003, 0x0001);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGW(TAG, "sending stop command");
        send_modbus_command(0x01, 0x06, 0x0003, 2);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void) {
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, RTS_PIN, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_mode(UART_NUM, UART_MODE_RS485_HALF_DUPLEX));
    // invert out pins (due to mosfet level shifter)
    ESP_ERROR_CHECK(uart_set_line_inverse(UART_NUM, UART_SIGNAL_TXD_INV | UART_SIGNAL_RTS_INV));

    ESP_LOGW(TAG, "Modbus communication initialized");

    // Start Modbus task
    ESP_LOGW(TAG, "starting modbus task...");
    xTaskCreate(modbus_task, "modbus_task", 2048, NULL, 10, NULL);
}
