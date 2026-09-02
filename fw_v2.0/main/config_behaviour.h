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
// The gate can run at up to three frequencies. Which one is used when is decided in
// Gate::updateSpeedProfile(); this is where the values live.

//--- the reference speed: read this before changing any of the others -------------------
// This constant has FOUR jobs, which is why it is not simply "the normal speed":
//
//  1. It is the UNIT every distance in this firmware is expressed in. "The rail is 9100 ms
//     long" (main.cpp), "the pedestrian gap is 1900 ms wide" (control.cpp), "slow for the
//     last 1500 ms" below - all of them mean milliseconds AT THIS FREQUENCY, not seconds on
//     a clock. Travel is integrated as elapsed time x (current speed / this), so those
//     numbers stay correct whatever speed the gate actually runs at.
//     => Changing this value silently rescales every one of them. It is not a speed
//        setting. Change VFD_FREQUENCY_FULL_HZ if the gate should be quicker.
//     => The only legitimate reason to change it is that the gate itself changed - a new
//        motor or different gearing - and then every distance constant has to be
//        re-measured with it, and the stored calibration in NVS discarded.
//
//  2. It is the speed used when VARIABLE_SPEED_ENABLED is 0, so switching the feature off
//     gives back the firmware as it was rather than a constant full speed.
//
//  3. It is what the speed profile falls back to whenever it is unsure - today that means
//     a position that no limit switch has confirmed (after a boot with the gate parked
//     mid-rail, or after the gate was pushed by hand). The rule for this feature is that
//     when it does not know something, it behaves like the firmware that did not have it.
//
//  4. It is the anchor for the movement backstop, which has to allow for the slowest speed
//     a movement can legitimately use (Gate::getMovementTimeoutMs()).
//
// 40 Hz because that is what the gate actually ran on from V2.0 (2025-04) through V2.2,
// unchanged for the whole time the system has been in service - so the distances that were
// measured during those years are already in these units. Note the firmware never ran at
// the 50 Hz a constant here used to claim: that constant was never read.
#define VFD_FREQUENCY_REFERENCE_HZ 40

//--- the speed the gate runs at in the middle of a movement -----------------------------
// The one to change if the gate should be quicker or slower. Nothing else has to be
// adjusted for it - see job 1 above.
//
// 70 Hz is the drive's configured ceiling and therefore the most this may be. That is a
// hard limit, not a preference (doc/vfd/T13-400W-12-H_parameters_edit.pdf):
//   -1.7- Maximum frequency is set to 70 Hz (factory 50). Above that the drive simply runs
//         at 70 while the firmware believes it is running faster, so the distance
//         bookkeeping drifts - and invisibly, because the measured travel absorbs the error
//         into a consistently larger number. Raise the drive parameter first if this should
//         ever go higher, and note the new value in doc/vfd/.
//   -2.0- Corresponding frequency of the highest output voltage is 99.9 Hz (west) / 95 Hz
//         (east) - deliberately set high to run the motors on reduced voltage. The V/f
//         ratio is therefore still constant at 70 Hz, so this is NOT field weakening and
//         there is no torque loss; a drive left at the factory 50 Hz would lose torque
//         above 50 Hz instead.
#define VFD_FREQUENCY_FULL_HZ 70

//--- the speed for the gentle start and the final approach ------------------------------
// Note the drive's low-speed torque boost (-0.3- / -0.4-) only reaches up to 20 Hz, so
// 25 Hz gets plain reduced voltage. If the gate stalls or struggles at the slow speed, this
// is the first constant to raise - or extend the boost range on the drive.
#define VFD_FREQUENCY_SLOW_HZ 25

//--- the feature switch -----------------------------------------------------------------
// 1 = the gate starts gently, runs the middle at VFD_FREQUENCY_FULL_HZ and slows down again
//     for the last stretch before the limit stop - which otherwise takes the full speed on
//     every single movement and is the part of the mechanics that suffers for it. The
//     drive's own deceleration ramp (-0.2- stop time) means the gate keeps moving for a
//     moment after the limit switch is reached, so arriving slowly shortens that overtravel
//     as well.
//
// 0 = one constant speed from standstill to limit switch, and that speed is
//     VFD_FREQUENCY_REFERENCE_HZ - i.e. exactly the behaviour that ran in production for
//     years, with every distance constant meaning literal milliseconds again. This is the
//     switch to flip if the feature ever misbehaves; nothing else needs changing with it.
//
// Deliberately time-based and crude. Slowing down early only costs a moment, the
// final-approach test also holds once the gate has travelled past where the end was
// expected, and where the position is not trustworthy the profile falls back rather than
// guesses - so the estimate does not have to be good, only roughly right, and being wrong
// makes the gate gentler rather than more violent. Encoders (ROADMAP 3.2) would make it
// exact, but they are not a prerequisite. See ROADMAP 2.6.
#define VARIABLE_SPEED_ENABLED 1

//--- shape of the profile ---------------------------------------------------------------
// As DISTANCE at the reference speed - "the first 700 ms worth of travel", not "the first
// 700 ms". A movement shorter than the sum of the two would run slowly throughout; that no
// longer happens to the pedestrian gap, because the final approach is timed against the
// limit switch only and a partial opening does not end at one.
#define GATE_SLOW_START_DISTANCE_MS 700
#define GATE_SLOW_APPROACH_DISTANCE_MS 1500

#define BEEP_AT_LIMIT_SW_CHANGE

#define PAUSED_SWITCH_TO_IDLE_TIMEOUT_MS 30*1000
