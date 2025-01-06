#pragma once
// Assign GPIO pins and configure options in this file


//==========================================
//===== UART / RS485 / Modbus settings =====
//==========================================
#define CONFIG_BAUD_RATE            9600  //vfd supports: 4800, 9600, 19200, 38400
#define CONFIG_RS485_DIR_GPIO       4
#define CONFIG_RS485_TX_GPIO        17
#define CONFIG_RS485_RX_GPIO        16
#define CONFIG_RS485_RX_INVERTED    1 // 1 if RX signal is inverted, 0 otherwise


//===========================
//===== Input GPIO pins =====
//===========================
// unless noted otherwise, all inputs are active LOW (switches wired to pull input to GND)
#define CONFIG_SW_G1_OPEN_GPIO      5  // Limit switch for Gate 1: Open (switch closed to GND (LOW) when gate is fully open)
#define CONFIG_SW_G1_CLOSED_GPIO    18   // Limit switch for Gate 1: Closed (switch OPEN (HIGH) when gate is fully closed)
#define CONFIG_SW_G2_OPEN_GPIO      36   // Limit switch for Gate 2: Open (switch closed to GND (LOW) when gate is fully open)
#define CONFIG_SW_G2_CLOSED_GPIO    2  // Limit switch for Gate 2: Closed (switch open (HIGH) when gate is fully closed)
#define CONFIG_ENCODER1_GPIO        35  // Encoder Gate 1 signal
#define CONFIG_ENCODER2_GPIO        32  // Encoder Gate 2 signal
#define CONFIG_BTN_OPEN_GPIO        21  // Button Open
#define CONFIG_BTN_CLOSE_GPIO       19  // Button Close
#define CONFIG_REMOTE_OPEN_GPIO     23  // Remote Open signal
#define CONFIG_REMOTE_CLOSE_GPIO    22  // Remote Close signal
#define CONFIG_LIGHTBARRIER_GPIO    39  // Light barrier input (NPN sensor pulls low when obstructed)
#define CONFIG_FN_BUTTON_GPIO       34  // Function button
// Logical inversion for specific signals
#define CONFIG_SW_G1_CLOSED_INVERTED 1  // Gate 1 Closed signal is inverted (switch only open when gate is fully closed)
#define CONFIG_SW_G2_CLOSED_INVERTED 1  // Gate 2 Closed signal is inverted (switch only open when gate is fully closed)


//============================
//===== Output GPIO pins =====
//============================
#define CONFIG_SERVO_ENABLE_GPIO    12  // P-MOSFET Enable supply for Servo (outputs 12V when HIGH)
#define CONFIG_LIGHTBARRIER_EN_GPIO 14  // P-MOSFET Enable supply for Light Barrier (outputs 12V-filtered when HIGH)
#define CONFIG_LED_GPIO             13  // Status LED (outputs GND when HIGH)
#define CONFIG_BUZZER_GPIO          27  // Buzzer (on when HIGH)
#define CONFIG_RELAY_VFD1_GPIO      25  // Relay for VFD1 (turns Relay on when HIGH)
#define CONFIG_RELAY_VFD2_GPIO      26  // Relay for VFD2 (turns Relay on when HIGH)
#define CONFIG_SERVO_PWM_GPIO       33  // Servo PWM Signal (non inverting, outputs 5V when HIGH, pulls to GND when LOW)


 