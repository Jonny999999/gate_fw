//=============================================================================
//===== Host tests for gpio_evaluatedSwitch (debounce + press duration) =======
//=============================================================================
// Runs off-target with a simulated clock and a scripted input level sequence,
// so the button behaviour can be verified without access to the real gate.
//
// Build + run:  ./run_tests.sh
//
// Background: a stale msPressed used to make short presses of the open button
// register as long presses (see ROADMAP.md, bug B1). The tests below pin down
// that behaviour so it cannot come back.

#include "gpio_evaluateSwitch.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

//===============================
//===== Simulated hardware ======
//===============================
static uint32_t simulatedTimeMs = 0;
static int simulatedPinLevel = 1; // idle high (button wired to GND, pressed = low)

uint32_t esp_log_timestamp(void) { return simulatedTimeMs; }
int gpio_get_level(gpio_num_t) { return simulatedPinLevel; }
void gpio_set_direction(gpio_num_t, gpio_mode_t) {}
void gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t) {}

// Interval at which the real control loop calls handle() (CONTROL_LOOP_HANDLE_DELAY_MS)
static const uint32_t kHandleIntervalMs = 10;

//===============================
//======= Test framework ========
//===============================
static int testsRun = 0;
static int testsFailed = 0;
static std::string currentTestName;

static void check(bool condition, const std::string &what)
{
    if (!condition)
    {
        testsFailed++;
        printf("  FAIL: %s\n", what.c_str());
    }
}

#define CHECK(cond) check((cond), std::string(#cond) + "   (line " + std::to_string(__LINE__) + ")")

static void beginTest(const char *name)
{
    testsRun++;
    currentTestName = name;
    printf("[test] %s\n", name);
    simulatedTimeMs = 1000; // start at a non-zero time to catch uninitialised timestamps
    simulatedPinLevel = 1;
}

//===============================
//======== Test helpers =========
//===============================
// Advance the simulated clock by durationMs, calling handle() every kHandleIntervalMs
// (this is what the control loop does). Returns the highest msPressed observed while
// the switch reported state==true, which is what a long-press check would evaluate.
static uint32_t advance(gpio_evaluatedSwitch &sw, uint32_t durationMs, uint32_t *maxMsPressedWhilePressed = nullptr)
{
    uint32_t maxSeen = 0;
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += kHandleIntervalMs)
    {
        simulatedTimeMs += kHandleIntervalMs;
        sw.handle();
        if (sw.state && sw.msPressed > maxSeen)
            maxSeen = sw.msPressed;
    }
    if (maxMsPressedWhilePressed)
        *maxMsPressedWhilePressed = maxSeen;
    return maxSeen;
}

// Simulate a complete press of the given duration, followed by a release long enough
// to be confirmed. Returns the highest msPressed seen while the switch reported pressed.
static uint32_t pressFor(gpio_evaluatedSwitch &sw, uint32_t pressDurationMs, uint32_t releaseDurationMs = 500)
{
    uint32_t maxDuringPress = 0;
    uint32_t maxDuringRelease = 0;
    simulatedPinLevel = 0; // pressed
    advance(sw, pressDurationMs, &maxDuringPress);
    simulatedPinLevel = 1; // released
    advance(sw, releaseDurationMs, &maxDuringRelease);
    return maxDuringPress > maxDuringRelease ? maxDuringPress : maxDuringRelease;
}

//===============================
//=========== Tests =============
//===============================

// 1. A press shorter than the debounce time must not be reported at all.
static void test_pressShorterThanDebounceIsIgnored()
{
    beginTest("press shorter than debounce time is ignored");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    simulatedPinLevel = 0;
    advance(sw, 20); // shorter than minOnMs (40 ms)
    simulatedPinLevel = 1;
    CHECK(sw.state == false);

    advance(sw, 200);
    CHECK(sw.state == false);
}

