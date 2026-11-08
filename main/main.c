#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sensor.h"
#include "faultDetect.h"

static const char *TAG = "RESILIENTSENSE";

// Enable for automatic fault-injection testing.
#define ENABLE_FAULT_INJECTION 1

static void inject_fault(
    sensor_reading_t *reading,
    uint32_t cycle)
{
#if ENABLE_FAULT_INJECTION
    // Simulate acquisition timeout: cycles 8-10.
    if (cycle >= 8 && cycle <= 10) {
        reading->status = ESP_ERR_TIMEOUT;
    }

    // Simulate invalid data: cycles 18-20.
    if (cycle >= 18 && cycle <= 20) {
        reading->status = ESP_ERR_INVALID_RESPONSE;
    }

    // Simulate out-of-range temperature: 28-30.
    if (cycle >= 28 && cycle <= 30) {
        reading->status = ESP_OK;
        reading->raw_adc = 2000;
        reading->temperature_c = 125.0f;
    }
#else
    (void)reading;
    (void)cycle;
#endif
}

static void print_diagnostic_report(
    const faultDetect_t *detector)
{
    int64_t timestamp_ms =
        esp_timer_get_time() / 1000;

    if (detector->state == HEALTH_HEALTHY) {
        ESP_LOGI(TAG,
            "RECOVERY VERIFIED | Timestamp: %lld ms",
            (long long)timestamp_ms);
        return;
    }

    ESP_LOGW(TAG, "========== DIAGNOSTIC REPORT ==========");
    ESP_LOGW(TAG, "Timestamp: %lld ms",
             (long long)timestamp_ms);
    ESP_LOGW(TAG, "Health: %s",
             fault_state_name(detector->state));
    ESP_LOGW(TAG, "Fault: %s",
             fault_code_name(detector->fault));
    ESP_LOGW(TAG, "Description: %s",
             fault_description(detector->fault));
    ESP_LOGW(TAG, "Corrective action: %s",
             fault_corrective_action(detector->fault));
    ESP_LOGW(TAG, "Consecutive failures: %lu",
             (unsigned long)detector->consecutive_failures);
    ESP_LOGW(TAG, "Total failures: %lu",
             (unsigned long)detector->total_failures);
    ESP_LOGW(TAG, "=======================================");
}

void app_main(void)
{
    ESP_LOGI(TAG, "ResilientSense starting...");

    ESP_ERROR_CHECK(sensor_init());

    faultDetect_t detector;
    faultDetect_init(&detector);

    ESP_LOGI(TAG, "Sensor and diagnostics initialized");

    uint32_t cycle = 0;

    while (1) {
        cycle++;

        sensor_reading_t reading = sensor_read();

        // Test-only simulated faults.
        inject_fault(&reading, cycle);

        bool changed =
            faultDetect_update(&detector, &reading);

        if (reading.status == ESP_OK) {
            ESP_LOGI(TAG,
                "Cycle %lu | ADC: %d | Temp: %.2f C | Health: %s",
                (unsigned long)cycle,
                reading.raw_adc,
                reading.temperature_c,
                fault_state_name(detector.state));
        } else {
            ESP_LOGE(TAG,
                "Cycle %lu | Read error: %s | Health: %s",
                (unsigned long)cycle,
                esp_err_to_name(reading.status),
                fault_state_name(detector.state));
        }

        // Generate reports only when state or fault changes.
        if (changed) {
            print_diagnostic_report(&detector);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
