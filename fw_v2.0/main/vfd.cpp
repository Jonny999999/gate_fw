#include "vfd.hpp"

extern "C" {
#include "esp_log.h"

#include "modbus.h"
}


// Define register addresses (see screenshot manual: doc/vfd/T13-400W-12-HT13-750W-12H_modbus.jpg)
#define VFD_REG_START_STOP  0x0003 // Start/Stop command register (R/W: 1 = start-fwd, 5 = start-rev, 2 = stop)
#define VFD_REG_FREQUENCY   0x0002 // Frequency command register (R/W: 500=50Hz)
#define VFD_REG_VOLTAGE     0x0008 // Bus voltage register (R: 3100 = 310V)
#define VFD_REG_CURRENT     0x0009 // Bus current register (R: 132 = 1.32A)
#define VFD_REG_TEMPERATURE 0x000A // Temperature register (R: 43 = 43C)



// Variables
static const char *TAG = "VFD";



// Constructor
VFD::VFD(uint8_t slave_addr) : slave_addr(slave_addr), frequency(0), is_running(false) {
    // Note: deliberately no communication with the drive here.
    // The VFDs are powered through the relays, which are OFF at boot by design, so any
    // register read at construction time is guaranteed to time out. It used to do exactly
    // that and reported "Failed to initialize VFD" on every single start - an error message
    // for the completely normal case, which is worse than no message at all.
    // Use probe() once the relay has been on long enough if the drive should be checked.
    ESP_LOGI(TAG, "VFD %d created (not contacted yet - powered via relay)", slave_addr);
}


esp_err_t VFD::probe() {
    // Read back the frequency register to confirm the drive is powered and talking.
    // Only meaningful once the relay has been on for at least DELAY_VFD_STARTUP.
    uint16_t reg_value = 0;
    esp_err_t status = read_modbus_register(slave_addr, VFD_REG_FREQUENCY, &reg_value);
    if (status == ESP_OK) {
        frequency = reg_value / 10;
        ESP_LOGI(TAG, "VFD %d responded, frequency register = %d", slave_addr, reg_value);
    } else {
        ESP_LOGE(TAG, "VFD %d did not respond. Error: 0x%x", slave_addr, status);
    }
    return status;
}



esp_err_t VFD::start(bool directionFwd) {
    uint16_t reg_value = directionFwd ? 0x0001 : 0x0005; // 1=forward,  5=reverse,  2=stop  - Note: Manual states 3 for reverse but it is actually 5
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
    // FIXME: find out reason whe VFD2 can not be stopped without error, when opening. Current workaround, request random register first (which triggers the error) then do actual command which works then -> invalid state before?
    ESP_LOGW(TAG, "Stop VFD %d - WORKAROUND: Read Register first before stopping (this may fail)...", slave_addr);
    uint16_t reg_value = 0;
    read_modbus_register(slave_addr, VFD_REG_START_STOP, &reg_value);
    ESP_LOGW(TAG, "Stop VFD %d - WORKAROUND done, Continue sending actual stop command now...", slave_addr);
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
        *out_current_A = (float)reg_value/100;
        ESP_LOGD(TAG, "Current read for VFD %d: %.2f A.", slave_addr, reg_value/100.0);
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
