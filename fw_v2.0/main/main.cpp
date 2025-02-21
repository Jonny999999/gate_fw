extern "C" {
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "modbus.h"
#include "iotest.h"
#include "config.h"
}

#include "vfd.hpp"
#include "buzzer.hpp"
#include "gate.hpp"

#include "control.hpp"


static const char *TAG = "main";
//create buzzer object on pin 12 with gap between queued events of 500ms 
buzzer_t buzzer(CONFIG_BUZZER_GPIO, 100);

//======================================
//============ buzzer task =============
//======================================
//TODO: move the task creation to buzzer class (buzzer.cpp)
//e.g. only have function buzzer.createTask() in app_main
void task_buzzer( void * pvParameters ){
    ESP_LOGI("task_buzzer", "Start of buzzer task...");
        //run function that waits for a beep events to arrive in the queue
        //and processes them
        buzzer.processQueue();
}

// Configure all GPIO pins according to config.h
void configure_gpio_pins() {
    // TODO: most pins are initialized in the task they are used, thus can be dropped here -> move to iotest only?
    // configuration struct for gpio pins
    gpio_config_t io_conf;

    ESP_LOGW(TAG, "configuring GPIO pins...");

    // configure all inputs at once
    uint64_t input_pins = 
        (1ULL << CONFIG_SW_G1_OPEN_GPIO) |
        (1ULL << CONFIG_SW_G1_CLOSED_GPIO) |
        (1ULL << CONFIG_SW_G2_OPEN_GPIO) |
        (1ULL << CONFIG_SW_G2_CLOSED_GPIO) |
        (1ULL << CONFIG_ENCODER1_GPIO) |
        (1ULL << CONFIG_ENCODER2_GPIO) |
        (1ULL << CONFIG_BTN_OPEN_GPIO) |
        (1ULL << CONFIG_BTN_CLOSE_GPIO) |
        (1ULL << CONFIG_REMOTE_OPEN_GPIO) |
        (1ULL << CONFIG_REMOTE_CLOSE_GPIO) |
        (1ULL << CONFIG_LIGHTBARRIER_GPIO) |
        (1ULL << CONFIG_FN_BUTTON_GPIO);
    io_conf.intr_type = GPIO_INTR_DISABLE; // Disable interrupts
    io_conf.mode = GPIO_MODE_INPUT;       // Set as input
    io_conf.pin_bit_mask = input_pins;    // Pin mask for inputs
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // No internal pulldown (external pullup present)
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     // No internal pullup (optocoupler pulls low when active)
    gpio_config(&io_conf);

    // configure all outputs at once
    uint64_t output_pins = 
        (1ULL << CONFIG_SERVO_ENABLE_GPIO) |
        (1ULL << CONFIG_LIGHTBARRIER_EN_GPIO) |
        (1ULL << CONFIG_LED_GPIO) |
        (1ULL << CONFIG_BUZZER_GPIO) |
        (1ULL << CONFIG_RELAY_VFD1_GPIO) |
        (1ULL << CONFIG_RELAY_VFD2_GPIO) |
        (1ULL << CONFIG_SERVO_PWM_GPIO) |
        (1ULL << CONFIG_RS485_DIR_GPIO) |
        (1ULL << CONFIG_RS485_TX_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;      // Set as output
    io_conf.pin_bit_mask = output_pins;  // Pin mask for outputs
    gpio_config(&io_conf);

    ESP_LOGW(TAG, "done.");
}



// Task function that repeatedly calls gate1.handle()
void gateHandleTask(void *pvParameters)
{
    Gate* gate = (Gate*) pvParameters;
    while (true) {
        gate->handle();
        vTaskDelay(pdMS_TO_TICKS(500));  // Call handle() every 100 ms.
    }
}






//#define RUN_GPIO_TEST
//#define RUN_MODBUS_TEST
//#define RUN_GATE_TEST

extern "C" void app_main(void)
{

    // initialize gpio pins as inputs/outputs
    configure_gpio_pins();

    // Configure UART
    modbus_init();

    // set loglevel
    esp_log_level_set("Modbus-RTU", ESP_LOG_WARN);
    esp_log_level_set("IO-test", ESP_LOG_INFO);
    esp_log_level_set("VFD", ESP_LOG_WARN);
    esp_log_level_set("Gate1_West", ESP_LOG_INFO);
    esp_log_level_set("Gate2_East", ESP_LOG_INFO);
    esp_log_level_set("buzzer", ESP_LOG_ERROR);
    esp_log_level_set("control", ESP_LOG_INFO);

    //--- create task for buzzer ---
    xTaskCreate(&task_buzzer, "task_buzzer", 2048, NULL, 5, NULL);

    // beep at startup
    buzzer.beep(3, 50, 100);

    // Create VFD instances
    VFD vfd1(0x01); // VFD 1 with address 0x01
    VFD vfd2(0x02); // VFD 2 with address 0x02



    // Create a Gate instance using the configured GPIOs.
    // Parameters:
    //   - Gate name: "Gate1"
    //   - kLimitSwitchOpenGpio: CONFIG_SW_G1_OPEN_GPIO (e.g., GPIO_NUM_5)
    //   - kLimitSwitchClosedGpio: CONFIG_SW_G1_CLOSED_GPIO (e.g., GPIO_NUM_18)
    //   - kRelayPinGpio: CONFIG_RELAY_VFD1_GPIO (e.g., GPIO_NUM_25)
    //   - pointer to VFD instance: vfd1 (created earlier)
    //   - pointer to Buzzer instance: buzzer (assumed global or instantiated)
    //   - full run duration (0% to 100%): 5000 ms
    Gate gate1West("Gate1_West",
               CONFIG_SW_G1_OPEN_GPIO, 0, // active low (switch NO to GND -> optocoupler ON)
               CONFIG_SW_G1_CLOSED_GPIO, 1, // active high (switch NC to GND -> optocoupler OFF)
               CONFIG_RELAY_VFD1_GPIO,
               &vfd1,
               &buzzer,
               15000
    );



    Gate gate2East("Gate2_East",
               CONFIG_SW_G2_OPEN_GPIO, 0, // active low (switch NO to GND -> optocoupler ON)
               CONFIG_SW_G2_CLOSED_GPIO, 1, // active high (switch NC to GND -> optocoupler OFF)
               CONFIG_RELAY_VFD2_GPIO,
               &vfd2,
               &buzzer,
               15000
    );



ControlConfig controlConfig = {
    .gateA = &gate1West,
    .gateB = &gate2East,
    .remoteOpenGpio = CONFIG_REMOTE_OPEN_GPIO,
    .remoteCloseGpio = CONFIG_REMOTE_CLOSE_GPIO,
    .buttonOpenGpio = CONFIG_BTN_OPEN_GPIO,
    .buttonCloseGpio = CONFIG_BTN_CLOSE_GPIO,
    .buzzer = &buzzer
};

#ifndef RUN_GATE_TEST
    // Create Task handling user input and the gates
    xTaskCreate(controlTask, "ControlTask", 4096*2, (void*)&controlConfig, 5, nullptr);
#endif



#ifdef RUN_GATE_TEST
    // Create a task that continuously calls gate1.handle()
    xTaskCreate(gateHandleTask, "gateHandleTask", 2048*5, &gate1, 5, NULL);



    ESP_LOGW("GateTest", "Starting Gate Test Sequence.");

    // --- Test Sequence with simple delays ---
    
    // 1. Open the gate for 3000 ms.
    ESP_LOGW("GateTest", "Command: Open for 3000 ms");
    gate1.openForMs(3000);
    vTaskDelay(pdMS_TO_TICKS(10000));  // Wait longer than 3000 ms to let the movement finish

    // 2. Run the gate to fully open (100%).
    ESP_LOGW("GateTest", "Command: Run to 100%% (Fully Open)");
    gate1.runTo(100.0f);
    vTaskDelay(pdMS_TO_TICKS(10000));  // Wait long enough for full open movement

    // 3. Run the gate to fully close (0%).
    ESP_LOGW("GateTest", "Command: Run to 0%% (Fully Closed)");
    gate1.runTo(0.0f);
    vTaskDelay(pdMS_TO_TICKS(10000));  // Wait long enough for full close movement

    // 4. Open the gate for 5000 ms, but stop it manually after 2000 ms.
    ESP_LOGW("GateTest", "Command: Open for 5000 ms, manual stop after 2000 ms");
    gate1.openForMs(5000);
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait 2 seconds
    ESP_LOGW("GateTest", "Command: stopping gate");
    gate1.stop();                     // Issue a stop command
    vTaskDelay(pdMS_TO_TICKS(3000));  // Allow time for the stop to take effect

    ESP_LOGW("GateTest", "Gate Test Sequence Completed.");
    
    // Optionally, suspend or delete this task if no further testing is required.
    vTaskDelay(pdMS_TO_TICKS(100000));
    vTaskDelete(NULL);

#endif


#ifdef RUN_MODBUS_TEST


    while (1)
    {
        uint16_t temperature;
        float voltage, current;

        // Control VFD 1
        vfd1.setFrequency(20); // Set frequency to 200 Hz
        vfd1.start();
        vTaskDelay(pdMS_TO_TICKS(2000));
        vfd1.getVoltage(&voltage);
        vfd1.getCurrent(&current);
        vfd1.getTemperature(&temperature);
        ESP_LOGI("Main", "VFD1 -> Voltage: %.1f V, Current: %.1f A, Temperature: %d C", voltage, current, temperature);
        vfd1.stop();
        vTaskDelay(pdMS_TO_TICKS(500));


        //// Control VFD 2
        //vfd2.setFrequency(10); // Set frequency to 150 Hz
        //vfd2.start();
        //vTaskDelay(pdMS_TO_TICKS(1500));
        //vfd2.getVoltage(&voltage);
        //vfd2.getCurrent(&current);
        //vfd2.getTemperature(&temperature);
        //ESP_LOGI("Main", "VFD2 -> Voltage: %d, Current: %d, Temperature: %d", voltage, current, temperature);
        //vfd2.stop();

        vTaskDelay(pdMS_TO_TICKS(5000));
    }

#endif


#ifdef RUN_GPIO_TEST
    // GPIO Test
    ESP_LOGW(TAG, "starting GPIO test");
    while (1)
    {
        ioTest_readAllInputs();
        ioTest_setOutputsToInputStates();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
#endif


// keep main task alive forever 
// (prevent deconstruction of objects passed to tasks)
while(1) vTaskDelay(portMAX_DELAY); 

}
