#include "indicator.hpp"

#include "timing.hpp"

#include <atomic>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
}

//===============================
//========= Parameters ==========
//===============================
// Tick of the indicator task. Determines how precisely the on/off times are hit;
// 5 ms is well below the shortest signal in use (20 ms).
#define INDICATOR_TICK_MS 5

// Lowest priority of all application tasks: this only drives a buzzer and an LED, so it
// must never delay input sampling (8), the control logic or gate handling (both 5).
// Its own timing stays accurate anyway, because all the other tasks spend nearly all their
// time blocked in vTaskDelay().
#define INDICATOR_TASK_PRIORITY 3
#define INDICATOR_TASK_STACK_SIZE 2560
#define BUZZER_QUEUE_LENGTH 20

// Mirror the buzzer on the fault LED whenever no fault and no status needs it.
// Off by default: the GREEN panel LED is already wired in parallel with the buzzer, so the
// acoustic feedback is mirrored visually in hardware. Mirroring it on the red LED as well
// would only make the one channel that can show a persistent state harder to read.
#define LED_MIRRORS_BUZZER 0


//=====================================================
//=============== Pattern catalogue ===================
//=====================================================
// Every acoustic and visual signal of the firmware, in one place.
//
//   kBuzzerSignals     one row per BuzzerSignal   { msOn, msOff, repeatCount }
//   INDICATOR_BLINK_*  shared LED blink rates     { mode, msOn, msOff, repeatCount }
//
// The blink rates are macros because several signals share them - a fault and a status can
// deliberately blink at the same rate, and 'FAST' is the thing they have in common. The
// buzzer timings are not, because every one of them is used exactly once.
//
// Kept here rather than in config.h on purpose - these are the indicator task's own
// vocabulary, not a wiring or tuning parameter someone would look for elsewhere.

//--- buzzer signals ---
// see kBuzzerSignals further down: each signal is used exactly once, so its timing lives
// directly in that table instead of in a macro that would only add a second place to edit.

//--- LED blink patterns ---
// The rate is the message: the faster it blinks, the more attention it needs.
#define INDICATOR_BLINK_OFF        { BlinkMode::OFF,      0,    0, 0 }
#define INDICATOR_BLINK_SOLID      { BlinkMode::ON,       0,    0, 0 }
#define INDICATOR_BLINK_VERY_FAST  { BlinkMode::BLINKING, 100,  100, INDICATOR_REPEAT_FOREVER }
#define INDICATOR_BLINK_FAST       { BlinkMode::BLINKING, 200,  200, INDICATOR_REPEAT_FOREVER }
#define INDICATOR_BLINK_SLOW       { BlinkMode::BLINKING, 500,  500, INDICATOR_REPEAT_FOREVER }
#define INDICATOR_BLINK_VERY_SLOW  { BlinkMode::BLINKING, 1000, 1000, INDICATOR_REPEAT_FOREVER }


//===============================
//========== Variables ==========
//===============================
static const char *TAG_INDICATOR = "indicator";

// One entry of the buzzer queue
struct BuzzerQueueEntry
{
    uint16_t msOn;
    uint16_t msOff;
    uint16_t repeatCount;
};

static QueueHandle_t buzzerQueue = nullptr;
static IndicatorPinConfig pinConfig;

// Requested by other tasks, rendered by the indicator task
static std::atomic<FaultCode> requestedFault{FaultCode::NONE};
static std::atomic<StatusIndication> requestedStatus{StatusIndication::IDLE};
// Set when an alarm / stop request has to interrupt whatever the buzzer is doing
static std::atomic<bool> buzzerAlarmActive{false};
static std::atomic<uint32_t> buzzerAlarmTiming{0}; // msOn in the high half, msOff in the low half
static std::atomic<bool> buzzerStopRequested{false};


//=========================================================
//===== IndicatorChannel - drives one output pin ==========
//=========================================================
// Small state machine that turns a BlinkPattern into pin levels. Used for the buzzer and
// for the LED; the difference in behaviour comes from how the two are fed, not from here.
class IndicatorChannel
{
public:
    void init(gpio_num_t gpio)
    {
        this->gpio = gpio;
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(gpio, 0);
    }

