#pragma once

#include <stdint.h>

//=====================================================
//========== Blink / beep pattern generation ==========
//=====================================================
// Turns a BlinkPattern into a plain on/off state over time.
//
// Pure logic: this deliberately touches no GPIO and reads no clock. The caller feeds it the
// current time and applies getOutputIsOn() to whatever output it drives, which makes the
// whole thing verifiable off-target (see test_host/test_blink.cpp).

enum class BlinkMode : uint8_t
{
    OFF,     // output permanently off
    ON,      // output permanently on
    BLINKING // msOn / msOff, repeated repeatCount times
};

// 0 repetitions means: keep going until the pattern is replaced
#define INDICATOR_REPEAT_FOREVER 0

struct BlinkPattern
{
    BlinkMode mode;
    uint16_t msOn;
    uint16_t msOff;
    // note: msOff == 0 means there is no PAUSE between repetitions - it does NOT mean the
    // output stays on. A single beep of {msOn, 0, 1} is a beep of msOn and then silence.
    uint16_t repeatCount;
};


class BlinkChannel
{
public:
    // Start a new pattern. Restarts even if it is the same one.
    void setPattern(const BlinkPattern &newPattern, uint32_t nowMs);

    // Advance the pattern. Call at a steady interval.
    void update(uint32_t nowMs);

    // True until a pattern with a finite repeatCount has played out.
    bool isBusy() const { return !finished; }

    // What the output should be right now.
    bool getOutputIsOn() const { return outputIsOn; }

private:
    BlinkPattern pattern = {BlinkMode::OFF, 0, 0, 0};
    uint16_t repeatsDone = 0;
    bool phaseIsOn = false;
    bool finished = true;
    bool outputIsOn = false;
    uint32_t timestampPhaseStartMs = 0;
};
