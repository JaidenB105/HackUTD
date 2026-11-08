#ifndef TEST_MOCK_ESP_ERR_H
#define TEST_MOCK_ESP_ERR_H

// Test-only replacement for ESP-IDF error definitions.
// These numeric values are local to the test harness.

typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                 -1
#define ESP_ERR_TIMEOUT        0x107
#define ESP_ERR_INVALID_RESPONSE 0x108

#endif
