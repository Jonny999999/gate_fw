#pragma once
#include "driver/gpio.h"
// Assign GPIO pins and configure options in this file


//==========================================
//===== UART / RS485 / Modbus settings =====
//==========================================
#define CONFIG_BAUD_RATE            9600  //vfd supports: 4800, 9600, 19200, 38400
#define CONFIG_RS485_DIR_GPIO       GPIO_NUM_4
#define CONFIG_RS485_TX_GPIO        GPIO_NUM_17
#define CONFIG_RS485_RX_GPIO        GPIO_NUM_16
#define CONFIG_RS485_RX_INVERTED    1 // 1 if RX signal is inverted, 0 otherwise


//===========================
//===== Input GPIO pins =====
//===========================
// unless noted otherwise, all inputs are active LOW (switches wired to pull input to GND)
#define CONFIG_SW_G1_OPEN_GPIO      GPIO_NUM_5  // Limit switch for Gate 1: Open (switch closed to GND (LOW) when gate is fully open)
#define CONFIG_SW_G1_CLOSED_GPIO    GPIO_NUM_18   // Limit switch for Gate 1: Closed (switch OPEN (HIGH) when gate is fully closed)
#define CONFIG_FN_BUTTON_GPIO       GPIO_NUM_34  // Function button
// Logical inversion for specific signals
#define CONFIG_SW_G1_CLOSED_INVERTED 1  // Gate 1 Closed signal is inverted (switch only open when gate is fully closed)
#define CONFIG_SW_G2_CLOSED_INVERTED 1  // Gate 2 Closed signal is inverted (switch only open when gate is fully closed)


//============================
//===== Output GPIO pins =====
//============================
#define CONFIG_SERVO_ENABLE_GPIO    GPIO_NUM_12  // P-MOSFET Enable supply for Servo (outputs 12V when HIGH)
#define CONFIG_LIGHTBARRIER_EN_GPIO GPIO_NUM_14  // P-MOSFET Enable supply for Light Barrier (outputs 12V-filtered when HIGH)
#define CONFIG_LED_GPIO             GPIO_NUM_13  // Status LED (outputs GND when HIGH)
#define CONFIG_BUZZER_GPIO          GPIO_NUM_27  // Buzzer (on when HIGH)
#define CONFIG_RELAY_VFD1_GPIO      GPIO_NUM_25  // Relay for VFD1 (turns Relay on when HIGH)
#define CONFIG_RELAY_VFD2_GPIO      GPIO_NUM_26  // Relay for VFD2 (turns Relay on when HIGH)
#define CONFIG_SERVO_PWM_GPIO       GPIO_NUM_33  // Servo PWM Signal (non inverting, outputs 5V when HIGH, pulls to GND when LOW)


 