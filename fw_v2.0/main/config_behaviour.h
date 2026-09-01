#pragma once

// Behaviour tuning for the gate: speeds, timeouts, thresholds and the debug switches that
// go with them.
//
// Companion to config.h, which stays what its own header says it is - the GPIO / pin
// mapping and the UART settings. The two answer different questions: config.h says how the
// board is WIRED, this file says how the gate BEHAVES, and only the second one is something
// to try a different value for while standing at the gate.
// (This is the start of the split listed as an open item in ROADMAP 1.2. Only what used to
//  sit at the top of gate.hpp has moved so far; the control-task timings - button windows,
//  light barrier, automatic closing - are still at the top of control.cpp and are the
//  remaining half of that item.)
//
// Macros only, so this is safe to include from C and C++ and from inside extern "C".

// Define a delay (in milliseconds) waited for the VFD startup after the relay is turned on.
#define DELAY_VFD_STARTUP 870
    // 900 worked well, but feels long...
    // 800 west gate unreliable sometimes it does not start...
    // 700 too short, west gate did not start properly
    // it seems VFD recognizes command, but needs some time to finish booting and only starts later anyways
// TODO: test individual delay for each VFD

// #define IGNORE_VFD_ERROR // if defined does not force realy off when stop command fails
// How long the VFD supply relay stays on after the last movement.
// 1 h was too short (the drives were often off during an active day, costing the ~870 ms
// startup delay every time). 3 h was better, 4 h is comfortable - keeping the drives
// powered is no longer a concern with PV excess.
// note: written as UL and multiplied out in 64 bit at the point of use, see handle().
//       (4 h in microseconds does not fit in 32 bits, which is what the 3 h setting silently
//        ran into: it wrapped and expired after ~37 minutes instead.)
#define RELAY_INACTIVITY_TIMEOUT_MS ((4UL*60 + 0)*60*1000UL)

// Log the motor current (with the speed it was measured at) on every check while closing.
// Enabled on this branch on purpose: the obstruction threshold below was measured at a
// single speed, and what the motor draws at SLOW_VFD_FREQUENCY is exactly the number this
// experiment still owes. See kCurrentLimitSlowAmpere.
#define LOG_VFD_CURRENT_WHEN_CLOSING
#define CURRENT_MONITORING_ENABLED
#define DEFAULT_VFD_CURRENT_LIMIT 0.60

//===============================
//======= Gate speed ============
//===============================
// Motor speed in Hz. 40 Hz is what the gate has always actually run at - the constant used
// to say 50 and was simply never read (see the note on kSpeedFullHz), while the movement
// timings in main.cpp were measured against the 40 Hz the drive was really given.
#define DEFAULT_VFD_FREQUENCY 40

// Variable gate speed - PROOF OF CONCEPT, see ROADMAP 2.6.
// Set to 0 to get exactly the previous behaviour back: one constant speed from standstill
// to limit switch.
//
// The gate starts gently, runs at full speed in the middle, and slows down again for the
// last stretch before the limit stop - which today takes the full 40 Hz on every single
// movement and is the part of the mechanics that suffers for it.
//
// Deliberately time-based and crude. The two slow stretches are only a second or so of
// travel each, and starting one too early merely costs a moment while starting one too
// late costs nothing that is not already the case today - so the position estimate does
// not have to be good, it only has to be roughly right. Encoders (ROADMAP 3.2) would make
// it exact, but they are not a prerequisite for finding out whether the feature is worth
// having at all.
#define VARIABLE_SPEED_ENABLED 1
#define SLOW_VFD_FREQUENCY 25 // speed for the gentle start and the final approach

#define BEEP_AT_LIMIT_SW_CHANGE

#define PAUSED_SWITCH_TO_IDLE_TIMEOUT_MS 30*1000