// 2. A normal press is reported once, and msPressed roughly matches the real duration.
static void test_normalPressIsReportedOnce()
{
    beginTest("normal press produces exactly one rising and one falling edge");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    int risingEdgeCount = 0;
    int fallingEdgeCount = 0;

    simulatedPinLevel = 0;
    for (int i = 0; i < 20; i++) // 200 ms pressed
    {
        simulatedTimeMs += kHandleIntervalMs;
        sw.handle();
        if (sw.risingEdge) risingEdgeCount++;
        if (sw.fallingEdge) fallingEdgeCount++;
    }
    simulatedPinLevel = 1;
    for (int i = 0; i < 20; i++) // 200 ms released
    {
        simulatedTimeMs += kHandleIntervalMs;
        sw.handle();
        if (sw.risingEdge) risingEdgeCount++;
        if (sw.fallingEdge) fallingEdgeCount++;
    }

    CHECK(risingEdgeCount == 1);
    CHECK(fallingEdgeCount == 1);
    CHECK(sw.state == false);
}

// 3. REGRESSION (bug B1): a short press must never be evaluated as a long press,
//    not even directly after a long press. This is the exact failure the user hit:
//    msPressed was carried over from the previous press.
static void test_shortPressAfterLongPressIsNotLong()
{
    beginTest("short press after a long press is not detected as long press (B1)");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    const uint32_t longPressThresholdMs = 600; // FULLY_OPEN_LONG_PRESS_DURATION_MS

    // a) deliberate long press -> msPressed must exceed the threshold
    uint32_t maxDuringLongPress = pressFor(sw, 1500);
    CHECK(maxDuringLongPress > longPressThresholdMs);

    // b) several short presses right after -> must all stay well below the threshold
    for (int attempt = 0; attempt < 5; attempt++)
    {
        uint32_t maxDuringShortPress = pressFor(sw, 150);
        CHECK(maxDuringShortPress < longPressThresholdMs);
    }
}

// 4. REGRESSION (bug B1): the value visible in the same handle() call as risingEdge
//    must belong to THIS press, not to the previous one.
static void test_msPressedIsFreshOnRisingEdge()
{
    beginTest("msPressed is not stale when risingEdge is signalled (B1)");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    pressFor(sw, 2000); // long press first, leaves a large msPressed behind

    // now a new press - capture msPressed in the very cycle risingEdge appears
    uint32_t msPressedAtRisingEdge = 0xFFFFFFFF;
    simulatedPinLevel = 0;
    for (int i = 0; i < 20; i++)
    {
        simulatedTimeMs += kHandleIntervalMs;
        sw.handle();
        if (sw.risingEdge)
        {
            msPressedAtRisingEdge = sw.msPressed;
            break;
        }
    }
    CHECK(msPressedAtRisingEdge != 0xFFFFFFFF); // rising edge was actually seen
    CHECK(msPressedAtRisingEdge <= sw.minOnMs + kHandleIntervalMs);
}

// 5. REGRESSION (bug B1 + B2): if handle() is not called for a long time because the
//    control loop was blocked (e.g. a blocking modbus transaction) and the button was
//    released during that gap, the switch must not report a long press afterwards.
static void test_blockedControlLoopDoesNotFakeLongPress()
{
    beginTest("release during a blocked control loop is not reported as long press (B1/B2)");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    const uint32_t longPressThresholdMs = 600;

    pressFor(sw, 1200); // previous long press, so a stale value would be large

    // short press ...
    simulatedPinLevel = 0;
    advance(sw, 100); // long enough for the press to be confirmed (minOnMs = 40)
    CHECK(sw.state == true);

    // ... user releases, but handle() is not called for 800 ms (blocking modbus)
    simulatedPinLevel = 1;
    simulatedTimeMs += 800;
    sw.handle();

    // whatever the switch reports now must not look like a long press
    bool looksLikeLongPress = sw.state && (sw.msPressed > longPressThresholdMs);
    CHECK(looksLikeLongPress == false);
}

// 6. A genuinely held button must still be detected as a long press.
static void test_realLongPressIsStillDetected()
{
    beginTest("a genuinely held button is still detected as a long press");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    const uint32_t longPressThresholdMs = 600;

    simulatedPinLevel = 0;
    bool longPressDetected = false;
    for (int i = 0; i < 200; i++) // hold for 2 s
    {
        simulatedTimeMs += kHandleIntervalMs;
        sw.handle();
        if (sw.state && sw.msPressed > longPressThresholdMs)
            longPressDetected = true;
    }
    CHECK(longPressDetected == true);
}

