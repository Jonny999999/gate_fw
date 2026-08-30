#pragma once

#include <stdint.h>

//=========================================================
//========== Debounced button / switch evaluation =========
//=========================================================
// Pure logic: this class never touches a GPIO and never reads a clock itself. The caller
// feeds it raw samples together with the current time, which makes the whole behaviour
// verifiable off-target (see test_host/).
//
// Replaces the old gpio_evaluatedSwitch, which exposed sticky flags (state / risingEdge /
// msPressed) that the consumer had to poll at exactly the right moment. If the consumer
// was late - and the control loop regularly was, because of blocking modbus transactions -
// edges were missed and durations were read from the previous press. See ROADMAP.md B1/B2.
//
// This class instead reports each state change exactly once, as a return value, so a
// delayed caller cannot lose or misread an event.

enum class ButtonEvent
{
    NONE = 0,
    PRESSED,         // press confirmed (raw input stable for minStableMs)
    LONG_PRESS,      // still held longPressMs after the press started - once per press
    VERY_LONG_PRESS, // still held veryLongPressMs after the press started - once per press
    RELEASED         // release confirmed (raw input stable for minStableMs)
};

const char *buttonEventToString(ButtonEvent event);

class DebouncedButton
{
public:
    // minStableMs:      how long the raw input must stay unchanged before a change is accepted
    // longPressMs:      hold time after which LONG_PRESS is reported (0 disables it)
    // veryLongPressMs:  hold time after which VERY_LONG_PRESS is reported (0 disables it).
    //                   Both are reported for the same press, in order, so the user can be
    //                   told what will happen and keep holding to escalate.
    explicit DebouncedButton(uint32_t minStableMs = 40, uint32_t longPressMs = 0,
                             uint32_t veryLongPressMs = 0);

    // Feed one raw sample. Returns the event caused by this sample, or NONE.
    // Call this at a steady interval; the shorter and more regular, the more precise the
    // reported durations are.
    ButtonEvent update(bool rawIsPressed, uint32_t nowMs);

    //--- current state ---
    bool getIsPressed() const { return isPressedDebounced; }

    // How long the button is currently held (or was held during the last completed press).
    uint32_t getMsPressed(uint32_t nowMs) const;

    // How long the button has been released (or was released before the current press).
    uint32_t getMsReleased(uint32_t nowMs) const;

    // Time of the last confirmed change, for callers that need to time out on inactivity.
    uint32_t getTimestampLastChangeMs() const { return timestampLastChangeMs; }

private:
    enum class DebounceState
    {
        RELEASED_STABLE, // confirmed released
        PRESS_PENDING,   // raw input reads pressed, waiting for it to stay that way
        PRESSED_STABLE,  // confirmed pressed
        RELEASE_PENDING  // raw input reads released, waiting for it to stay that way
    };

    const uint32_t minStableMs;
    const uint32_t longPressMs;
    const uint32_t veryLongPressMs;

    DebounceState debounceState = DebounceState::RELEASED_STABLE;
    bool isPressedDebounced = false;
    bool longPressReported = false;
    bool veryLongPressReported = false;

    uint32_t timestampRawChangeMs = 0;    // when the raw input last differed from the stable state
    uint32_t timestampPressStartMs = 0;   // when the press that is now active first appeared
    uint32_t timestampReleaseStartMs = 0; // when the release that is now active first appeared
    uint32_t timestampLastChangeMs = 0;   // when the last press/release was confirmed
    uint32_t msPressedLastCompleted = 0;  // duration of the last finished press
};
