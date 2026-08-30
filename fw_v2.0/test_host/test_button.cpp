//=============================================================================
//============ Host tests for DebouncedButton (debounce + long press) =========
//=============================================================================
// Runs off-target with a simulated clock and a scripted input sequence, so the button
// behaviour can be verified without access to the real gate.
//
// Build + run:  ./run_tests.sh
//
// Background: short presses of the open button used to register as long presses
// (see ROADMAP.md, bug B1). Several tests below pin that behaviour down.

#include "button.hpp"

#include <cstdio>
#include <string>
#include <vector>

//===============================
//======= Test framework ========
//===============================
static int testsRun = 0;
static int checksFailed = 0;

static void check(bool condition, const std::string &what)
{
    if (!condition)
    {
        checksFailed++;
        printf("  FAIL: %s\n", what.c_str());
    }
}

#define CHECK(cond) check((cond), std::string(#cond) + "   (line " + std::to_string(__LINE__) + ")")

static void beginTest(const char *name)
{
    testsRun++;
    printf("[test] %s\n", name);
}

//===============================
//===== Simulated hardware ======
//===============================
// Interval the input task samples at
static const uint32_t kSampleIntervalMs = 5;
// Long press threshold used by the open button
static const uint32_t kLongPressMs = 800;
// Debounce time
static const uint32_t kMinStableMs = 40;

// Drives the button for durationMs with a fixed raw level, collecting every event.
static void feed(DebouncedButton &button, bool rawIsPressed, uint32_t durationMs,
                 uint32_t &nowMs, std::vector<ButtonEvent> &eventsOut)
{
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += kSampleIntervalMs)
    {
        nowMs += kSampleIntervalMs;
        ButtonEvent event = button.update(rawIsPressed, nowMs);
        if (event != ButtonEvent::NONE)
            eventsOut.push_back(event);
    }
}

static int countEvents(const std::vector<ButtonEvent> &events, ButtonEvent wanted)
{
    int count = 0;
    for (ButtonEvent event : events)
        if (event == wanted)
            count++;
    return count;
}

//===============================
//=========== Tests =============
//===============================

// 1. A press shorter than the debounce time must not be reported at all.
static void test_pressShorterThanDebounceIsIgnored()
{
    beginTest("press shorter than the debounce time is ignored");
    uint32_t now = 1000; // start at a non-zero time to catch uninitialised timestamps
    DebouncedButton button(kMinStableMs, kLongPressMs);
    std::vector<ButtonEvent> events;

    feed(button, true, 20, now, events); // shorter than minStableMs
    feed(button, false, 200, now, events);

    CHECK(events.empty());
    CHECK(button.getIsPressed() == false);
}

// 2. A normal press produces exactly one PRESSED and one RELEASED, and no LONG_PRESS.
static void test_shortPressProducesPressAndRelease()
{
    beginTest("short press produces exactly one PRESSED and one RELEASED");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);
    std::vector<ButtonEvent> events;

    feed(button, true, 200, now, events);
    feed(button, false, 300, now, events);

    CHECK(countEvents(events, ButtonEvent::PRESSED) == 1);
    CHECK(countEvents(events, ButtonEvent::RELEASED) == 1);
    CHECK(countEvents(events, ButtonEvent::LONG_PRESS) == 0);
    CHECK(button.getIsPressed() == false);
}

// 3. REGRESSION (B1): a short press must never be reported as a long press, not even
//    directly after a long press. This is the exact failure the user hit.
static void test_shortPressAfterLongPressIsNotLong()
{
    beginTest("short press after a long press is not reported as long press (B1)");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);

    // a) a deliberate long press
    std::vector<ButtonEvent> longPressEvents;
    feed(button, true, 1500, now, longPressEvents);
    feed(button, false, 300, now, longPressEvents);
    CHECK(countEvents(longPressEvents, ButtonEvent::LONG_PRESS) == 1);

    // b) several short presses right after - none of them may report a long press
    for (int attempt = 0; attempt < 5; attempt++)
    {
        std::vector<ButtonEvent> shortPressEvents;
        feed(button, true, 150, now, shortPressEvents);
        feed(button, false, 300, now, shortPressEvents);
        CHECK(countEvents(shortPressEvents, ButtonEvent::LONG_PRESS) == 0);
        CHECK(countEvents(shortPressEvents, ButtonEvent::PRESSED) == 1);
    }
}

// 4. REGRESSION (B1/B2): a consumer that is blocked for a long time (blocking modbus
//    transaction in the control loop) must not see a long press that never happened.
//    The events are produced by the sampler, so a slow consumer cannot misread them.
static void test_slowConsumerCannotInventLongPress()
{
    beginTest("a blocked consumer cannot turn a short press into a long press (B1/B2)");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);
    std::vector<ButtonEvent> events;

    feed(button, true, 1200, now, events); // previous long press
    feed(button, false, 300, now, events);
    events.clear();

    // short press, then the consumer stalls for 900 ms while the button is already released.
    feed(button, true, 100, now, events);
    feed(button, false, 900, now, events);

    CHECK(countEvents(events, ButtonEvent::LONG_PRESS) == 0);
    CHECK(button.getMsPressed(now) < kLongPressMs);
}