    // Start a new pattern. Restarts even if it is the same one.
    void setPattern(const BlinkPattern &newPattern, uint32_t nowMs)
    {
        pattern = newPattern;
        repeatsDone = 0;
        finished = (newPattern.mode == BlinkMode::OFF);
        phaseIsOn = (newPattern.mode != BlinkMode::OFF);
        timestampPhaseStartMs = nowMs;
        applyOutput(phaseIsOn);
    }

    // Advance the pattern. Call once per task tick.
    void update(uint32_t nowMs)
    {
        if (finished || pattern.mode != BlinkMode::BLINKING)
            return;

        const uint32_t phaseDurationMs = phaseIsOn ? pattern.msOn : pattern.msOff;
        if ((nowMs - timestampPhaseStartMs) < phaseDurationMs)
            return;

        timestampPhaseStartMs = nowMs;

        if (phaseIsOn)
        {
            // on phase over -> go dark, unless there is no off phase at all
            if (pattern.msOff == 0)
                return; // stays on permanently
            phaseIsOn = false;
            applyOutput(false);
        }
        else
        {
            // one full on/off cycle completed
            repeatsDone++;
            if (pattern.repeatCount != INDICATOR_REPEAT_FOREVER && repeatsDone >= pattern.repeatCount)
            {
                finished = true;
                applyOutput(false);
                return;
            }
            phaseIsOn = true;
            applyOutput(true);
        }
    }

    bool isBusy() const { return !finished; }
    bool getOutputIsOn() const { return outputIsOn; }

private:
    void applyOutput(bool on)
    {
        outputIsOn = on;
        gpio_set_level(gpio, on ? 1 : 0);
    }

    gpio_num_t gpio = GPIO_NUM_0;
    BlinkPattern pattern = INDICATOR_BLINK_OFF;
    uint16_t repeatsDone = 0;
    bool phaseIsOn = false;
    bool finished = true;
    bool outputIsOn = false;
    uint32_t timestampPhaseStartMs = 0;
};


//===============================
//====== Signal -> pattern ======
//===============================
// One row per BuzzerSignal, in enum order. Both the order and the completeness are checked
// at compile time below, so adding a signal without a timing cannot slip through.
struct BuzzerSignalDefinition
{
    BuzzerSignal signal; // which signal this row describes
    uint16_t msOn;
    uint16_t msOff;
    uint16_t repeatCount;
};

static constexpr BuzzerSignalDefinition kBuzzerSignals[] = {
    {BuzzerSignal::STARTUP,                   50,  100, 3}, // firmware booted
    {BuzzerSignal::BUTTON_ACKNOWLEDGED,      100,    0, 1}, // open button registered
    {BuzzerSignal::ADDITIONAL_PRESS,          60,    0, 1}, // one more press counted
    {BuzzerSignal::OPEN_FURTHER_CONFIRMED,    40,   20, 2}, // opening further
    {BuzzerSignal::MOVEMENT_START_WARNING,  1000,    0, 1}, // gate is about to move
    {BuzzerSignal::MOVEMENT_STOPPED,         400,    0, 1}, // movement stopped
    {BuzzerSignal::BARRIER_BLOCKED,          100,   50, 4}, // light barrier interrupted
    {BuzzerSignal::LIMIT_SWITCH_REACHED,     100,   50, 1}, // limit switch changed while off
    {BuzzerSignal::OBSTRUCTION_DETECTED,    1500,  100, 1}, // motor current too high
    {BuzzerSignal::FAULT,                     70,  100, 5}, // something went wrong
};

static constexpr size_t kBuzzerSignalCount = sizeof(kBuzzerSignals) / sizeof(kBuzzerSignals[0]);

static_assert(kBuzzerSignalCount == (size_t)BuzzerSignal::COUNT,
              "every BuzzerSignal needs exactly one row in kBuzzerSignals");

