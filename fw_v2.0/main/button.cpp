#include "button.hpp"

//===============================
//========== Helpers ============
//===============================
const char *buttonEventToString(ButtonEvent event)
{
    switch (event)
    {
    case ButtonEvent::PRESSED:         return "PRESSED";
    case ButtonEvent::LONG_PRESS:      return "LONG_PRESS";
    case ButtonEvent::VERY_LONG_PRESS: return "VERY_LONG_PRESS";
    case ButtonEvent::RELEASED:        return "RELEASED";
    case ButtonEvent::NONE:
    default:                      return "NONE";
    }
}


//===============================
//======== Constructor ==========
//===============================
DebouncedButton::DebouncedButton(uint32_t minStableMs, uint32_t longPressMs, uint32_t veryLongPressMs)
    : minStableMs(minStableMs), longPressMs(longPressMs), veryLongPressMs(veryLongPressMs)
{
}


//===============================
//========== update() ===========
//===============================
// One step of the debounce state machine.
//
// The press start is taken from the moment the raw input FIRST read 'pressed'
// (timestampPressStartMs), not from the moment the press was confirmed. That way the
// reported duration matches what the user actually did, and the debounce time does not
// silently shorten every measurement.
ButtonEvent DebouncedButton::update(bool rawIsPressed, uint32_t nowMs)
{
    switch (debounceState)
    {
    //--- confirmed released ---
    case DebounceState::RELEASED_STABLE:
        if (rawIsPressed)
        {
            // 1. raw input changed - start the debounce window, do not report anything yet
            debounceState = DebounceState::PRESS_PENDING;
            timestampRawChangeMs = nowMs;
            timestampPressStartMs = nowMs;
        }
        break;

    //--- raw input reads pressed, waiting for it to settle ---
    case DebounceState::PRESS_PENDING:
        if (!rawIsPressed)
        {
            // 2a. bounced back before the window elapsed -> it was never a press
            debounceState = DebounceState::RELEASED_STABLE;
        }
        else if ((nowMs - timestampRawChangeMs) >= minStableMs)
        {
            // 2b. stable long enough -> accept the press
            debounceState = DebounceState::PRESSED_STABLE;
            isPressedDebounced = true;
            longPressReported = false;
            veryLongPressReported = false;
            timestampLastChangeMs = nowMs;
            return ButtonEvent::PRESSED;
        }
        break;

    //--- confirmed pressed ---
    case DebounceState::PRESSED_STABLE:
        if (!rawIsPressed)
        {
            // 3a. raw input changed - start the debounce window, still counts as pressed
            debounceState = DebounceState::RELEASE_PENDING;
            timestampRawChangeMs = nowMs;
            timestampReleaseStartMs = nowMs;
        }
        else if (longPressMs > 0 && !longPressReported && (nowMs - timestampPressStartMs) >= longPressMs)
        {
            // 3b. held long enough -> report the long press once, while it is still held.
            //     Deciding this here (rather than letting the consumer compare a duration)
            //     is what makes long-press detection independent of how busy the consumer is.
            longPressReported = true;
            return ButtonEvent::LONG_PRESS;
        }
        else if (veryLongPressMs > 0 && !veryLongPressReported && (nowMs - timestampPressStartMs) >= veryLongPressMs)
        {
            // 3c. still held -> report the second threshold, also while still held.
            //     Both thresholds fire for the same press, so the consumer can confirm the
            //     first meaning and let the user keep holding to escalate to the second.
            veryLongPressReported = true;
            return ButtonEvent::VERY_LONG_PRESS;
        }
        break;

    //--- raw input reads released, waiting for it to settle ---
    case DebounceState::RELEASE_PENDING:
        if (rawIsPressed)
        {
            // 4a. bounced back -> the press simply continues, press start stays untouched
            debounceState = DebounceState::PRESSED_STABLE;
        }
        else if ((nowMs - timestampRawChangeMs) >= minStableMs)
        {
            // 4b. stable long enough -> accept the release.
            //     The duration is measured up to when the release was first SEEN, so the
            //     debounce window is not counted as part of the press.
            debounceState = DebounceState::RELEASED_STABLE;
            isPressedDebounced = false;
            msPressedLastCompleted = timestampReleaseStartMs - timestampPressStartMs;
            timestampLastChangeMs = nowMs;
            return ButtonEvent::RELEASED;
        }
        break;
    }

    return ButtonEvent::NONE;
}


//===============================
//====== Duration getters =======
//===============================
uint32_t DebouncedButton::getMsPressed(uint32_t nowMs) const
{
    switch (debounceState)
    {
    case DebounceState::PRESSED_STABLE:
        return nowMs - timestampPressStartMs;
    case DebounceState::RELEASE_PENDING:
        // release not confirmed yet - report the duration up to when it was first seen,
        // so the value cannot keep growing after the user already let go
        return timestampReleaseStartMs - timestampPressStartMs;
    default:
        return msPressedLastCompleted;
    }
}


uint32_t DebouncedButton::getMsReleased(uint32_t nowMs) const
{
    switch (debounceState)
    {
    case DebounceState::RELEASED_STABLE:
        return nowMs - timestampReleaseStartMs;
    case DebounceState::PRESS_PENDING:
        return timestampPressStartMs - timestampReleaseStartMs;
    default:
        return 0; // currently pressed
    }
}
