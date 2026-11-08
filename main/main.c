#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sensor.h"
#include "faultDetect.h"
#include "sensor_history.h"
#include "diagnostic_engine.h"

static const char *TAG = "RESILIENTSENSE";

// Enable for automatic fault-injection testing.
#define ENABLE_FAULT_INJECTION 1

static bool inject_fault(
    sensor_reading_t *reading,
    uint32_t cycle)
{
#if ENABLE_FAULT_INJECTION
    bool injected = false;
    // Simulate acquisition timeout: cycles 8-10.
    if (cycle >= 8 && cycle <= 10) {
        reading->status = ESP_ERR_TIMEOUT;
        injected = true;
    }

    // Simulate successfully acquired but unusable data: cycles 18-20.
    if (cycle >= 18 && cycle <= 20) {
        reading->status = ESP_OK;
        reading->raw_adc = 2000;
        reading->temperature_c = NAN;
        injected = true;
    }

    // Simulate out-of-range temperature: 28-30.
    if (cycle >= 28 && cycle <= 30) {
        reading->status = ESP_OK;
        reading->raw_adc = 2000;
        reading->temperature_c = 125.0f;
        injected = true;
    }

    // Simulate repeated high and low ADC saturation.
    if ((cycle >= 38 && cycle <= 40) ||
        (cycle >= 48 && cycle <= 50)) {
        reading->status = ESP_OK;
        reading->raw_adc = cycle < 45 ? 4095 : 0;
        reading->temperature_c = 25.0f;
        injected = true;
    }

    // No sample is added to history during these simulated missed updates.
    if (cycle >= 58 && cycle <= 63) {
        injected = true;
    }

    // Simulate a rapid but valid temperature change.
    if (cycle >= 68 && cycle <= 70) {
        reading->status = ESP_OK;
        reading->raw_adc = 2000;
        reading->temperature_c = cycle == 68 ? 20.0f :
                                 cycle == 69 ? 125.0f : 20.0f;
        injected = true;
    }
    return injected;
#else
    (void)reading;
    (void)cycle;
    return false;
#endif
}

static void print_diagnostic_report(const diagnostic_report_t *report)
{
    if (report->recovered) {
        ESP_LOGI(TAG,
            "Cycle %lu | RECOVERY VERIFIED | Diagnostic fault cleared",
            (unsigned long)report->cycle);
        return;
    }

    ESP_LOGW(TAG, "========== DIAGNOSTIC REPORT ==========");
    ESP_LOGW(TAG, "Cycle: %lu | Fault: %s (%s) | Severity: %s",
             (unsigned long)report->cycle,
             diagnostic_fault_id(report->fault),
             diagnostic_fault_name(report->fault),
             diagnostic_severity_name(report->severity));
    ESP_LOGW(TAG, "Component: %s",
             diagnostic_component_name(report->component));
    if (report->fault == DIAGNOSTIC_FAULT_ADC_SATURATION) {
        const char *level =
            (report->evidence_flags &
             DIAGNOSTIC_EVIDENCE_ADC_HIGH_SATURATION) != 0
                ? "high" : "low";
        ESP_LOGW(TAG, "Observation: %lu consecutive %s ADC saturation readings",
                 (unsigned long)report->consecutive_count, level);
    } else {
        ESP_LOGW(TAG, "Observation: %s",
                 diagnostic_fault_description(report->fault));
    }
    ESP_LOGW(TAG, "Evidence flags: 0x%08lx | Acquisition status: %s",
             (unsigned long)report->evidence_flags,
             esp_err_to_name((esp_err_t)report->acquisition_status));
    ESP_LOGW(TAG, "Acquired: %lld us | Ingested: %lld us | Simulated: %s",
             (long long)report->acquired_at_us,
             (long long)report->ingested_at_us,
             report->simulated ? "yes" : "no");
    if (report->raw_value_count > 0) {
        ESP_LOGW(TAG, "ADC evidence (%lu consecutive): %d, %d, %d",
                 (unsigned long)report->consecutive_count,
                 report->raw_values[0],
                 report->raw_value_count > 1 ? report->raw_values[1] : 0,
                 report->raw_value_count > 2 ? report->raw_values[2] : 0);
    }
    if (report->elapsed_us > 0) {
        ESP_LOGW(TAG, "Temperature delta: %.2f C in %lld us (%.2f C/s)",
                 report->temperature_delta_c,
                 (long long)report->elapsed_us,
                 report->temperature_rate_c_per_second);
    }
    for (size_t i = 0; i < report->possible_cause_count; ++i) {
        ESP_LOGW(TAG, "Possible cause %u: %s",
                 (unsigned)(i + 1),
                 diagnostic_cause_description(report->possible_causes[i]));
    }
    ESP_LOGW(TAG, "Recommended action: %s",
             diagnostic_action_description(report->recommended_action));
    ESP_LOGW(TAG, "Observed condition confirmed: %s; physical root cause unverified",
             report->diagnosis_confirmed ? "yes" : "no");
    ESP_LOGW(TAG, "=======================================");
}

void app_main(void)
{
    ESP_LOGI(TAG, "ResilientSense starting...");

    ESP_ERROR_CHECK(sensor_init());

    faultDetect_t detector;
    faultDetect_init(&detector);

    sensor_history_t history;
    sensor_history_init(&history);

    diagnostic_engine_t diagnostic_engine;
    diagnostic_engine_init(&diagnostic_engine, NULL);

    ESP_LOGI(TAG, "Sensor and diagnostics initialized");

    uint32_t cycle = 0;

    while (1) {
        cycle++;

        const bool missing_update = cycle >= 58 && cycle <= 63;
        sensor_reading_t reading = missing_update
            ? (sensor_reading_t) {.status = ESP_ERR_TIMEOUT}
            : sensor_read();
        const int64_t acquired_at_us = esp_timer_get_time();

        // Test-only simulated faults.
        const bool simulated = inject_fault(&reading, cycle);

        sensor_sample_t sample = {
            .reading = reading,
            .timestamp_us = esp_timer_get_time(),
            .acquired_at_us = acquired_at_us,
            .acquisition_time_valid = true,
            .simulated = simulated
        };

        if (!missing_update) {
            if (!sensor_history_push(&history, &sample)) {
                ESP_LOGE(TAG, "Cycle %lu | Failed to store sensor sample",
                         (unsigned long)cycle);
            }
        }

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

        if (changed) {
            ESP_LOGI(TAG, "Health state: %s | Basic fault: %s",
                     fault_state_name(detector.state),
                     fault_code_name(detector.fault));
        }

        diagnostic_report_t report;
        const int64_t now_us = esp_timer_get_time();
        if (diagnostic_engine_update(&diagnostic_engine, &history, now_us,
                                     simulated, cycle, &report)) {
            print_diagnostic_report(&report);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
