#include "gate_task.hpp"

#include "input.hpp"

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
// Set when the gate task stopped a closing movement because of the light barrier.
// The control task takes it from here (countdown, resume, give up), but the stop itself
// does not depend on the control task being in any particular state.
static std::atomic<bool> publishedPausedByLightBarrier{false};
// Set when a movement command could not be carried out because a gate already reports that
// end position - see Gate::getMovementWasRefused().
static std::atomic<bool> publishedMovementWasRefused{false};


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
    case GateCommandType::CONTINUE_CLOSING:
        // the control task decided the way is clear again
        publishedPausedByLightBarrier.store(false);   return "CONTINUE_CLOSING";
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
        // Re-check the barrier here as well, not just when the command was sent. The two
        // are a control-loop apart, and this is the last point before the motor is asked to
        // turn. Cheap, and it removes the one cycle in which an obstruction that appeared in
        // between would have been acted on only afterwards.
        if (inputLightBarrierIsObstructed())
        {
            ESP_LOGW(TAG_GATE_TASK, "Close command ignored - the light barrier is interrupted");
            publishedPausedByLightBarrier.store(true);
            break;
        }
        gate->closeCompletely();
        break;

    case GateCommandType::STOP:
        gate->stop();
        break;

    case GateCommandType::PAUSE:
        gate->pause();
        break;

    case GateCommandType::CANCEL:
        publishedPausedByLightBarrier.store(false);
        gate->cancel();
        break;

    case GateCommandType::CONTINUE_CLOSING:
        // the control task decided the way is clear again
        publishedPausedByLightBarrier.store(false);
        // Two situations end up here:
        //  - the gate was paused mid-movement  -> resume where it left off
        //  - the gate never started, because the barrier was already obstructed when the
        //    close button was pressed          -> start the closing movement now
        if (inputLightBarrierIsObstructed())
        {
            ESP_LOGW(TAG_GATE_TASK, "Resume ignored - the light barrier is interrupted again");
            publishedPausedByLightBarrier.store(true);
            break;
        }
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


// Publish everything the control task is allowed to see about the gates.
// Must be called whenever the gate state may have changed - in particular BEFORE
// pendingCommandCount is decremented, see the note in gateTask().
static void publishGateState()
{
    publishedGatesAreIdle.store(gateA->getIsIdling() && gateB->getIsIdling());
    publishedAnyGateIsClosing.store(gateA->getIsClosing() || gateB->getIsClosing());
    publishedMovementWasRefused.store(gateA->getMovementWasRefused() || gateB->getMovementWasRefused());
}


//=====================================================
//========= Light barrier safety (layer 1) ============
//=====================================================
// A closing gate must stop as soon as the light barrier is interrupted. That decision is
// made HERE, in the task that owns the motors, and not in the control state machine:
//
//  - it runs every cycle regardless of what the control task believes is going on. A bug in
//    the control state machine can no longer disable the safety stop, which is exactly what
//    happened once (control left MOVING_TO_TARGET early and stopped checking the barrier).
//  - it reads the barrier level directly from the input task, so it does not depend on any
//    event being delivered or consumed.
//
// The control task still decides what happens NEXT - warn, wait, resume or give up - but it
// is no longer what stands between an interrupted barrier and the motor stopping.
static void handleLightBarrierSafety()
{
    if (!inputLightBarrierIsObstructed())
        return;

    // note: getIsClosing() is also true while the VFD is still booting for a closing
    // movement, so the gate is stopped before the motor ever starts turning
    bool pausedAnyGate = false;
    if (gateA->getIsClosing())
    {
        gateA->pause();
        pausedAnyGate = true;
    }
    if (gateB->getIsClosing())
    {
        gateB->pause();
        pausedAnyGate = true;
    }

    if (pausedAnyGate)
    {
        ESP_LOGE(TAG_GATE_TASK, "LIGHT BARRIER interrupted while closing => movement stopped");
        publishedPausedByLightBarrier.store(true);
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
        // 1. safety first: stop a closing gate if the light barrier is interrupted.
        //    Before the commands, so an obstruction can never be overtaken by a close
        //    request that arrived in the same cycle.
        handleLightBarrierSafety();

        // 2. carry out everything the control task asked for
        GateCommand command;
        while (xQueueReceive(gateCommandQueue, &command, 0) == pdTRUE)
        {
            ESP_LOGI(TAG_GATE_TASK, "executing command %s (param=%lu)",
                     gateCommandToString(command.type), command.param);
            applyCommandToGate(gateA, command);
            applyCommandToGate(gateB, command);

            // Publish BEFORE the command stops counting as pending.
            // The other order is a race: pendingCommandCount would already be 0 while the
            // published state still described the situation before the command, and the
            // control task would conclude the movement had finished. handle() below blocks
            // on modbus for tens of milliseconds, which made that window easy to hit.
            publishGateState();
            pendingCommandCount--;
        }

        // 3. step both gate state machines (this is where the blocking modbus traffic is)
        gateB->handle();
        gateA->handle();

        // 4. publish what the control task is allowed to see
        publishGateState();

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


bool gatesArePausedByLightBarrier()
{
    return publishedPausedByLightBarrier.load();
}


bool anyGateRefusedMovement()
{
    return publishedMovementWasRefused.load();
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
