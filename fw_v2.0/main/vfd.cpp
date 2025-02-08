#include "vfd.hpp"

extern "C" {
#include "esp_log.h"

#include "modbus.h"
}


// Define register addresses
#define VFD_REG_START_STOP  0x0003 // Start/Stop command register
#define VFD_REG_FREQUENCY   0x0002 // Frequency command register
#define VFD_REG_VOLTAGE     0x0008 // Bus voltage register
#define VFD_REG_CURRENT     0x0009 // Bus current register
#define VFD_REG_TEMPERATURE 0x000A // Temperature register



// Variables
static const char *TAG = "VFD";



// Constructor
VFD::VFD(uint8_t slave_addr) : slave_addr(slave_addr), frequency(0), is_running(false) {
    // Perform a self-test by attempting to read the frequency
    uint16_t reg_value = 0;
    esp_err_t status = read_modbus_register(slave_addr, VFD_REG_FREQUENCY, &reg_value);
    
    if (status == ESP_OK) {
        frequency = reg_value; // Cache the initial frequency value
        ESP_LOGI(TAG, "VFD %d initialized successfully. Initial frequency: %d Hz.", slave_addr, frequency);
    } else {
        ESP_LOGE(TAG, "Failed to initialize VFD with address %d. Could not read frequency. Error: 0x%x", slave_addr, status);
    }
}



esp_err_t VFD::start(bool directionFwd) {
    uint16_t reg_value = directionFwd ? 0x0001 : 0x0003; // 1=forward,  3=reverse,  2=stop
    esp_err_t status = send_modbus_command(slave_addr, 0x06, VFD_REG_START_STOP, reg_value);
    if (status == ESP_OK) {
        is_running = true;
        ESP_LOGI(TAG, "VFD %d started successfully.", slave_addr);
    } else {
        ESP_LOGE(TAG, "Failed to start VFD %d. Error: 0x%x", slave_addr, status);
    }
    return status;
}



esp_err_t VFD::stop() {
    esp_err_t status = send_modbus_command(slave_addr, 0x06, VFD_REG_START_STOP, 0x0002);
    if (status == ESP_OK) {
        is_running = false;
        ESP_LOGI(TAG, "VFD %d stopped successfully.", slave_addr);
    } else {
        ESP_LOGE(TAG, "Failed to stop VFD %d. Error: 0x%x", slave_addr, status);
    }
    return status;
}



esp_err_t VFD::setFrequency(uint16_t freq_hz) {
    esp_err_t status = send_modbus_command(slave_addr, 0x06, VFD_REG_FREQUENCY, freq_hz*10); //TODO support 1 decimal place?
    if (status == ESP_OK) {
        frequency = freq_hz; // Cache the frequency
        ESP_LOGI(TAG, "Frequency set to %d Hz for VFD %d.", freq_hz, slave_addr);
    } else {
        ESP_LOGE(TAG, "Failed to set frequency for VFD %d. Error: 0x%x", slave_addr, status);
    }
    return status;
}



esp_err_t VFD::getFrequency(uint16_t *out_freq) {
    if (!out_freq) {
        return ESP_ERR_INVALID_ARG; // Return an error if the pointer is null
    }
    uint16_t register_value = 0;
    // Replace the following line with the appropriate function or code to read the frequency register
    esp_err_t err = readRegister(VFD_REG_FREQUENCY, &register_value);

    if (err != ESP_OK) {
        return err; // Return the error code if reading the register fails
    }

    // TODO add support for 1 decimal place? (return float)
    frequency = register_value/10; // update cached frequency
    // Set the output frequency based on the read value
    *out_freq = frequency;

    return ESP_OK; // Return success
}



esp_err_t VFD::getVoltage(float *out_voltage_V) {
    if (!out_voltage_V) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t reg_value = 0;
    esp_err_t status = read_modbus_register(slave_addr, VFD_REG_VOLTAGE, &reg_value);
    if (status == ESP_OK) {
        *out_voltage_V = (float)reg_value/10;
        ESP_LOGI(TAG, "Voltage read for VFD %d: %d V.", slave_addr, reg_value);
    } else {
        ESP_LOGE(TAG, "Failed to read voltage for VFD %d. Error: 0x%x", slave_addr, status);
    }
    return status;
}



esp_err_t VFD::getCurrent(float *out_current_A) {
    if (!out_current_A) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t reg_value = 0;
    esp_err_t status = read_modbus_register(slave_addr, VFD_REG_CURRENT, &reg_value);
    if (status == ESP_OK) {
        *out_current_A = (float)reg_value/10;
        ESP_LOGI(TAG, "Current read for VFD %d: %d A.", slave_addr, reg_value);
    } else {
        ESP_LOGE(TAG, "Failed to read current for VFD %d. Error: 0x%x", slave_addr, status);
    }
    return status;
}



esp_err_t VFD::getTemperature(uint16_t *out_temp) {
    if (!out_temp) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t reg_value = 0;
    esp_err_t status = read_modbus_register(slave_addr, VFD_REG_TEMPERATURE, &reg_value);
    if (status == ESP_OK) {
        *out_temp = reg_value;
        ESP_LOGI(TAG, "Temperature read for VFD %d: %d °C.", slave_addr, reg_value);
    } else {
        ESP_LOGE(TAG, "Failed to read temperature for VFD %d. Error: 0x%x", slave_addr, status);
    }
    return status;
}



bool VFD::isRunning() const {
    return is_running;
}
