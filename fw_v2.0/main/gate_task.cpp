#include "gate_task.hpp"

#include <atomic>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
}

//===============================
//========= Parameters ==========
//===============================
// How often both gate state machines are stepped. Note the actual period is longer while a
// gate is moving, because handle() then performs blocking modbus transactions - that is
// exactly why this no longer runs inside the control loop.
#define GATE_HANDLE_INTERVAL_MS 10

// Same priority as the control task: neither should starve the other, and the input task
// (priority 8) stays above both.
#define GATE_TASK_PRIORITY 5
#define GATE_TASK_STACK_SIZE (4096 * 2)

#define GATE_COMMAND_QUEUE_LENGTH 10


//===============================
//========== Variables ==========
//===============================
static const char *TAG_GATE_TASK = "gateTask";

struct GateCommand
{
    GateCommandType type;
    uint32_t param;
};

static QueueHandle_t gateCommandQueue = nullptr;
static Gate *gateA = nullptr;
static Gate *gateB = nullptr;

// Number of commands queued but not finished executing.
// Incremented by the sender, decremented by the gate task once the command has been fully
// carried out, so gatesAreIdle() cannot report 'done' for a movement that has not started.
static std::atomic<int> pendingCommandCount{0};

// Gate state published for the control task.
// The Gate objects themselves are only ever touched by the gate task; everything the
// control task needs is copied into these atomics at the end of each cycle. That keeps the
// cross-task boundary explicit instead of relying on plain member reads happening to be
// atomic on this MCU. The values can be up to one gate task cycle old, which is harmless -
// pendingCommandCount covers the window right after a command was sent.
static std::atomic<bool> publishedGatesAreIdle{true};
static std::atomic<bool> publishedAnyGateIsClosing{false};


//===============================
//========== Functions ==========
//===============================
static const char *gateCommandToString(GateCommandType type)
{
    switch (type)
    {
    case GateCommandType::OPEN_COMPLETELY:    return "OPEN_COMPLETELY";
    case GateCommandType::CLOSE_COMPLETELY:   return "CLOSE_COMPLETELY";
    case GateCommandType::STOP:               return "STOP";
    case GateCommandType::PAUSE:              return "PAUSE";
    case GateCommandType::CANCEL:             return "CANCEL";
    case GateCommandType::CONTINUE_CLOSING:   return "CONTINUE_CLOSING";
    case GateCommandType::SET_TARGET_RUN_TIME:return "SET_TARGET_RUN_TIME";
    default:                                  return "UNKNOWN";
    }
}


// Apply one command to one gate.
static void applyCommandToGate(Gate *gate, const GateCommand &command)
{
    switch (command.type)
    {
    case GateCommandType::OPEN_COMPLETELY:
        gate->openCompletely();
        break;

    case GateCommandType::CLOSE_COMPLETELY:
        gate->closeCompletely();
        break;

    case GateCommandType::STOP:
        gate->stop();
        break;

    case GateCommandType::PAUSE:
        gate->pause();
        break;

    case GateCommandType::CANCEL:
        gate->cancel();
        break;

    case GateCommandType::CONTINUE_CLOSING:
        // Two situations end up here:
        //  - the gate was paused mid-movement  -> resume where it left off
        //  - the gate never started, because the barrier was already obstructed when the
        //    close button was pressed          -> start the closing movement now
        if (gate->getIsIdling())
            gate->closeCompletely();
        else
            gate->resume();
        break;

    case GateCommandType::SET_TARGET_RUN_TIME:
        gate->updateTargetRunTime(command.param);
        break;
    }
}


//===============================
//========== Gate task ==========
//===============================
static void gateTask(void *param)
{
    ESP_LOGW(TAG_GATE_TASK, "gate handling task started");

    while (true)
    {
        // 1. carry out everything the control task asked for
        GateCommand command;
        while (xQueueReceive(gateCommandQueue, &command, 0) == pdTRUE)
        {
            ESP_LOGI(TAG_GATE_TASK, "executing command %s (param=%lu)",
                     gateCommandToString(command.type), command.param);
            applyCommandToGate(gateA, command);
            applyCommandToGate(gateB, command);
            // only now the command counts as done - see pendingCommandCount
            pendingCommandCount--;
        }

        // 2. step both gate state machines (this is where the blocking modbus traffic is)
        gateB->handle();
        gateA->handle();

        // 3. publish what the control task is allowed to see
        publishedGatesAreIdle.store(gateA->getIsIdling() && gateB->getIsIdling());
        publishedAnyGateIsClosing.store(gateA->getIsClosing() || gateB->getIsClosing());

        vTaskDelay(pdMS_TO_TICKS(GATE_HANDLE_INTERVAL_MS));
    }
}


//===============================
//====== Public interface =======
//===============================
void gateTaskStart(Gate *newGateA, Gate *newGateB)
{
    gateA = newGateA;
    gateB = newGateB;

    gateCommandQueue = xQueueCreate(GATE_COMMAND_QUEUE_LENGTH, sizeof(GateCommand));
    if (gateCommandQueue == nullptr)
    {
        ESP_LOGE(TAG_GATE_TASK, "failed to create gate command queue!");
        return;
    }

    xTaskCreate(&gateTask, "gateTask", GATE_TASK_STACK_SIZE, nullptr, GATE_TASK_PRIORITY, nullptr);
}


void gateSendCommand(GateCommandType type, uint32_t param)
{
    const GateCommand command = {type, param};

    // count it before queueing, so gatesAreIdle() can never see a gap between the control
    // task sending a command and the gate task picking it up
    pendingCommandCount++;
    if (xQueueSend(gateCommandQueue, &command, 0) != pdTRUE)
    {
        pendingCommandCount--;
        ESP_LOGE(TAG_GATE_TASK, "gate command queue full - dropped %s!", gateCommandToString(type));
    }
}


bool gatesAreIdle()
{
    return (pendingCommandCount.load() == 0) && publishedGatesAreIdle.load();
}


bool anyGateIsClosing()
{
    return publishedAnyGateIsClosing.load();
}


bool anyGateHadError()
{
    return gateA->getErrorLatched() || gateB->getErrorLatched();
}


void clearGateErrors()
{
    gateA->clearErrorLatch();
    gateB->clearErrorLatch();
}
