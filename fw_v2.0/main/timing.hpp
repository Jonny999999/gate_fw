#pragma once

#include <stdint.h>
#include <esp_timer.h>

//=====================================================
//========== Single time source for the firmware ======
//=====================================================
// Previously the code mixed two different clocks:
//   - esp_log_timestamp()   (control.cpp, gpio_evaluateSwitch)
//   - esp_timer_get_time()  (gate.cpp)
// esp_log_timestamp() is a *logging* API and derives from the FreeRTOS tick, so with
// CONFIG_FREERTOS_HZ=100 it only advances in 10 ms steps - the same order of magnitude as
// the intervals that were being measured with it. Everything now uses esp_timer, which is
// a 64 bit microsecond counter independent of the tick rate.

// Milliseconds since boot.
// Wraps after ~49.7 days. That is safe for interval checks written as
//     (millis() - timestampOfEvent) > intervalMs
// because the unsigned subtraction wraps consistently. Do NOT write comparisons as
//     millis() > timestampOfEvent + intervalMs
static inline uint32_t millis()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Microseconds since boot (does not wrap in any practical runtime).
static inline uint64_t micros()
{
    return (uint64_t)esp_timer_get_time();
}
