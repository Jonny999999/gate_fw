#pragma once

#include "gate.hpp"

#include <stdint.h>

//=====================================================
//======== Gate handling task (owns the RS485 bus) ====
//=====================================================
// Both Gate objects are driven from one dedicated task instead of from the control loop.
//
// Why: every Gate operation that talks to a VFD (start, stop, set frequency, read current)
// is a blocking modbus transaction of 10-150 ms, up to ~600 ms with retries. While gate
// handling ran inside the control loop, the whole user interface stalled for that long -
// most visibly during closing, where the current is polled on every iteration.
// (ROADMAP.md B2)
//
// A side effect is that the RS485 bus now has exactly one owner, so it needs no mutex
// (ROADMAP.md B12).
//
// The control task talks to the gates only through the small command / query interface
// below, and never touches a Gate object directly.

enum class GateCommandType
{
    OPEN_COMPLETELY,      // open both gates until the limit switch
    CLOSE_COMPLETELY,     // close both gates until the limit switch
    STOP,                 // stop where they are
    PAUSE,                // stop but remember direction and remaining run time
    CANCEL,               // give up a paused/pending movement
    CONTINUE_CLOSING,     // resume a paused gate, or start closing one that never started
    SET_TARGET_RUN_TIME   // change how long the current movement should last (param = ms)
};

// Create the gate handling task. Must be called once before any command is sent.
// The Gate objects have to outlive the task.
void gateTaskStart(Gate *gateA, Gate *gateB);

// Queue a command for BOTH gates. Non-blocking - returns as soon as the command is queued.
void gateSendCommand(GateCommandType type, uint32_t param = 0);

//--- status queries, all non-blocking and safe to call from the control task ---

// True when both gates are idle AND no command is still queued or being executed.
// The pending-command part matters: without it the control task could ask right after
// sending a command, see the gates still idle, and conclude the movement had finished.
bool gatesAreIdle();

// True once the gate task stopped a closing movement because the light barrier was
// interrupted. The stop itself already happened - this only tells the control task to take
// over with the countdown / resume / give-up handling.
// Cleared by CONTINUE_CLOSING and CANCEL.
bool gatesArePausedByLightBarrier();

// True while at least one gate is running in the closing direction
// (including while the VFD is still booting for a closing movement).
bool anyGateIsClosing();

// True when the last movement command could not be carried out because a gate already
// reports that end position (e.g. close requested while the closed limit switch is active).
// Not an error - it lets the control task acknowledge that nothing will happen rather than
// signalling a start and then standing still.
bool anyGateRefusedMovement();

// True if a gate reported an error since the last clearGateErrors(). Latched, because
// ERROR_STATE itself only lasts one handle() cycle.
bool anyGateHadError();
void clearGateErrors();