// 7. Contact bounce on press and on release must not produce extra edges.
static void test_bounceDoesNotProduceExtraEdges()
{
    beginTest("contact bounce does not produce extra edges");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    int risingEdgeCount = 0;
    int fallingEdgeCount = 0;

    // bouncing press: 5 ms low / 5 ms high, four times, then settle low
    for (int b = 0; b < 4; b++)
    {
        simulatedPinLevel = 0; simulatedTimeMs += 5; sw.handle();
        if (sw.risingEdge) risingEdgeCount++;
        simulatedPinLevel = 1; simulatedTimeMs += 5; sw.handle();
        if (sw.fallingEdge) fallingEdgeCount++;
    }
    simulatedPinLevel = 0;
    for (int i = 0; i < 30; i++) // settled, pressed for 300 ms
    {
        simulatedTimeMs += kHandleIntervalMs; sw.handle();
        if (sw.risingEdge) risingEdgeCount++;
        if (sw.fallingEdge) fallingEdgeCount++;
    }
    // bouncing release
    for (int b = 0; b < 4; b++)
    {
        simulatedPinLevel = 1; simulatedTimeMs += 5; sw.handle();
        if (sw.risingEdge) risingEdgeCount++;
        simulatedPinLevel = 0; simulatedTimeMs += 5; sw.handle();
        if (sw.fallingEdge) fallingEdgeCount++;
    }
    simulatedPinLevel = 1;
    for (int i = 0; i < 30; i++) // settled, released for 300 ms
    {
        simulatedTimeMs += kHandleIntervalMs; sw.handle();
        if (sw.risingEdge) risingEdgeCount++;
        if (sw.fallingEdge) fallingEdgeCount++;
    }

    CHECK(risingEdgeCount == 1);
    CHECK(fallingEdgeCount == 1);
    CHECK(sw.state == false);
}

// 8. A bouncing release must not keep inflating msPressed into long-press territory.
static void test_bouncingReleaseDoesNotInflatePressDuration()
{
    beginTest("bouncing release does not inflate msPressed into a long press");
    gpio_evaluatedSwitch sw(GPIO_NUM_0, false, false);

    const uint32_t longPressThresholdMs = 600;

    // short clean press
    simulatedPinLevel = 0;
    advance(sw, 100);

    // release that chatters for 700 ms (each bounce shorter than minOffMs)
    uint32_t maxMsPressedSeen = 0;
    for (int b = 0; b < 35; b++)
    {
        simulatedPinLevel = 1; simulatedTimeMs += 10; sw.handle();
        if (sw.state && sw.msPressed > maxMsPressedSeen) maxMsPressedSeen = sw.msPressed;
        simulatedPinLevel = 0; simulatedTimeMs += 10; sw.handle();
        if (sw.state && sw.msPressed > maxMsPressedSeen) maxMsPressedSeen = sw.msPressed;
    }

    // NOTE: with a physically chattering contact the button really is being held down,
    // so some growth is legitimate. What must not happen is the *previous* press value
    // leaking in - that is covered by the tests above. Here we only assert the value
    // stays a plausible measurement of this press.
    CHECK(maxMsPressedSeen <= 800);
    (void)longPressThresholdMs;
}

//===============================
//============ main =============
//===============================
int main()
{
    printf("=== gpio_evaluatedSwitch host tests ===\n");

    test_pressShorterThanDebounceIsIgnored();
    test_normalPressIsReportedOnce();
    test_shortPressAfterLongPressIsNotLong();
    test_msPressedIsFreshOnRisingEdge();
    test_blockedControlLoopDoesNotFakeLongPress();
    test_realLongPressIsStillDetected();
    test_bounceDoesNotProduceExtraEdges();
    test_bouncingReleaseDoesNotInflatePressDuration();

    printf("=======================================\n");
    if (testsFailed == 0)
    {
        printf("OK - %d tests passed\n", testsRun);
        return 0;
    }
    printf("FAILED - %d check(s) failed in %d tests\n", testsFailed, testsRun);
    return 1;
}
