#include "config.hpp"

//==========================================
//====== Configure evaluated switches ======
//==========================================
//TODO: evaluate limit switches too?
gpio_evaluatedSwitch buttonOpen(GPIO_S_OPEN, false, true); //pullup false, inverted true (switch to 12V (pulldown on pcb))
gpio_evaluatedSwitch buttonClose(GPIO_S_CLOSE, false, true); //pullup false, inverted true (switch to 12V (pulldown on pcb))


//====================================
//========== Gate objects ============
//====================================
//Create and configure gate objects
//------ right gate ------
//create gate object
gate gateRight(GPIO_K_OPEN_RIGHT, GPIO_K_CLOSE_RIGHT, GPIO_B_RIGHT_OPEN, GPIO_B_RIGHT_CLOSED, "right", 10000);

//------ left gate ------
//create gate object
gate gateLeft(GPIO_K_OPEN_LEFT, GPIO_K_CLOSE_LEFT, GPIO_B_LEFT_OPEN, GPIO_B_LEFT_CLOSED, "left", 10000);
//note: actual gpio pins are defined globally in config.hpp

