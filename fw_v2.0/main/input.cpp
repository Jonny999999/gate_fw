#include "input.hpp"

#include "button.hpp"
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
// How often all inputs are sampled. Short and, thanks to the dedicated task, jitter-free.
#define INPUT_SAMPLE_INTERVAL_MS 5

// How long a button level has to be stable before a change is accepted.
// (was gpio_evaluatedSwitch::minOnMs / minOffMs = 40 ms)
#define BUTTON_DEBOUNCE_MS 40

// How long the open button has to be held to count as a long press.
// (was FULLY_OPEN_LONG_PRESS_DURATION_MS in control.cpp - the detection moved here so it
//  no longer depends on how busy the control task is)
// Raised from 600 ms: with the input task the measurement is exact, and control.cpp now
// suspends its input timeout while the button is held, so a longer and more deliberate
// hold costs nothing and separates the gestures more clearly.
#define OPEN_BUTTON_LONG_PRESS_MS 800

// Second threshold on the same press: keep holding past this and the gate will also close
// again by itself afterwards. Deliberately far beyond any accidental hold - nobody rests on
// the button for two and a half seconds - and the user hears the first meaning confirmed at
// 800 ms, so holding on is a conscious decision.
#define OPEN_BUTTON_VERY_LONG_PRESS_MS 2500

// The light barrier is treated asymmetrically on purpose:
//  - 'obstructed' is accepted immediately, so nothing is ever slowed down by debouncing
//  - 'free' needs a stable reading, so a flickering sensor cannot release the gate early
#define LIGHT_BARRIER_FREE_DEBOUNCE_MS 20

// Priority above the control task (5), so sampling keeps running while control blocks on
// a modbus transaction.
#define INPUT_TASK_PRIORITY 8
#define INPUT_TASK_STACK_SIZE 3072

#define INPUT_EVENT_QUEUE_LENGTH 20


//===============================
//========== Variables ==========
//===============================
static const char *TAG_INPUT = "input";

// which physical input an event came from
enum class InputSource : uint8_t
{
    OPEN_BUTTON,
    CLOSE_BUTTON,
    REMOTE_OPEN,
    REMOTE_CLOSE
};

struct InputEventMessage
{
    InputSource source;
    ButtonEvent event;
};

static QueueHandle_t inputEventQueue = nullptr;

// Levels published by the sampling task and read by the control task.
// Plain scalars, written by exactly one task - std::atomic makes that explicit and
// prevents the compiler from caching them across the control loop.
static std::atomic<bool> openButtonIsHeld{false};
static std::atomic<bool> closeButtonIsHeld{false};
static std::atomic<bool> lightBarrierIsObstructed{false};

static InputPinConfig pinConfig;


//===============================
//========== Functions ==========
//===============================
// Send one event to the control task. Never blocks: if the queue is full the control task
// is so far behind that dropping the oldest information is the least bad option, and we
// want to know about it.
static void queueEvent(InputSource source, ButtonEvent event)
{
    const InputEventMessage message = {source, event};
    if (xQueueSend(inputEventQueue, &message, 0) != pdTRUE)
        ESP_LOGE(TAG_INPUT, "input event queue full - dropped %s event", buttonEventToString(event));
}


// Sample the light barrier with the asymmetric debounce described above.
static void handleLightBarrier(uint32_t nowMs)
{
    // when obstructed the sensor pulls its 12V output to GND,
    // the optocoupler turns on and pulls the esp32 input to GND
    const bool rawIsObstructed = (gpio_get_level(pinConfig.lightBarrierGpio) == 0);

    static uint32_t timestampFirstFreeReadingMs = 0;
    static bool waitingForFreeToSettle = false;

    if (rawIsObstructed)
    {
        // accept immediately, never debounce an obstruction
        waitingForFreeToSettle = false;
        if (!lightBarrierIsObstructed.exchange(true))
            ESP_LOGW(TAG_INPUT, "light barrier changed state to 'obstructed'");
        return;
    }

    if (!lightBarrierIsObstructed.load())
        return; // already free, nothing to do

    // reads free while we still consider it obstructed -> require a stable reading
    if (!waitingForFreeToSettle)
    {
        waitingForFreeToSettle = true;
        timestampFirstFreeReadingMs = nowMs;
    }
    else if ((nowMs - timestampFirstFreeReadingMs) >= LIGHT_BARRIER_FREE_DEBOUNCE_MS)
    {
        waitingForFreeToSettle = false;
        lightBarrierIsObstructed.store(false);
        ESP_LOGW(TAG_INPUT, "light barrier changed state to 'free'");
    }
}