// 5. A genuinely held button must still produce a long press - exactly once, while held.
static void test_realLongPressIsReportedOnceWhileHeld()
{
    beginTest("a genuinely held button reports LONG_PRESS once, while still held");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);
    std::vector<ButtonEvent> events;

    feed(button, true, 3000, now, events);
    CHECK(countEvents(events, ButtonEvent::LONG_PRESS) == 1);
    CHECK(countEvents(events, ButtonEvent::RELEASED) == 0); // reported before the release
    CHECK(button.getIsPressed() == true);

    feed(button, false, 300, now, events);
    CHECK(countEvents(events, ButtonEvent::RELEASED) == 1);
}

// 6. The long press must be reported close to the configured threshold, not later.
static void test_longPressTimingIsAccurate()
{
    beginTest("LONG_PRESS is reported close to the configured threshold");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);

    uint32_t timestampPressStart = 0;
    uint32_t timestampLongPress = 0;
    for (int i = 0; i < 400; i++)
    {
        now += kSampleIntervalMs;
        ButtonEvent event = button.update(true, now);
        if (event == ButtonEvent::PRESSED) timestampPressStart = now;
        if (event == ButtonEvent::LONG_PRESS) { timestampLongPress = now; break; }
    }
    CHECK(timestampLongPress != 0);
    // measured from when the press was CONFIRMED, so the debounce time is included in the
    // threshold; allow one sample interval of jitter on top
    const uint32_t delayFromConfirmToLongPress = timestampLongPress - timestampPressStart;
    CHECK(delayFromConfirmToLongPress <= kLongPressMs);
    CHECK(delayFromConfirmToLongPress >= kLongPressMs - kMinStableMs - kSampleIntervalMs);
}

// 7. Contact bounce on press and on release must not produce extra events.
static void test_bounceDoesNotProduceExtraEvents()
{
    beginTest("contact bounce does not produce extra events");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);
    std::vector<ButtonEvent> events;

    // bouncing press: 5 ms pressed / 5 ms released, four times
    for (int b = 0; b < 4; b++)
    {
        feed(button, true, 5, now, events);
        feed(button, false, 5, now, events);
    }
    feed(button, true, 300, now, events); // settled, pressed

    // bouncing release
    for (int b = 0; b < 4; b++)
    {
        feed(button, false, 5, now, events);
        feed(button, true, 5, now, events);
    }
    feed(button, false, 300, now, events); // settled, released

    CHECK(countEvents(events, ButtonEvent::PRESSED) == 1);
    CHECK(countEvents(events, ButtonEvent::RELEASED) == 1);
    CHECK(countEvents(events, ButtonEvent::LONG_PRESS) == 0);
    CHECK(button.getIsPressed() == false);
}

// 8. A bouncing release must not stretch the measured press into long-press territory.
static void test_bouncingReleaseDoesNotStretchPressDuration()
{
    beginTest("bouncing release does not stretch the press into a long press");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);
    std::vector<ButtonEvent> events;

    feed(button, true, 100, now, events); // short clean press
    events.clear();

    // release that chatters for ~700 ms, every bounce shorter than minStableMs.
    // The contact really is closing again each time, so this legitimately counts as held -
    // what must not happen is a LONG_PRESS from a press the user experienced as short.
    for (int b = 0; b < 35; b++)
    {
        feed(button, false, 10, now, events);
        feed(button, true, 10, now, events);
    }
    feed(button, false, 300, now, events);

    // the chatter itself must not have produced additional press/release pairs
    CHECK(countEvents(events, ButtonEvent::PRESSED) == 0);
    CHECK(countEvents(events, ButtonEvent::RELEASED) == 1);
}

// 9. The measured press duration must match the real one, independent of the debounce time.
static void test_measuredDurationMatchesRealPress()
{
    beginTest("measured press duration matches the real press duration");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, kLongPressMs);
    std::vector<ButtonEvent> events;

    feed(button, true, 500, now, events);
    feed(button, false, 200, now, events);

    const uint32_t measured = button.getMsPressed(now);
    CHECK(measured >= 500 - kSampleIntervalMs);
    CHECK(measured <= 500 + kSampleIntervalMs);
}

// 10. Long press disabled (longPressMs = 0) must never report one - used for the
//     close button and the remote inputs.
static void test_longPressCanBeDisabled()
{
    beginTest("longPressMs = 0 disables long press reporting");
    uint32_t now = 1000;
    DebouncedButton button(kMinStableMs, 0);
    std::vector<ButtonEvent> events;

    feed(button, true, 5000, now, events);
    feed(button, false, 300, now, events);

    CHECK(countEvents(events, ButtonEvent::LONG_PRESS) == 0);
    CHECK(countEvents(events, ButtonEvent::PRESSED) == 1);
    CHECK(countEvents(events, ButtonEvent::RELEASED) == 1);
}

//===============================
//============ main =============
//===============================
int main()
{
    printf("=== DebouncedButton host tests ===\n");

    test_pressShorterThanDebounceIsIgnored();
    test_shortPressProducesPressAndRelease();
    test_shortPressAfterLongPressIsNotLong();
    test_slowConsumerCannotInventLongPress();
    test_realLongPressIsReportedOnceWhileHeld();
    test_longPressTimingIsAccurate();
    test_bounceDoesNotProduceExtraEvents();
    test_bouncingReleaseDoesNotStretchPressDuration();
    test_measuredDurationMatchesRealPress();
    test_longPressCanBeDisabled();

    printf("==================================\n");
    if (checksFailed == 0)
    {
        printf("OK - %d tests passed\n", testsRun);
        return 0;
    }
    printf("FAILED - %d check(s) failed in %d tests\n", checksFailed, testsRun);
    return 1;
}
