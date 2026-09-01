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
// Enabled on this branch on purpose: the thresholds below were measured at 40 Hz, and what
// the motor draws at the other two speeds is exactly the number this experiment still owes.
#define LOG_VFD_CURRENT_WHEN_CLOSING
#define CURRENT_MONITORING_ENABLED

// Motor current above which a closing gate is treated as obstructed.
// One value per speed, because a V/f drive does not draw the same current for the same load
// at a different frequency. Both still hold the threshold measured at 40 Hz - guessing the
// others would either weaken the detection exactly where the gate is nearly closed, or
// produce nuisance trips. Read them off the log lines above and set them from real numbers.
#define VFD_CURRENT_LIMIT_AMPERE 0.60      // at VFD_FREQUENCY_FULL_HZ
#define VFD_CURRENT_LIMIT_SLOW_AMPERE 0.60 // at VFD_FREQUENCY_SLOW_HZ

//===============================
//======= Gate speed ============
//===============================
// Reference speed: the frequency every DISTANCE constant in this firmware is expressed in.
// "The rail is 10000 ms long", "the gap is 1900 ms wide" - both mean at 40 Hz.
//
// 40 Hz is also what actually ran the gate in production from V2.0 (2025-04) through
// V2.2 - unchanged for the whole time the system has been in service. Note the firmware
// never ran at the 50 Hz a constant here used to claim: that constant was never read.
//
// Changing the speed the gate runs at must NOT change this one. It is the unit those
// measurements are in, not a speed setting - keeping it fixed is what lets the full speed
// below be changed without re-measuring the rail, the pedestrian gap and everything else.
// Only re-measuring the gate itself (a new motor, a new gearing) is a reason to touch it,
// and then every distance constant has to be re-measured with it.
#define VFD_FREQUENCY_REFERENCE_HZ 40

// Speed the gate actually runs at in the middle of a movement.
// 60 Hz to make the difference to the slow phases obvious during the experiment; 40 Hz is
// the value with years of service behind it.
//
// Safe on these drives, but only because of how they are parameterised - check both before
// raising it further (doc/vfd/T13-400W-12-H_parameters_edit.pdf):
//   -1.7- Maximum frequency is set to 70 Hz (factory 50). Anything above that is silently
//         clamped by the drive, which would also make the distance bookkeeping wrong - it
//         assumes the drive runs at what it was asked for.
//   -2.0- Corresponding frequency of the highest output voltage is 99.9 Hz (west) / 95 Hz
//         (east) - deliberately set high to run the motors on reduced voltage. The V/f
//         ratio is therefore still constant at 60 Hz, so this is NOT field weakening and
//         there is no torque loss; a drive left at the factory 50 Hz would lose torque
//         above 50 Hz instead.
#define VFD_FREQUENCY_FULL_HZ 75

// Speed for the gentle start and the final approach.
// Note the drive's low-speed torque boost (-0.3- / -0.4-) only reaches up to 20 Hz, so
// 25 Hz gets plain reduced voltage. If the gate stalls or struggles at the slow speed, this
// is the first constant to raise - or extend the boost range on the drive.
#define VFD_FREQUENCY_SLOW_HZ 25

// Variable gate speed - PROOF OF CONCEPT, see ROADMAP 2.6.
// Set to 0 for one constant speed from standstill to limit switch, as before.
//
// The gate starts gently, runs at full speed in the middle, and slows down again for the
// last stretch before the limit stop - which today takes the full speed on every single
// movement and is the part of the mechanics that suffers for it. The drive's own
// deceleration ramp (-0.2- stop time) means the gate keeps moving for a moment after the
// limit switch is reached, so arriving slowly shortens that overtravel as well.
//
// Deliberately time-based and crude. Starting a slow phase too early merely costs a moment,
// and the final-approach test also holds once the gate has travelled PAST where the end was
// expected - so the position estimate does not have to be good, only roughly right, and
// being wrong makes the gate gentler rather than more violent. Encoders (ROADMAP 3.2) would
// make it exact, but they are not a prerequisite for finding out whether this is worth
// having at all.
#define VARIABLE_SPEED_ENABLED 1

// Shape of the profile, as DISTANCE at the reference speed - "the first 700 ms worth of
// travel", not "the first 700 ms".
// A movement shorter than the sum of the two runs slowly throughout, which is what happens
// to the pedestrian gap (1900 ms of travel): it ends up exactly as wide as before and just
// opens more gently. Lower these if the gap should benefit from the higher full speed too.
#define GATE_SLOW_START_DISTANCE_MS 700
#define GATE_SLOW_APPROACH_DISTANCE_MS 1500

#define BEEP_AT_LIMIT_SW_CHANGE

#define PAUSED_SWITCH_TO_IDLE_TIMEOUT_MS 30*1000
