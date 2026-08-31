//=============================================================================
//========== Host tests for BlinkChannel (beep / blink pattern) ===============
//=============================================================================
// Drives the pattern generator with a simulated clock and records the resulting output
// state, so the beep and blink shapes can be checked without a buzzer.
//
// Background: a single beep is written as {msOn, 0, 1} - no pause after it. That zero was
// once treated as "no off phase at all", which left the buzzer stuck on permanently and,
// because the channel never reported itself finished, swallowed every later beep too.
// Several tests below pin that down.

#include "blink.hpp"

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
//======== Test helpers =========
//===============================
// Interval the indicator task ticks at
static const uint32_t kTickMs = 10;

// Run the channel for durationMs, returning how long the output was on in total and
// how many separate on-pulses there were.
struct RunResult
{
    uint32_t msOn;
    int pulseCount;
    bool endedOn;
    bool stillBusy;
};

static RunResult run(BlinkChannel &channel, uint32_t durationMs, uint32_t &nowMs)
{
    RunResult result = {0, 0, false, false};
    bool wasOn = channel.getOutputIsOn();
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += kTickMs)
    {
        nowMs += kTickMs;
        channel.update(nowMs);
        const bool isOn = channel.getOutputIsOn();
        if (isOn)
            result.msOn += kTickMs;
        if (isOn && !wasOn)
            result.pulseCount++;
        wasOn = isOn;
    }
    result.endedOn = channel.getOutputIsOn();
    result.stillBusy = channel.isBusy();
    return result;
}

//===============================
//=========== Tests =============
//===============================

// 1. REGRESSION: a single beep with no pause after it must END, not stay on.
//    This is the bug that left the buzzer howling as soon as the gate was opened.
static void test_singleBeepWithoutPauseTurnsOffAgain()
{
    beginTest("single beep written as {msOn, 0, 1} turns off again");
    uint32_t now = 1000;
    BlinkChannel channel;

    channel.setPattern({BlinkMode::BLINKING, 100, 0, 1}, now);
    CHECK(channel.getOutputIsOn() == true); // starts immediately

    const RunResult result = run(channel, 1000, now);
    CHECK(result.endedOn == false);
    CHECK(result.stillBusy == false);
    CHECK(result.pulseCount == 0);       // the pulse was already running when we started
    CHECK(result.msOn <= 100 + kTickMs); // and it lasted about msOn, not the whole second
}

// 2. REGRESSION: the channel must report itself finished afterwards, otherwise the
//    indicator task never pulls the next signal off the queue.
static void test_channelReportsFinishedSoTheQueueDrains()
{
    beginTest("channel reports finished, so queued signals still play");
    uint32_t now = 1000;
    BlinkChannel channel;

    channel.setPattern({BlinkMode::BLINKING, 100, 0, 1}, now);
    run(channel, 500, now);
    CHECK(channel.isBusy() == false);

    // a second signal must be able to play right after
    channel.setPattern({BlinkMode::BLINKING, 100, 0, 1}, now);
    CHECK(channel.isBusy() == true);
    CHECK(channel.getOutputIsOn() == true);
    const RunResult second = run(channel, 500, now);
    CHECK(second.endedOn == false);
    CHECK(second.stillBusy == false);
}

// 3. All the single-beep signals of the firmware behave the same way.
static void test_everySingleBeepSignalEnds()
{
    beginTest("every single-beep signal shape ends by itself");
    const uint16_t durationsMs[] = {60, 100, 400, 500, 1000, 1500};
    for (uint16_t msOn : durationsMs)
    {
        uint32_t now = 1000;
        BlinkChannel channel;
        channel.setPattern({BlinkMode::BLINKING, msOn, 0, 1}, now);
        const RunResult result = run(channel, msOn + 500, now);
        CHECK(result.endedOn == false);
        CHECK(result.stillBusy == false);
        CHECK(result.msOn <= (uint32_t)msOn + kTickMs);
    }
}

