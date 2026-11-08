#ifndef DIAGNOSTIC_ENGINE_H
#define DIAGNOSTIC_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sensor_history.h"

#define DIAGNOSTIC_RAW_EVIDENCE_CAPACITY 3

typedef enum {
    DIAGNOSTIC_FAULT_NONE = 0,
    DIAGNOSTIC_FAULT_ADC_SATURATION,
    DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA,
    DIAGNOSTIC_FAULT_STALE_DATA,
    DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE,
    DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE
} diagnostic_fault_t;

typedef enum {
    DIAGNOSTIC_SEVERITY_INFO = 0,
    DIAGNOSTIC_SEVERITY_WARNING,
    DIAGNOSTIC_SEVERITY_ERROR,
    DIAGNOSTIC_SEVERITY_CRITICAL
} diagnostic_severity_t;

typedef enum {
    DIAGNOSTIC_COMPONENT_SENSOR = 0,
    DIAGNOSTIC_COMPONENT_ADC,
    DIAGNOSTIC_COMPONENT_ACQUISITION
} diagnostic_component_t;

typedef enum {
    DIAGNOSTIC_EVIDENCE_NONE = 0,
    DIAGNOSTIC_EVIDENCE_ADC_HIGH_SATURATION = 1U << 0,
    DIAGNOSTIC_EVIDENCE_ADC_LOW_SATURATION = 1U << 1,
    DIAGNOSTIC_EVIDENCE_ACQUISITION_ERROR = 1U << 2,
    DIAGNOSTIC_EVIDENCE_INVALID_VALUE = 1U << 3,
    DIAGNOSTIC_EVIDENCE_STALE_TIMESTAMP = 1U << 4,
    DIAGNOSTIC_EVIDENCE_TEMPERATURE_RATE = 1U << 5,
    DIAGNOSTIC_EVIDENCE_SIMULATED = 1U << 6
} diagnostic_evidence_flag_t;

typedef enum {
    DIAGNOSTIC_CAUSE_NONE = 0,
    DIAGNOSTIC_CAUSE_ADC_RANGE,
    DIAGNOSTIC_CAUSE_ADC_CONFIGURATION,
    DIAGNOSTIC_CAUSE_ABNORMAL_SENSOR_OUTPUT,
    DIAGNOSTIC_CAUSE_ACQUISITION_ERROR,
    DIAGNOSTIC_CAUSE_CONVERSION_ERROR,
    DIAGNOSTIC_CAUSE_SENSOR_NOISE,
    DIAGNOSTIC_CAUSE_ENVIRONMENTAL_CHANGE,
    DIAGNOSTIC_CAUSE_MISSING_UPDATE
} diagnostic_cause_t;

typedef enum {
    DIAGNOSTIC_ACTION_NONE = 0,
    DIAGNOSTIC_ACTION_VERIFY_ADC,
    DIAGNOSTIC_ACTION_INSPECT_ACQUISITION,
    DIAGNOSTIC_ACTION_CHECK_SENSOR_UPDATES,
    DIAGNOSTIC_ACTION_REPEAT_ACQUISITION
} diagnostic_action_t;

typedef struct {
    int adc_min_value;
    int adc_max_value;
    int adc_low_saturation_threshold;
    int adc_high_saturation_threshold;
    size_t consecutive_saturation_count;
    size_t consecutive_invalid_count;
    size_t consecutive_communication_failure_count;
    size_t consecutive_recovery_count;
    int64_t stale_timeout_us;
    double temperature_rate_threshold_c_per_second;
} diagnostic_config_t;

typedef struct {
    diagnostic_fault_t fault;
    diagnostic_severity_t severity;
    diagnostic_component_t component;
    uint32_t evidence_flags;
    size_t consecutive_count;
    int raw_values[DIAGNOSTIC_RAW_EVIDENCE_CAPACITY];
    size_t raw_value_count;
    int acquisition_status;
    int64_t acquired_at_us;
    int64_t ingested_at_us;
    int64_t elapsed_us;
    int64_t stale_age_us;
    double temperature_delta_c;
    double temperature_rate_c_per_second;
    diagnostic_cause_t possible_causes[3];
    size_t possible_cause_count;
    diagnostic_action_t recommended_action;
    bool diagnosis_confirmed;
    bool recovered;
    bool simulated;
    uint32_t cycle;
} diagnostic_report_t;

typedef struct {
    diagnostic_config_t config;
    diagnostic_fault_t active_fault;
    diagnostic_severity_t active_severity;
    uint32_t active_evidence_flags;
    int active_acquisition_status;
    bool active;
} diagnostic_engine_t;

void diagnostic_config_init(diagnostic_config_t *config);
void diagnostic_engine_init(
    diagnostic_engine_t *engine,
    const diagnostic_config_t *config
);

/* Analyze current history. now_us uses the same monotonic microsecond clock. */
bool diagnostic_engine_analyze(
    const diagnostic_engine_t *engine,
    const sensor_history_t *history,
    int64_t now_us,
    bool simulated,
    uint32_t cycle,
    diagnostic_report_t *report
);

/*
 * Returns true only for a new/changed diagnosis or verified recovery.
 * The report is populated on every valid call, including calls with no event.
 */
bool diagnostic_engine_update(
    diagnostic_engine_t *engine,
    const sensor_history_t *history,
    int64_t now_us,
    bool simulated,
    uint32_t cycle,
    diagnostic_report_t *report
);

const char *diagnostic_fault_id(diagnostic_fault_t fault);
const char *diagnostic_fault_name(diagnostic_fault_t fault);
const char *diagnostic_fault_description(diagnostic_fault_t fault);
const char *diagnostic_severity_name(diagnostic_severity_t severity);
const char *diagnostic_component_name(diagnostic_component_t component);
const char *diagnostic_cause_description(diagnostic_cause_t cause);
const char *diagnostic_action_description(diagnostic_action_t action);
const char *diagnostic_recommended_action(diagnostic_fault_t fault);

#endif
