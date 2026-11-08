#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "diagnostic_engine.h"

static unsigned checks;
static unsigned failures;

#define CHECK(condition) do {                                           \
    ++checks;                                                           \
    if (!(condition)) {                                                 \
        fprintf(stderr, "FAIL %s:%d: %s\n",                            \
                __FILE__, __LINE__, #condition);                        \
        ++failures;                                                     \
    }                                                                   \
} while (0)

static sensor_sample_t make_sample(int raw_adc,
                                   float temperature,
                                   esp_err_t status,
                                   int64_t acquired_at_us,
                                   int64_t ingested_at_us)
{
    sensor_sample_t sample = {0};
    sample.reading.raw_adc = raw_adc;
    sample.reading.temperature_c = temperature;
    sample.reading.status = status;
    sample.timestamp_us = ingested_at_us;
    sample.acquired_at_us = acquired_at_us;
    sample.acquisition_time_valid = true;
    return sample;
}

static void push(sensor_history_t *history,
                int raw_adc,
                float temperature,
                esp_err_t status,
                int64_t acquired_at_us,
                int64_t ingested_at_us)
{
    const sensor_sample_t sample = make_sample(
        raw_adc, temperature, status, acquired_at_us, ingested_at_us);
    CHECK(sensor_history_push(history, &sample));
}

static diagnostic_config_t test_config(void)
{
    diagnostic_config_t config;
    diagnostic_config_init(&config);
    return config;
}

static bool analyze(diagnostic_engine_t *engine,
                    const sensor_history_t *history,
                    int64_t now_us,
                    diagnostic_report_t *report)
{
    return diagnostic_engine_analyze(engine, history, now_us, false, 1,
                                     report);
}