// the table is indexed by the enum value, so the rows have to be in enum order
static constexpr bool buzzerSignalTableIsInEnumOrder()
{
    for (size_t i = 0; i < kBuzzerSignalCount; i++)
        if (kBuzzerSignals[i].signal != (BuzzerSignal)i)
            return false;
    return true;
}
static_assert(buzzerSignalTableIsInEnumOrder(),
              "kBuzzerSignals rows must be in the same order as the BuzzerSignal enum");


static BuzzerQueueEntry buzzerSignalToEntry(BuzzerSignal signal)
{
    if ((size_t)signal >= kBuzzerSignalCount)
        return {0, 0, 0};
    const BuzzerSignalDefinition &definition = kBuzzerSignals[(size_t)signal];
    return {definition.msOn, definition.msOff, definition.repeatCount};
}


// Faster blinking = more serious, see the comment on FaultCode in indicator.hpp.
static BlinkPattern faultCodeToPattern(FaultCode code)
{
    switch (code)
    {
    case FaultCode::VFD_COMMUNICATION:        return INDICATOR_BLINK_VERY_FAST;
    case FaultCode::MOVEMENT_TIMEOUT:         return INDICATOR_BLINK_FAST;
    case FaultCode::OBSTRUCTION:              return INDICATOR_BLINK_SLOW;
    case FaultCode::BARRIER_BLOCKED_TOO_LONG: return INDICATOR_BLINK_VERY_SLOW;
    case FaultCode::NONE:
    default:                                  return INDICATOR_BLINK_OFF;
    }
}


static BlinkPattern statusToPattern(StatusIndication status)
{
    switch (status)
    {
    // same blink rate the paused state has always used
    case StatusIndication::WAITING_FOR_BARRIER: return INDICATOR_BLINK_FAST;
    // solid, so the barrier can be aligned by watching the LED
    case StatusIndication::BARRIER_OBSTRUCTED:  return INDICATOR_BLINK_SOLID;
    case StatusIndication::IDLE:
    default:                                    return INDICATOR_BLINK_OFF;
    }
}


//===============================
//======= Indicator task ========
//===============================
static void indicatorTask(void *param)
{
    IndicatorChannel buzzerChannel;
    IndicatorChannel faultLedChannel;
    buzzerChannel.init(pinConfig.buzzerGpio);
    faultLedChannel.init(pinConfig.faultLedGpio);

    // what the LED is showing right now, so it is only restarted when it actually changes
    LedPriority activeLedPriority = LedPriority::BUZZER_MIRROR;
    FaultCode activeFault = FaultCode::NONE;
    StatusIndication activeStatus = StatusIndication::IDLE;
#if LED_MIRRORS_BUZZER
    bool activeBuzzerMirrorIsOn = false;
#endif
    bool ledPatternInitialised = false;

    ESP_LOGW(TAG_INDICATOR, "indicator task started (tick %d ms)", INDICATOR_TICK_MS);

    TickType_t lastWakeTime = xTaskGetTickCount();
    while (true)
    {
        const uint32_t nowMs = millis();

        //--- 1. buzzer: alarm and stop requests interrupt whatever is playing ---
        if (buzzerStopRequested.exchange(false))
        {
            xQueueReset(buzzerQueue);
            buzzerChannel.setPattern(INDICATOR_BLINK_OFF, nowMs);
        }
        if (buzzerAlarmActive.exchange(false))
        {
            const uint32_t timing = buzzerAlarmTiming.load();
            xQueueReset(buzzerQueue);
            buzzerChannel.setPattern({BlinkMode::BLINKING,
                                      (uint16_t)(timing >> 16),
                                      (uint16_t)(timing & 0xFFFF),
                                      INDICATOR_REPEAT_FOREVER},
                                     nowMs);
        }

        //--- 2. buzzer: start the next queued signal once the current one finished ---
        if (!buzzerChannel.isBusy())
        {
            BuzzerQueueEntry entry;
            if (xQueueReceive(buzzerQueue, &entry, 0) == pdTRUE)
                buzzerChannel.setPattern({BlinkMode::BLINKING, entry.msOn, entry.msOff, entry.repeatCount}, nowMs);
        }
        buzzerChannel.update(nowMs);

        //--- 3. LED: decide what deserves to be shown ---
        const FaultCode fault = requestedFault.load();
        const StatusIndication status = requestedStatus.load();

        LedPriority priority = LedPriority::BUZZER_MIRROR;
        if (fault != FaultCode::NONE)
            priority = LedPriority::FAULT;
        else if (status != StatusIndication::IDLE)
            priority = LedPriority::STATUS;

        //--- 4. LED: apply, but only restart the pattern when something actually changed ---
        if (priority == LedPriority::FAULT)
        {
            if (!ledPatternInitialised || activeLedPriority != priority || activeFault != fault)
                faultLedChannel.setPattern(faultCodeToPattern(fault), nowMs);
        }
        else if (priority == LedPriority::STATUS)
        {
            if (!ledPatternInitialised || activeLedPriority != priority || activeStatus != status)
                faultLedChannel.setPattern(statusToPattern(status), nowMs);
        }
        else
        {
#if LED_MIRRORS_BUZZER
            const bool mirrorShouldBeOn = buzzerChannel.getOutputIsOn();
            if (!ledPatternInitialised || activeLedPriority != priority || activeBuzzerMirrorIsOn != mirrorShouldBeOn)
                faultLedChannel.setPattern(mirrorShouldBeOn ? BlinkPattern INDICATOR_BLINK_SOLID
                                                       : BlinkPattern INDICATOR_BLINK_OFF, nowMs);
            activeBuzzerMirrorIsOn = mirrorShouldBeOn;
#else
            if (!ledPatternInitialised || activeLedPriority != priority)
                faultLedChannel.setPattern(INDICATOR_BLINK_OFF, nowMs);
#endif
        }

        activeLedPriority = priority;
        activeFault = fault;
        activeStatus = status;
        ledPatternInitialised = true;

        faultLedChannel.update(nowMs);

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(INDICATOR_TICK_MS));
    }
}


