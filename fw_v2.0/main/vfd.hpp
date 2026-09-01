#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
}



// custom class for controlling a VFD via modbus
// tested with / created for 750W single-phase-supply VFDs

// assumes modbus_init (from modbus.c) was run before already.

class VFD
{
public:
    VFD(uint8_t modbusSlaveAddress);

    // Public API for controlling the VFD
    esp_err_t start(bool directionFwd = true);                      // Start motor
    esp_err_t stop();                       // Stop motor
    esp_err_t setFrequency(uint16_t newFrequency_Hz);  // Set frequency (in Hz)

    esp_err_t getFrequency(uint16_t *currentFrequency_Hz);  // Get currently set frequency (in Hz)
    esp_err_t getVoltage(float *voltage_V); // Get bus voltage
    esp_err_t getCurrent(float *current_A); // Get bus current
    esp_err_t getTemperature(uint16_t *temperature_C); // Get temperature

    bool isRunning() const; // get current cached state

    // Check whether the drive is powered and answering. Only meaningful once its relay has
    // been on for at least the VFD startup delay - not at construction time, when the relay
    // is still off.
    esp_err_t probe();


private:
    uint8_t uart_num;  // UART port number
    uint8_t slave_addr;   // Slave address
    uint16_t frequency; // Tracks the current frequency
    bool is_running = false;

    // Helper for sending commands
    esp_err_t sendCommand(uint8_t function_code, uint16_t reg_addr, uint16_t value);
    esp_err_t readRegister(uint16_t reg_addr, uint16_t *value);
};
