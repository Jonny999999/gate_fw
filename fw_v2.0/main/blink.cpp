#include "blink.hpp"

//===============================
//========= setPattern ==========
//===============================
void BlinkChannel::setPattern(const BlinkPattern &newPattern, uint32_t nowMs)
{
    pattern = newPattern;
    repeatsDone = 0;
    finished = (newPattern.mode != BlinkMode::BLINKING);
    phaseIsOn = (newPattern.mode != BlinkMode::OFF);
    timestampPhaseStartMs = nowMs;
    outputIsOn = phaseIsOn;
}


//===============================
//=========== update ============
//===============================
void BlinkChannel::update(uint32_t nowMs)
{
    if (finished || pattern.mode != BlinkMode::BLINKING)
        return;

    // 1. wait until the current phase has run its course
    const uint32_t phaseDurationMs = phaseIsOn ? pattern.msOn : pattern.msOff;
    if ((nowMs - timestampPhaseStartMs) < phaseDurationMs)
        return;

    timestampPhaseStartMs = nowMs;

    // 2. end of the on phase -> the output ALWAYS goes dark here.
    //    msOff == 0 only means there is no pause before the next repetition; it must never
    //    leave the output stuck on, which is exactly what a single beep {msOn, 0, 1} would
    //    otherwise do - and every single-beep signal is written that way.
    if (phaseIsOn)
    {
        phaseIsOn = false;
        outputIsOn = false;
        if (pattern.msOff > 0)
            return; // wait out the pause before deciding about the next repetition
        // no pause -> fall through and finish this repetition right away
    }

    // 3. one full on/off cycle done - repeat or stop
    repeatsDone++;
    if (pattern.repeatCount != INDICATOR_REPEAT_FOREVER && repeatsDone >= pattern.repeatCount)
    {
        finished = true;
        outputIsOn = false;
        return;
    }
    phaseIsOn = true;
    outputIsOn = true;
}
