#include "gpio_evaluateSwitch.hpp"

//==================================
//=========== GPIO Pins ============
//==================================
//buttons
#define GPIO_S_OPEN GPIO_NUM_14
#define GPIO_S_CLOSE GPIO_NUM_27

//limit switches
#define GPIO_B_RIGHT_OPEN GPIO_NUM_32
#define GPIO_B_RIGHT_CLOSED GPIO_NUM_33
#define GPIO_B_LEFT_OPEN GPIO_NUM_25
#define GPIO_B_LEFT_CLOSED GPIO_NUM_26

//outputs
#define GPIO_K_OPEN_RIGHT GPIO_NUM_15
#define GPIO_K_CLOSE_RIGHT GPIO_NUM_2
#define GPIO_K_OPEN_LEFT GPIO_NUM_16
#define GPIO_K_CLOSE_LEFT GPIO_NUM_4

//=========================================
//=== Create evaluated switch objects =====
//=========================================
gpio_evaluatedSwitch buttonOpen(GPIO_S_OPEN, false, true); //pullup false, inverted true (switch to 12V (pulldown on pcb))
gpio_evaluatedSwitch buttonClose(GPIO_S_CLOSE, false, true); //pullup false, inverted true (switch to 12V (pulldown on pcb))
                                                             //TODO: evaluate limit switches too?
