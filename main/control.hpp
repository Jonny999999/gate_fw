
//variable declarations
const char* controlStateStr[3] = {"IDLE", "WAIT_FOR_INPUT", "MOVING_TO_TARGET"};
enum class controlState {IDLE, WAIT_FOR_INPUT, MOVING_TO_TARGET};
controlState ctlState = controlState::IDLE;
uint32_t timestampLastAction;
uint8_t countPressed = 0;

//control function
static const char *TAG_CTL = "control";

void control(){

    switch(ctlState){
        case controlState::IDLE:
            //--- close gate ---
            if (buttonClose.risingEdge){
                ESP_LOGW(TAG_CTL, "Closing completely");
                gateLeft.close(20000);
                gateRight.close(20000);
                ctlState = controlState::MOVING_TO_TARGET;
            //--- open gate ---
            }else if (buttonOpen.risingEdge){
                ESP_LOGI(TAG_CTL, "Opening (waiting for further input)");
                gateLeft.open(20000);
                gateRight.open(20000);//FIXME: provide value that makes more sense
                countPressed = 1;
                timestampLastAction = esp_log_timestamp();
                ctlState = controlState::WAIT_FOR_INPUT;
            }
            break;


        case controlState::WAIT_FOR_INPUT:
            //--- open completely ---
            if (buttonOpen.fallingEdge && buttonOpen.msPressed > 1000){ //open button was longer than 1s pressed
                ESP_LOGW(TAG_CTL, "Opening completely");
                ctlState = controlState::MOVING_TO_TARGET;

            //--- increment open duration ---
            }else if (buttonOpen.risingEdge){ //open button high again
                ESP_LOGI(TAG_CTL, "Incrementing open duration - total: %d", countPressed);
                countPressed++;
                timestampLastAction = esp_log_timestamp();
                
            //--- timeout ---
            }else if (esp_log_timestamp() - timestampLastAction > 2000){ //no input for more then 2s
                ESP_LOGW(TAG_CTL, "Timeout - applying target duration");
                ctlState = controlState::MOVING_TO_TARGET;
                gateLeft.setDuration(2000*countPressed);
                gateRight.setDuration(2000*countPressed);
                ctlState = controlState::MOVING_TO_TARGET;
            }

            
            break;
        case controlState::MOVING_TO_TARGET:
            //--- idle when gates stopped ---
            if (gateLeft.state == gateState::IDLE && gateRight.state == gateState::IDLE){ //both do not move
                ESP_LOGI(TAG_CTL, "Done - both gates have stopped");
                ctlState = controlState::IDLE;
            //--- stop with any button ---
            }else if (buttonClose.risingEdge || buttonOpen.risingEdge){ //any button is pressed
                //stop gates
                ESP_LOGE(TAG_CTL, "Stopped via button!");
                gateLeft.stop();
                gateRight.stop();
                ctlState = controlState::IDLE;
            }
            break;
    }


}
