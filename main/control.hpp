#include "config.hpp"

//--- variable declarations ---
const char* controlStateStr[3] = {"IDLE", "WAIT_FOR_INPUT", "MOVING_TO_TARGET"};
enum class controlState {IDLE, WAIT_FOR_INPUT, MOVING_TO_TARGET};
controlState ctlState = controlState::IDLE;
uint32_t timestampLastAction;
uint8_t countPressed = 0;

//individual tag for logging
static const char *TAG_CTL = "control";


//================================
//======= control function =======
//================================
//function which decides according to user input (buttons open, close)
//when and how much the gates should be opened
void control(){

    switch(ctlState){
        //--------------------
        //------- IDLE -------
        //--------------------
        //wait for initial user input
        case controlState::IDLE:
            //--- button close ---
            //close gates completely
            if (buttonClose.risingEdge){
                ESP_LOGW(TAG_CTL, "Closing completely");
                buzzer.beep(1, 1000, 0);
                gateLeft.close(20000);
                gateRight.close(20000);
                ctlState = controlState::MOVING_TO_TARGET;

            //--- button open ---
            //open, wait for further input
            }else if (buttonOpen.risingEdge){
                ESP_LOGW(TAG_CTL, "Opening (waiting for further input)");
                buzzer.beep(1, 100, 0);
                gateLeft.open(20000);
                gateRight.open(20000);//FIXME: provide value that makes more sense
                countPressed = 0; //reset count
                timestampLastAction = esp_log_timestamp();
                ctlState = controlState::WAIT_FOR_INPUT;
            

            //--- remote close ---
            //close gates completely
            }else if (remoteClose.risingEdge){
                ESP_LOGW(TAG_CTL, "REMOTE: Closing completely");
                buzzer.beep(2, 500, 100);
                gateLeft.close(20000);
                gateRight.close(20000);
                ctlState = controlState::MOVING_TO_TARGET;

            //--- remote open ---
            //open gates completely
            }else if (remoteOpen.risingEdge){
                ESP_LOGW(TAG_CTL, "REMOTE: Opening completely");
                buzzer.beep(1, 1000, 0);
                gateLeft.open(20000);
                gateRight.open(20000);//FIXME: provide value that makes more sense
                ctlState = controlState::MOVING_TO_TARGET;
            }
            break;


        //--------------------------
        //----- WAIT_FOR_INPUT -----
        //--------------------------
        //wait for and process additional input
        //(decide what the user wants exactly while the gate already moves)
        case controlState::WAIT_FOR_INPUT:
            //--- stop ---
            if (buttonClose.state){ //close button is pressed while waiting for input
                gateLeft.stop(stopReason::CANCEL);
                gateRight.stop(stopReason::CANCEL);
                ctlState = controlState::IDLE;
                buzzer.beep(1, 400, 0);

            //--- open completely ---
            }else if (buttonOpen.state && buttonOpen.msPressed > 800){ //open button is pressed longer than 800ms
                ESP_LOGW(TAG_CTL, "Opening completely");
                buzzer.beep(1, 1000, 0);
                ctlState = controlState::MOVING_TO_TARGET;

            //--- increment open duration ---
            }else if (buttonOpen.risingEdge){ //open button high again
                ESP_LOGI(TAG_CTL, "Incrementing open duration - total: %d", countPressed);
                buzzer.beep(1, 60, 0);
                countPressed++;
                timestampLastAction = esp_log_timestamp();
                
            //--- timeout ---
            }else if (esp_log_timestamp() - timestampLastAction > 900){ //no input for more than 1200ms
                ESP_LOGW(TAG_CTL, "Timeout - applying target duration");
                ctlState = controlState::MOVING_TO_TARGET;
                //TODO use percentage calculated with openDuration from config instead of ms duration
                gateLeft.setDuration( 1100 + (400 * countPressed) );
                gateRight.setDuration( 1100 + (400 * countPressed) );
                if(countPressed > 1){
                    //signal that input has been applied
                    buzzer.beep(2, 40, 20);
                }
                ctlState = controlState::MOVING_TO_TARGET;
            }
            break;


            //------------------------------
            //------ MOVING_TO_TARGET ------
            //------------------------------
            //while gate moves to target, stop with buttons
            //or reset to idle when gates have stopped
        case controlState::MOVING_TO_TARGET:
            //--- idle when gates stopped at target or timeout ---
            if (gateLeft.state == gateState::IDLE && gateRight.state == gateState::IDLE){ //both do not move and are ready to receive new commands
                ESP_LOGW(TAG_CTL, "Done - both gates have stopped, returning to idle");
                ctlState = controlState::IDLE;

            //--- stop with any user input ---
            }else if (buttonClose.risingEdge || buttonOpen.risingEdge
                    || remoteOpen.risingEdge || remoteClose.risingEdge
                ){ //any remote input or button is pressed while moving to target
                //stop gates
                gateLeft.stop(stopReason::CANCEL);
                gateRight.stop(stopReason::CANCEL);
                //note: controlState gets switched in above case when WAIT_LOCK is actually over (both gates IDLE)
                buzzer.beep(1, 400, 0);
            }
            break;
    }


}