static void test_empty_and_normal_history(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_config_t config = test_config();
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;

    CHECK(analyze(&engine, &history, 1000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);

    push(&history, 2000, 25.0f, ESP_OK, 1000000, 1001000);
    push(&history, 2001, 25.2f, ESP_OK, 2000000, 2001000);
    push(&history, 1999, 25.1f, ESP_OK, 3000000, 3001000);
    CHECK(analyze(&engine, &history, 3500000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);
}

static void test_high_saturation_and_boundary(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_config_t config = test_config();
    config.adc_high_saturation_threshold = 4090;
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;

    push(&history, 4090, 25.0f, ESP_OK, 1000000, 1000001);
    push(&history, 4095, 25.0f, ESP_OK, 2000000, 2000001);
    push(&history, 4095, 25.0f, ESP_OK, 3000000, 3000001);
    CHECK(analyze(&engine, &history, 3000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_ADC_SATURATION);
    CHECK(report.evidence_flags & DIAGNOSTIC_EVIDENCE_ADC_HIGH_SATURATION);
    CHECK(report.consecutive_count == 3);
    CHECK(report.raw_value_count == 3);
    CHECK(report.raw_values[0] == 4090);
    CHECK(report.raw_values[2] == 4095);
    CHECK(report.diagnosis_confirmed);
}

static void test_low_saturation_and_boundary(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_config_t config = test_config();
    config.adc_low_saturation_threshold = 5;
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;

    push(&history, 5, 25.0f, ESP_OK, 1000000, 1000001);
    push(&history, 0, 25.0f, ESP_OK, 2000000, 2000001);
    push(&history, 0, 25.0f, ESP_OK, 3000000, 3000001);
    CHECK(analyze(&engine, &history, 3000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_ADC_SATURATION);
    CHECK(report.evidence_flags & DIAGNOSTIC_EVIDENCE_ADC_LOW_SATURATION);
}

static void test_repeated_invalid_successful_measurements(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, NULL);
    diagnostic_report_t report;

    push(&history, 2000, NAN, ESP_OK, 1000000, 1000001);
    push(&history, 2000, NAN, ESP_OK, 2000000, 2000001);
    push(&history, 2000, NAN, ESP_OK, 3000000, 3000001);
    CHECK(analyze(&engine, &history, 3000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA);
    CHECK(report.evidence_flags & DIAGNOSTIC_EVIDENCE_INVALID_VALUE);
    CHECK(report.recommended_action ==
          DIAGNOSTIC_ACTION_INSPECT_ACQUISITION);
}

static void test_communication_failures(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, NULL);
    diagnostic_report_t report;

    push(&history, 2000, 25.0f, ESP_ERR_TIMEOUT, 1000000, 1000001);
    push(&history, 2000, 25.0f, ESP_FAIL, 2000000, 2000001);
    push(&history, 2000, 25.0f, ESP_ERR_TIMEOUT, 3000000, 3000001);
    CHECK(analyze(&engine, &history, 3000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE);
    CHECK(report.evidence_flags & DIAGNOSTIC_EVIDENCE_ACQUISITION_ERROR);
    CHECK(report.acquisition_status == ESP_ERR_TIMEOUT);
    CHECK(diagnostic_fault_description(report.fault) != NULL);
}

static void test_acquisition_timeout_boundary(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, NULL);
    diagnostic_report_t report;

    push(&history, 2000, 25.0f, ESP_ERR_TIMEOUT, 1000000, 1000001);
    push(&history, 2000, 25.0f, ESP_ERR_TIMEOUT, 2000000, 2000001);
    CHECK(analyze(&engine, &history, 2000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);
    push(&history, 2000, 25.0f, ESP_ERR_TIMEOUT, 3000000, 3000001);
    CHECK(analyze(&engine, &history, 3000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE);
}

static void test_stale_timeout_and_exact_boundary(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_config_t config = test_config();
    config.stale_timeout_us = 5000000;
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;

    push(&history, 2000, 25.0f, ESP_OK, 1000000, 3000000);
    CHECK(analyze(&engine, &history, 5999999, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);
    CHECK(analyze(&engine, &history, 6000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_STALE_DATA);
    CHECK(report.stale_age_us == 5000000);
    CHECK(report.acquired_at_us == 1000000);
    CHECK(report.ingested_at_us == 3000000);
}

static void test_legacy_timestamp_fallback(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_config_t config = test_config();
    config.stale_timeout_us = 1000000;
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;
    sensor_sample_t legacy = make_sample(2000, 25.0f, ESP_OK,
                                         0, 1000000);
    legacy.acquisition_time_valid = false;
    CHECK(sensor_history_push(&history, &legacy));
    CHECK(analyze(&engine, &history, 1500000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);
    CHECK(analyze(&engine, &history, 2000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_STALE_DATA);
    CHECK(report.acquired_at_us == 1000000);
}

static void test_identical_fresh_measurements_are_not_stale(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, NULL);
    diagnostic_report_t report;

    push(&history, 2000, 25.0f, ESP_OK, 1000000, 1001000);
    push(&history, 2000, 25.0f, ESP_OK, 2000000, 2001000);
    push(&history, 2000, 25.0f, ESP_OK, 3000000, 3001000);
    CHECK(analyze(&engine, &history, 3500000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);
}

static void test_abnormal_temperature_change_at_threshold(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_config_t config = test_config();
    config.temperature_rate_threshold_c_per_second = 350.0;
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;

    push(&history, 2000, 20.0f, ESP_OK, 1000000, 1000001);
    push(&history, 2000, 55.0f, ESP_OK, 1100000, 1100001);
    CHECK(analyze(&engine, &history, 1100000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE);
    CHECK(report.elapsed_us == 100000);
    CHECK(fabs(report.temperature_delta_c - 35.0) < 0.001);
    CHECK(fabs(report.temperature_rate_c_per_second - 350.0) < 0.01);
    CHECK(report.recommended_action ==
          DIAGNOSTIC_ACTION_REPEAT_ACQUISITION);
}

static void test_normal_fluctuations_and_bad_timestamps(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_config_t config = test_config();
    config.temperature_rate_threshold_c_per_second = 100.0;
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;

    push(&history, 2000, 25.0f, ESP_OK, 1000000, 1000001);
    push(&history, 2000, 25.2f, ESP_OK, 2000000, 2000001);
    CHECK(analyze(&engine, &history, 2000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);

    sensor_history_init(&history);
    push(&history, 2000, 20.0f, ESP_OK, 3000000, 3000001);
    push(&history, 2000, 60.0f, ESP_OK, 2000000, 3000002);
    CHECK(analyze(&engine, &history, 3000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);

    sensor_history_init(&history);
    push(&history, 2000, 20.0f, ESP_OK, 0, 0);
    push(&history, 2000, 60.0f, ESP_OK, 0, 0);
    CHECK(analyze(&engine, &history, 1100000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);
}

static void test_ring_wraparound(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, NULL);
    diagnostic_report_t report;

    for (size_t i = 0; i < SENSOR_HISTORY_CAPACITY + 2; ++i) {
        const bool saturated = i >= SENSOR_HISTORY_CAPACITY - 1;
        push(&history, saturated ? 4095 : 2000, 25.0f, ESP_OK,
             (int64_t)(i + 1) * 1000000,
             (int64_t)(i + 1) * 1000000 + 1);
    }
    CHECK(history.count == SENSOR_HISTORY_CAPACITY);
    CHECK(analyze(&engine, &history, 20000000, &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_ADC_SATURATION);
    CHECK(report.raw_value_count == 3);
    CHECK(report.raw_values[0] == 4095);
}

static void test_report_changes_and_recovery(void)
{
    sensor_history_t history;
    sensor_history_init(&history);
    diagnostic_engine_t engine;
    diagnostic_engine_init(&engine, NULL);
    diagnostic_report_t report;

    push(&history, 4095, 25.0f, ESP_OK, 1000000, 1000001);
    push(&history, 4095, 25.0f, ESP_OK, 2000000, 2000001);
    push(&history, 4095, 25.0f, ESP_OK, 3000000, 3000001);
    CHECK(diagnostic_engine_update(&engine, &history, 3000000, false, 3,
                                   &report));
    CHECK(report.fault == DIAGNOSTIC_FAULT_ADC_SATURATION);
    CHECK(!diagnostic_engine_update(&engine, &history, 3100000, false, 4,
                                    &report));

    push(&history, 0, 25.0f, ESP_OK, 4000000, 4000001);
    push(&history, 0, 25.0f, ESP_OK, 5000000, 5000001);
    push(&history, 0, 25.0f, ESP_OK, 6000000, 6000001);
    CHECK(diagnostic_engine_update(&engine, &history, 6000000, false, 5,
                                    &report));

    push(&history, 2000, 25.0f, ESP_OK, 7000000, 7000001);
    push(&history, 2000, 25.0f, ESP_OK, 8000000, 8000001);
    CHECK(!diagnostic_engine_update(&engine, &history, 8000000, false, 6,
                                    &report));
    push(&history, 2000, 25.0f, ESP_OK, 9000000, 9000001);
    CHECK(diagnostic_engine_update(&engine, &history, 9000000, false, 7,
                                   &report));
    CHECK(report.recovered);
    CHECK(report.fault == DIAGNOSTIC_FAULT_NONE);
}

static void test_diagnostic_recommendations(void)
{
    CHECK(diagnostic_recommended_action(DIAGNOSTIC_FAULT_ADC_SATURATION) ==
          diagnostic_action_description(DIAGNOSTIC_ACTION_VERIFY_ADC));
    CHECK(diagnostic_recommended_action(
              DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA) ==
          diagnostic_action_description(
              DIAGNOSTIC_ACTION_INSPECT_ACQUISITION));
    CHECK(diagnostic_recommended_action(DIAGNOSTIC_FAULT_STALE_DATA) ==
          diagnostic_action_description(
              DIAGNOSTIC_ACTION_CHECK_SENSOR_UPDATES));
    CHECK(diagnostic_recommended_action(
              DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE) ==
          diagnostic_action_description(
              DIAGNOSTIC_ACTION_REPEAT_ACQUISITION));
    CHECK(diagnostic_fault_id(DIAGNOSTIC_FAULT_ADC_SATURATION) != NULL);
    CHECK(diagnostic_fault_name(DIAGNOSTIC_FAULT_STALE_DATA) != NULL);
}

static void test_invalid_arguments(void)
{
    diagnostic_config_t config;
    diagnostic_config_init(NULL);
    config = test_config();
    diagnostic_engine_t engine;
    diagnostic_engine_init(NULL, &config);
    diagnostic_engine_init(&engine, &config);
    diagnostic_report_t report;
    sensor_history_t history;
    sensor_history_init(&history);

    CHECK(!diagnostic_engine_analyze(NULL, &history, 1, false, 1, &report));
    CHECK(!diagnostic_engine_analyze(&engine, NULL, 1, false, 1, &report));
    CHECK(!diagnostic_engine_analyze(&engine, &history, 1, false, 1, NULL));
    CHECK(!diagnostic_engine_analyze(&engine, &history, -1, false, 1, &report));
    CHECK(!diagnostic_engine_update(NULL, &history, 1, false, 1, &report));
    CHECK(!diagnostic_engine_update(&engine, &history, 1, false, 1, NULL));

    config.consecutive_invalid_count = 0;
    diagnostic_engine_init(&engine, &config);
    CHECK(!diagnostic_engine_analyze(&engine, &history, 1, false, 1, &report));
}

int main(void)
{
    test_empty_and_normal_history();
    test_high_saturation_and_boundary();
    test_low_saturation_and_boundary();
    test_repeated_invalid_successful_measurements();
    test_communication_failures();
    test_acquisition_timeout_boundary();
    test_stale_timeout_and_exact_boundary();
    test_legacy_timestamp_fallback();
    test_identical_fresh_measurements_are_not_stale();
    test_abnormal_temperature_change_at_threshold();
    test_normal_fluctuations_and_bad_timestamps();
    test_ring_wraparound();
    test_report_changes_and_recovery();
    test_diagnostic_recommendations();
    test_invalid_arguments();

    if (failures != 0) {
        fprintf(stderr, "FAIL: %u of %u checks failed\n", failures, checks);
        return 1;
    }
    printf("PASS: diagnostic_engine (%u checks, 15 test groups)\n", checks);
    return 0;
}