// 4. A repeated pattern with pauses produces the expected number of separate pulses.
static void test_repeatedPatternProducesExpectedPulses()
{
    beginTest("repeated pattern produces the expected number of pulses");
    uint32_t now = 1000;
    BlinkChannel channel;

    // the startup signal: 3 beeps of 50 ms with 100 ms between them
    channel.setPattern({BlinkMode::BLINKING, 50, 100, 3}, now);
    const RunResult result = run(channel, 2000, now);

    // the first pulse is already on when the run starts, so two more edges are counted
    CHECK(result.pulseCount == 2);
    CHECK(result.endedOn == false);
    CHECK(result.stillBusy == false);
}

// 5. A repeated pattern WITHOUT pauses is one continuous tone, and still ends.
static void test_repeatedPatternWithoutPauseIsContinuousAndEnds()
{
    beginTest("repeats without a pause form one continuous tone and still end");
    uint32_t now = 1000;
    BlinkChannel channel;

    channel.setPattern({BlinkMode::BLINKING, 100, 0, 3}, now);
    const RunResult result = run(channel, 1000, now);

    CHECK(result.pulseCount == 0); // never went off in between
    CHECK(result.endedOn == false);
    CHECK(result.stillBusy == false);
    CHECK(result.msOn >= 300 - kTickMs);
    CHECK(result.msOn <= 300 + kTickMs);
}

// 6. An endless pattern keeps going and never reports itself finished.
static void test_endlessPatternKeepsRunning()
{
    beginTest("endless pattern keeps blinking and stays busy");
    uint32_t now = 1000;
    BlinkChannel channel;

    channel.setPattern({BlinkMode::BLINKING, 100, 900, INDICATOR_REPEAT_FOREVER}, now);
    const RunResult result = run(channel, 5000, now);

    CHECK(result.stillBusy == true);
    CHECK(result.pulseCount >= 4); // roughly one flash per second
    // short flash, long gap - the LED must be off most of the time
    CHECK(result.msOn < 1000);
}

// 7. Steady ON and OFF do what they say.
static void test_steadyOnAndOff()
{
    beginTest("steady ON stays on, OFF stays off");
    uint32_t now = 1000;
    BlinkChannel channel;

    channel.setPattern({BlinkMode::ON, 0, 0, 0}, now);
    const RunResult onResult = run(channel, 2000, now);
    CHECK(onResult.msOn == 2000);
    CHECK(onResult.endedOn == true);

    channel.setPattern({BlinkMode::OFF, 0, 0, 0}, now);
    const RunResult offResult = run(channel, 2000, now);
    CHECK(offResult.msOn == 0);
    CHECK(offResult.endedOn == false);
}

// 8. Replacing a pattern mid-flight takes effect immediately - this is what the LED
//    priority arbitration relies on.
static void test_patternCanBeReplacedMidFlight()
{
    beginTest("a new pattern replaces the running one immediately");
    uint32_t now = 1000;
    BlinkChannel channel;

    channel.setPattern({BlinkMode::BLINKING, 1000, 1000, INDICATOR_REPEAT_FOREVER}, now);
    run(channel, 200, now);
    CHECK(channel.getOutputIsOn() == true);

    channel.setPattern({BlinkMode::OFF, 0, 0, 0}, now);
    CHECK(channel.getOutputIsOn() == false);
    const RunResult result = run(channel, 2000, now);
    CHECK(result.msOn == 0);
}

//===============================
//============ main =============
//===============================
int main()
{
    printf("=== BlinkChannel host tests ===\n");

    test_singleBeepWithoutPauseTurnsOffAgain();
    test_channelReportsFinishedSoTheQueueDrains();
    test_everySingleBeepSignalEnds();
    test_repeatedPatternProducesExpectedPulses();
    test_repeatedPatternWithoutPauseIsContinuousAndEnds();
    test_endlessPatternKeepsRunning();
    test_steadyOnAndOff();
    test_patternCanBeReplacedMidFlight();

    printf("===============================\n");
    if (checksFailed == 0)
    {
        printf("OK - %d tests passed\n", testsRun);
        return 0;
    }
    printf("FAILED - %d check(s) failed in %d tests\n", checksFailed, testsRun);
    return 1;
}
