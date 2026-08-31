#pragma once

#include <stdint.h>
#include <driver/gpio.h>

//=====================================================
//========= Buzzer and fault LED indication ===========
//=====================================================
// One task owns the buzzer and the fault LED. Any other task can request an indication
// with a single call, from anywhere, without holding a pointer to anything.
//
// Buzzer and LED behave differently on purpose:
//
//   BUZZER - a queue of one-shot signals. A request is played after whatever is already
//            running, so two events in quick succession stay distinguishable instead of
//            cutting each other off. This is what the old buzzer_t did and what people
//            around the gate are used to, so the timings are unchanged.
//
//   LED    - a state, not a queue, arbitrated by priority (see LedPriority). The LED is the
//            only channel that can show something *persistently*, so the most important
//            thing wins instead of the last writer winning.
//
// Hardware note: only the RED fault LED is driven from software - it is labelled "Fault"
// on the control panel, hence the name. The GREEN panel LED is wired in parallel with the
// buzzer, so it echoes the beeps without any firmware involvement - see the comment on the
// output pins in config.h.
//
// Because it is the only LED under software control, the fault LED currently carries the
// StatusIndication values as well, at a lower priority than an actual fault. If the green
// LED is ever given its own output it becomes the natural home for those - the call sites
// would not have to change, only which channel StatusIndication is rendered on.

#include "blink.hpp"

//===============================
//======= Buzzer signals ========
//===============================
// Named signals instead of bare numbers at the call sites: a caller says WHAT happened,
// the indicator decides how it sounds. The timings live in one table in indicator.cpp
// (kBuzzerSignals) and are exactly those of the previous firmware - people are used to them.
enum class BuzzerSignal : uint8_t
{
    STARTUP,                // firmware booted
    BUTTON_ACKNOWLEDGED,    // open button press registered
    ADDITIONAL_PRESS,       // one more press counted while waiting for input
    OPEN_FURTHER_CONFIRMED, // several presses accepted, opening further
    MOVEMENT_START_WARNING, // long warning tone before the gate starts moving
    MOVEMENT_STOPPED,       // movement was stopped by the user
    BARRIER_BLOCKED,        // light barrier interrupted a closing movement
    LIMIT_SWITCH_REACHED,   // a limit switch changed while the gate was off
    OBSTRUCTION_DETECTED,   // motor current limit exceeded while closing
    FAULT,                  // something went wrong (VFD communication, timeout, ...)
    AUTO_CLOSE_ARMED,       // gate will close again on its own after the hold time
    AUTO_CLOSE_CANCELLED,   // the pending automatic close was called off

    COUNT                   // keep last - number of signals, used to check the timing table
};

// Queue a named signal. Non-blocking.
void indicatorBeep(BuzzerSignal signal);

// Queue a beep with explicit timing, for signals computed at runtime
// (the accelerating countdown before the gate resumes after the barrier cleared).
void indicatorBeepCustom(uint8_t count, uint16_t msOn, uint16_t msOff);

// Continuous alarm until indicatorBuzzerStop(). Replaces whatever is playing and clears
// the queue - an alarm is not something that should wait its turn.
void indicatorBuzzerAlarm(uint16_t msOn, uint16_t msOff);
void indicatorBuzzerStop();


//===============================
//========= LED signals =========
//===============================
// Why the gate is unhappy. Latched: stays visible until indicatorClearFault(), which the
// control task calls when the user starts a new movement.
//
// The blink rate encodes how much attention it needs - the faster, the more serious:
//   VFD_COMMUNICATION  very fast  the drives cannot be talked to, gate is unusable
//   MOVEMENT_TIMEOUT   fast       a limit switch was not reached in time
//   OBSTRUCTION        slow       motor current too high, something was in the way
//   BARRIER_BLOCKED_TOO_LONG  very slow  movement was given up, nothing is broken
enum class FaultCode : uint8_t
{
    NONE = 0,
    VFD_COMMUNICATION,
    MOVEMENT_TIMEOUT,
    OBSTRUCTION,
    BARRIER_BLOCKED_TOO_LONG
};

// What the gate is currently doing. Not latched - overwritten as the situation changes.
// Whenever a movement is pending without the user having asked for it right now
// (WAITING_FOR_BARRIER, AUTO_CLOSE_PENDING) the LED flashes an asymmetric pattern that
// cannot be confused with the evenly blinking fault codes: something is queued, nothing is
// broken, and any button press calls it off.
// Shown on the fault LED for now, because that is the only LED software can drive; a green
// status LED would be the better home for these (see the hardware note at the top).
enum class StatusIndication : uint8_t
{
    IDLE = 0,             // nothing to show
    WAITING_FOR_BARRIER,  // movement paused, waiting for the light barrier to clear
    BARRIER_OBSTRUCTED,   // barrier interrupted while idle (useful when aligning it)
    AUTO_CLOSE_PENDING    // gate is open and will close again on its own
};

// What may drive the LED, lowest to highest. A higher priority always wins, so a fault can
// no longer be erased by a status update the way it used to be.
enum class LedPriority : uint8_t
{
    BUZZER_MIRROR = 0, // visual echo of the buzzer, when there is nothing better to show
    STATUS = 1,
    FAULT = 2
};

void indicatorSetFault(FaultCode code);
void indicatorClearFault();
FaultCode indicatorGetFault();

void indicatorSetStatus(StatusIndication status);


//===============================
//========== Lifecycle ==========
//===============================
struct IndicatorPinConfig
{
    gpio_num_t buzzerGpio;
    gpio_num_t faultLedGpio;
};

// Create the indicator task. Must be called once, before any of the functions above.
void indicatorStart(const IndicatorPinConfig &pins);
