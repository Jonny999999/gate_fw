
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "config.h"



void modbus_init();
uint16_t modbus_crc16(const uint8_t *data, uint8_t length);
esp_err_t send_modbus_command(uint8_t slave_addr, uint8_t function_code, uint16_t reg_addr, uint16_t value);
esp_err_t read_modbus_register(uint8_t slave_addr, uint16_t reg_addr, uint16_t *value);