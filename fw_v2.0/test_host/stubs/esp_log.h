// Minimal host stub of esp_log.h
#pragma once
#include <stdint.h>

// Discard the message but still "use" the tag, so a file-scope log tag does not
// trigger an unused-variable warning in the host build.
#define ESP_LOGE(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGI(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGD(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, ...) do { (void)(tag); } while (0)

// implemented by the test: lets a test control the simulated clock
uint32_t esp_log_timestamp(void);