//===============================
//====== Public interface =======
//===============================
void indicatorStart(const IndicatorPinConfig &pins)
{
    pinConfig = pins;

    buzzerQueue = xQueueCreate(BUZZER_QUEUE_LENGTH, sizeof(BuzzerQueueEntry));
    if (buzzerQueue == nullptr)
    {
        ESP_LOGE(TAG_INDICATOR, "failed to create buzzer queue!");
        return;
    }

    xTaskCreate(&indicatorTask, "indicatorTask", INDICATOR_TASK_STACK_SIZE, nullptr,
                INDICATOR_TASK_PRIORITY, nullptr);
}


void indicatorBeep(BuzzerSignal signal)
{
    const BuzzerQueueEntry entry = buzzerSignalToEntry(signal);
    if (entry.repeatCount == 0)
        return;
    if (buzzerQueue != nullptr && xQueueSend(buzzerQueue, &entry, 0) != pdTRUE)
        ESP_LOGW(TAG_INDICATOR, "buzzer queue full - dropped signal %d", (int)signal);
}


void indicatorBeepCustom(uint8_t count, uint16_t msOn, uint16_t msOff)
{
    if (count == 0)
        return;
    const BuzzerQueueEntry entry = {msOn, msOff, count};
    if (buzzerQueue != nullptr && xQueueSend(buzzerQueue, &entry, 0) != pdTRUE)
        ESP_LOGW(TAG_INDICATOR, "buzzer queue full - dropped custom beep");
}


void indicatorBuzzerAlarm(uint16_t msOn, uint16_t msOff)
{
    buzzerAlarmTiming.store(((uint32_t)msOn << 16) | msOff);
    buzzerAlarmActive.store(true);
}


void indicatorBuzzerStop()
{
    buzzerStopRequested.store(true);
}


void indicatorSetFault(FaultCode code)
{
    if (code != FaultCode::NONE && requestedFault.load() != code)
        ESP_LOGE(TAG_INDICATOR, "fault indication set to code %d", (int)code);
    requestedFault.store(code);
}


void indicatorClearFault()
{
    requestedFault.store(FaultCode::NONE);
}


FaultCode indicatorGetFault()
{
    return requestedFault.load();
}


void indicatorSetStatus(StatusIndication status)
{
    requestedStatus.store(status);
}