//===============================
//======== Sampling task ========
//===============================
static void inputTask(void *param)
{
    // All buttons are wired as switch-to-GND behind an optocoupler -> pressed reads LOW.
    // Only the open button has a long press; for the others it would just be noise.
    DebouncedButton openButton(BUTTON_DEBOUNCE_MS, OPEN_BUTTON_LONG_PRESS_MS, OPEN_BUTTON_VERY_LONG_PRESS_MS);
    DebouncedButton closeButton(BUTTON_DEBOUNCE_MS, 0);
    DebouncedButton remoteOpen(BUTTON_DEBOUNCE_MS, 0);
    DebouncedButton remoteClose(BUTTON_DEBOUNCE_MS, 0);

    ESP_LOGW(TAG_INPUT, "input sampling task started (interval %d ms)", INPUT_SAMPLE_INTERVAL_MS);

    TickType_t lastWakeTime = xTaskGetTickCount();
    while (true)
    {
        const uint32_t nowMs = millis();

        // 1. buttons and remote
        struct
        {
            DebouncedButton &button;
            gpio_num_t gpio;
            InputSource source;
        } inputs[] = {
            {openButton,  pinConfig.openButtonGpio,  InputSource::OPEN_BUTTON},
            {closeButton, pinConfig.closeButtonGpio, InputSource::CLOSE_BUTTON},
            {remoteOpen,  pinConfig.remoteOpenGpio,  InputSource::REMOTE_OPEN},
            {remoteClose, pinConfig.remoteCloseGpio, InputSource::REMOTE_CLOSE},
        };

        for (auto &input : inputs)
        {
            const bool rawIsPressed = (gpio_get_level(input.gpio) == 0);
            const ButtonEvent event = input.button.update(rawIsPressed, nowMs);
            if (event != ButtonEvent::NONE)
            {
                ESP_LOGI(TAG_INPUT, "input %d: %s", (int)input.source, buttonEventToString(event));
                queueEvent(input.source, event);
            }
        }

        // 2. publish levels the control task needs directly
        openButtonIsHeld.store(openButton.getIsPressed());
        closeButtonIsHeld.store(closeButton.getIsPressed());

        // 3. light barrier
        handleLightBarrier(nowMs);

        // 4. wait for the next sampling slot (absolute, so the interval does not drift)
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(INPUT_SAMPLE_INTERVAL_MS));
    }
}


//===============================
//====== Public interface =======
//===============================
void inputStart(const InputPinConfig &pins)
{
    pinConfig = pins;

    gpio_set_direction(pinConfig.openButtonGpio, GPIO_MODE_INPUT);
    gpio_set_direction(pinConfig.closeButtonGpio, GPIO_MODE_INPUT);
    gpio_set_direction(pinConfig.remoteOpenGpio, GPIO_MODE_INPUT);
    gpio_set_direction(pinConfig.remoteCloseGpio, GPIO_MODE_INPUT);
    gpio_set_direction(pinConfig.lightBarrierGpio, GPIO_MODE_INPUT);

    inputEventQueue = xQueueCreate(INPUT_EVENT_QUEUE_LENGTH, sizeof(InputEventMessage));
    if (inputEventQueue == nullptr)
    {
        ESP_LOGE(TAG_INPUT, "failed to create input event queue!");
        return;
    }

    xTaskCreate(&inputTask, "inputTask", INPUT_TASK_STACK_SIZE, nullptr, INPUT_TASK_PRIORITY, nullptr);
}


InputState inputPoll()
{
    InputState state = {};

    // drain every event that arrived since the last call, so nothing is lost even if the
    // control task was blocked for a while
    InputEventMessage message;
    while (inputEventQueue != nullptr && xQueueReceive(inputEventQueue, &message, 0) == pdTRUE)
    {
        switch (message.source)
        {
        case InputSource::OPEN_BUTTON:
            if (message.event == ButtonEvent::PRESSED)         state.openButtonPressed = true;
            if (message.event == ButtonEvent::LONG_PRESS)      state.openButtonLongPress = true;
            if (message.event == ButtonEvent::VERY_LONG_PRESS) state.openButtonVeryLongPress = true;
            break;
        case InputSource::CLOSE_BUTTON:
            if (message.event == ButtonEvent::PRESSED)    state.closeButtonPressed = true;
            break;
        case InputSource::REMOTE_OPEN:
            if (message.event == ButtonEvent::PRESSED)    state.remoteOpenPressed = true;
            break;
        case InputSource::REMOTE_CLOSE:
            if (message.event == ButtonEvent::PRESSED)    state.remoteClosePressed = true;
            break;
        }
    }

    state.anyButtonPressed = state.openButtonPressed || state.closeButtonPressed ||
                             state.remoteOpenPressed || state.remoteClosePressed;

    state.openButtonIsHeld = openButtonIsHeld.load();
    state.closeButtonIsHeld = closeButtonIsHeld.load();
    state.lightBarrierIsObstructed = lightBarrierIsObstructed.load();

    return state;
}
