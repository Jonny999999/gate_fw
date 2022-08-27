#include "config.hpp"

//==========================================
//====== Configure evaluated switches ======
//==========================================
//TODO: evaluate limit switches too?
//buttons
gpio_evaluatedSwitch buttonOpen(GPIO_S_OPEN, false, true); //pullup false, inverted true (switch to 12V (pulldown on pcb))
gpio_evaluatedSwitch buttonClose(GPIO_S_CLOSE, false, true); //pullup false, inverted true (switch to 12V (pulldown on pcb))
//remote
gpio_evaluatedSwitch remoteOpen(GPIO_S_REMOTE_OPEN, false, false); //pullup false, not inverted (switch to GND, pullup on receiver pcb)
gpio_evaluatedSwitch remoteClose(GPIO_S_REMOTE_CLOSE, false, false); //pullup false, not inverted (switch to GND, pullup on receiver pcb)


//====================================
//========== Gate objects ============
//====================================
//gate::gate(
//        gpio_num_t gpio_relayOpen_f,
//        gpio_num_t gpio_relayClose_f,
//        gpio_num_t gpio_switchOpen_f,
//        gpio_num_t gpio_switchClosed_f,
//        const char name_f[16],
//        uint32_t msStop_f,
//        uint32_t msRetry_f,
//        uint32_t msTimeout_f
//        ){

//Create and configure gate objects
//------ right gate ------
//create gate object
gate gateRight(GPIO_K_OPEN_RIGHT, GPIO_K_CLOSE_RIGHT, GPIO_B_RIGHT_OPEN, GPIO_B_RIGHT_CLOSED, "right", 1000, 2000, 10000);

//------ left gate ------
//create gate object
gate gateLeft(GPIO_K_OPEN_LEFT, GPIO_K_CLOSE_LEFT, GPIO_B_LEFT_OPEN, GPIO_B_LEFT_CLOSED, "left", 1000, 2000, 10000);
//note: actual gpio pins are defined globally in config.hpp

//create buzzer object on pin 12 with gap between queued events of 500ms 
buzzer_t buzzer(GPIO_NUM_12, 100);
