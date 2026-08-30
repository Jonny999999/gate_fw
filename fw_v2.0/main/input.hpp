#pragma once

#include <stdint.h>
#include <driver/gpio.h>

//=====================================================
//============ User input (buttons, remote) ===========
//=====================================================
// All user inputs are sampled by a dedicated high-priority task at a fixed interval,
// completely independent of what the control task is doing.
//
// Why: the control task blocks for tens to hundreds of milliseconds on every modbus
// transaction (VFD start / stop / current read). While it was also responsible for
// sampling the buttons, the debounce and long-press timing depended on how busy it
// happened to be - which is what made short presses register as long presses
// (see ROADMAP.md B1/B2).
//
// Press events are queued, so a control task that is blocked for a while cannot lose
// them. Levels are published as a snapshot.

// Pin assignment, passed once at startup
struct InputPinConfig
{
    gpio_num_t openButtonGpio;
    gpio_num_t closeButtonGpio;
    gpio_num_t remoteOpenGpio;
    gpio_num_t remoteCloseGpio;
    gpio_num_t lightBarrierGpio;
};

// What happened since the previous inputPoll() call, plus the current levels.
struct InputState
{
    //--- events (each press is reported exactly once, none are lost) ---
    bool openButtonPressed;    // open button press confirmed
    bool openButtonLongPress;  // open button held past the long-press threshold
    bool openButtonVeryLongPress; // ... and kept held past the second, longer threshold
    bool closeButtonPressed;   // close button press confirmed
    bool remoteOpenPressed;    // remote 'A' button
    bool remoteClosePressed;   // remote 'B' button
    bool anyButtonPressed;     // any of the four press events above (used to stop movement)

    //--- current levels ---
    bool openButtonIsHeld;          // open button currently held down
    bool closeButtonIsHeld;         // close button currently held down
    bool lightBarrierIsObstructed;  // light barrier currently interrupted
};

// Create the input sampling task. Must be called once before inputPoll().
void inputStart(const InputPinConfig &pins);

// Collect everything that happened since the previous call.
// Non-blocking, intended to be called once per control loop iteration.
InputState inputPoll();
