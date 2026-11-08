#include "diagnostic_engine.h"

#include <math.h>
#include <string.h>

#include "esp_err.h"

static int64_t acquisition_time(const sensor_sample_t *sample)
{
    return sample->acquisition_time_valid
        ? sample->acquired_at_us
        : sample->timestamp_us;
}

static bool is_usable_reading(const sensor_sample_t *sample,
                              const diagnostic_config_t *config)
{
    return sample->reading.status == ESP_OK &&
           sample->reading.raw_adc >= config->adc_min_value &&
           sample->reading.raw_adc <= config->adc_max_value &&
           isfinite(sample->reading.temperature_c);
}

static bool is_saturated(const sensor_sample_t *sample,
                         const diagnostic_config_t *config,
                         uint32_t *flag)
{
    if (sample->reading.status != ESP_OK ||
        sample->reading.raw_adc < config->adc_min_value ||
        sample->reading.raw_adc > config->adc_max_value) {
        return false;
    }

    if (sample->reading.raw_adc <= config->adc_low_saturation_threshold) {
        *flag = DIAGNOSTIC_EVIDENCE_ADC_LOW_SATURATION;
        return true;
    }
    if (sample->reading.raw_adc >= config->adc_high_saturation_threshold) {
        *flag = DIAGNOSTIC_EVIDENCE_ADC_HIGH_SATURATION;
        return true;
    }
    return false;
}

static void add_cause(diagnostic_report_t *report,
                      diagnostic_cause_t cause)
{
    if (report->possible_cause_count <
        sizeof(report->possible_causes) / sizeof(report->possible_causes[0])) {
        report->possible_causes[report->possible_cause_count++] = cause;
    }
}

static void set_fault(diagnostic_report_t *report,
                      diagnostic_fault_t fault,
                      diagnostic_severity_t severity,
                      diagnostic_component_t component,
                      diagnostic_action_t action,
                      bool confirmed)
{
    report->fault = fault;
    report->severity = severity;
    report->component = component;
    report->recommended_action = action;
    report->diagnosis_confirmed = confirmed;
}

static void collect_recent_raw_values(const sensor_history_t *history,
                                      diagnostic_report_t *report,
                                      size_t count)
{
    if (count > DIAGNOSTIC_RAW_EVIDENCE_CAPACITY) {
        count = DIAGNOSTIC_RAW_EVIDENCE_CAPACITY;
    }

    for (size_t i = 0; i < count; ++i) {
        sensor_sample_t sample;
        if (!sensor_history_get_recent(history, count - i - 1, &sample)) {
            break;
        }
        report->raw_values[report->raw_value_count++] =
            sample.reading.raw_adc;
    }
}

void diagnostic_config_init(diagnostic_config_t *config)
{
    if (config == NULL) {
        return;
    }

    *config = (diagnostic_config_t) {
        .adc_min_value = 0,
        .adc_max_value = 4095,
        .adc_low_saturation_threshold = 16,
        .adc_high_saturation_threshold = 4079,
        .consecutive_saturation_count = 3,
        .consecutive_invalid_count = 3,
        .consecutive_communication_failure_count = 3,
        .consecutive_recovery_count = 3,
        .stale_timeout_us = INT64_C(5000000),
        .temperature_rate_threshold_c_per_second = 100.0
    };
}

void diagnostic_engine_init(diagnostic_engine_t *engine,
                            const diagnostic_config_t *config)
{
    if (engine == NULL) {
        return;
    }

    diagnostic_config_t defaults;
    diagnostic_config_init(&defaults);
    *engine = (diagnostic_engine_t) {
        .config = config != NULL ? *config : defaults,
        .active_fault = DIAGNOSTIC_FAULT_NONE,
        .active_severity = DIAGNOSTIC_SEVERITY_INFO,
        .active_evidence_flags = DIAGNOSTIC_EVIDENCE_NONE,
        .active_acquisition_status = ESP_OK,
        .active = false
    };
}

bool diagnostic_engine_analyze(const diagnostic_engine_t *engine,
                               const sensor_history_t *history,
                               int64_t now_us,
                               bool simulated,
                               uint32_t cycle,
                               diagnostic_report_t *report)
{
    if (engine == NULL || history == NULL || report == NULL ||
        now_us < 0 || engine->config.consecutive_saturation_count == 0 ||
        engine->config.consecutive_invalid_count == 0 ||
        engine->config.consecutive_communication_failure_count == 0 ||
        engine->config.consecutive_recovery_count == 0 ||
        engine->config.stale_timeout_us <= 0 ||
        engine->config.adc_min_value >= engine->config.adc_max_value ||
        engine->config.adc_low_saturation_threshold <
            engine->config.adc_min_value ||
        engine->config.adc_high_saturation_threshold >
            engine->config.adc_max_value ||
        engine->config.adc_low_saturation_threshold >=
            engine->config.adc_high_saturation_threshold ||
        !isfinite(engine->config.temperature_rate_threshold_c_per_second) ||
        engine->config.temperature_rate_threshold_c_per_second <= 0.0) {
        return false;
    }

    *report = (diagnostic_report_t) {
        .fault = DIAGNOSTIC_FAULT_NONE,
        .severity = DIAGNOSTIC_SEVERITY_INFO,
        .component = DIAGNOSTIC_COMPONENT_SENSOR,
        .simulated = simulated,
        .cycle = cycle
    };

    if (history->count == 0 || history->count > SENSOR_HISTORY_CAPACITY) {
        return true;
    }

    sensor_sample_t newest;
    (void)sensor_history_get_recent(history, 0, &newest);
    report->acquired_at_us = acquisition_time(&newest);
    report->ingested_at_us = newest.timestamp_us;
    report->acquisition_status = newest.reading.status;
    report->simulated = report->simulated || newest.simulated;

    size_t run = 0;
    for (size_t i = 0; i < history->count; ++i) {
        sensor_sample_t sample;
        (void)sensor_history_get_recent(history, i, &sample);
        if (sample.reading.status == ESP_OK) {
            break;
        }
        ++run;
        report->simulated = report->simulated || sample.simulated;
        if (i == 0) {
            report->acquisition_status = sample.reading.status;
        }
    }
    if (run >= engine->config.consecutive_communication_failure_count) {
        report->consecutive_count = run;
        report->evidence_flags |= DIAGNOSTIC_EVIDENCE_ACQUISITION_ERROR;
        set_fault(report, DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE,
                  DIAGNOSTIC_SEVERITY_ERROR,
                  DIAGNOSTIC_COMPONENT_ACQUISITION,
                  DIAGNOSTIC_ACTION_INSPECT_ACQUISITION, true);
        add_cause(report, DIAGNOSTIC_CAUSE_ACQUISITION_ERROR);
        if (report->simulated) {
            report->evidence_flags |= DIAGNOSTIC_EVIDENCE_SIMULATED;
        }
        return true;
    }

    run = 0;
    for (size_t i = 0; i < history->count; ++i) {
        sensor_sample_t sample;
        uint32_t saturation_flag = 0;
        (void)sensor_history_get_recent(history, i, &sample);
        if (!is_saturated(&sample, &engine->config, &saturation_flag)) {
            break;
        }
        ++run;
        report->simulated = report->simulated || sample.simulated;
        if (i == 0) {
            report->evidence_flags |= saturation_flag;
        }
    }
    if (run >= engine->config.consecutive_saturation_count) {
        report->consecutive_count = run;
        collect_recent_raw_values(history, report, run);
        set_fault(report, DIAGNOSTIC_FAULT_ADC_SATURATION,
                  DIAGNOSTIC_SEVERITY_WARNING, DIAGNOSTIC_COMPONENT_SENSOR,
                  DIAGNOSTIC_ACTION_VERIFY_ADC, true);
        add_cause(report, DIAGNOSTIC_CAUSE_ADC_RANGE);
        add_cause(report, DIAGNOSTIC_CAUSE_ADC_CONFIGURATION);
        add_cause(report, DIAGNOSTIC_CAUSE_ABNORMAL_SENSOR_OUTPUT);
        if (report->simulated) {
            report->evidence_flags |= DIAGNOSTIC_EVIDENCE_SIMULATED;
        }
        return true;
    }

    run = 0;
    for (size_t i = 0; i < history->count; ++i) {
        sensor_sample_t sample;
        (void)sensor_history_get_recent(history, i, &sample);
        if (sample.reading.status != ESP_OK) {
            break;
        }
        if (is_usable_reading(&sample, &engine->config)) {
            break;
        }
        ++run;
        report->simulated = report->simulated || sample.simulated;
    }
    if (run >= engine->config.consecutive_invalid_count) {
        report->consecutive_count = run;
        report->evidence_flags |= DIAGNOSTIC_EVIDENCE_INVALID_VALUE;
        collect_recent_raw_values(history, report, run);
        set_fault(report, DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA,
                  DIAGNOSTIC_SEVERITY_WARNING, DIAGNOSTIC_COMPONENT_SENSOR,
                  DIAGNOSTIC_ACTION_INSPECT_ACQUISITION, true);
        add_cause(report, DIAGNOSTIC_CAUSE_CONVERSION_ERROR);
        add_cause(report, DIAGNOSTIC_CAUSE_ABNORMAL_SENSOR_OUTPUT);
        if (report->simulated) {
            report->evidence_flags |= DIAGNOSTIC_EVIDENCE_SIMULATED;
        }
        return true;
    }

    sensor_sample_t latest_valid = {0};
    bool have_latest_valid = false;
    for (size_t i = 0; i < history->count; ++i) {
        sensor_sample_t sample;
        (void)sensor_history_get_recent(history, i, &sample);
        if (is_usable_reading(&sample, &engine->config)) {
            latest_valid = sample;
            have_latest_valid = true;
            break;
        }
    }
    if (have_latest_valid) {
        const int64_t latest_time = acquisition_time(&latest_valid);
        if (latest_time > 0 && latest_time <= now_us &&
            now_us - latest_time >= engine->config.stale_timeout_us) {
            report->acquired_at_us = latest_time;
            report->ingested_at_us = latest_valid.timestamp_us;
            report->acquisition_status = latest_valid.reading.status;
            report->stale_age_us = now_us - latest_time;
            report->simulated = report->simulated || latest_valid.simulated;
            report->evidence_flags |= DIAGNOSTIC_EVIDENCE_STALE_TIMESTAMP;
            set_fault(report, DIAGNOSTIC_FAULT_STALE_DATA,
                      DIAGNOSTIC_SEVERITY_WARNING,
                      DIAGNOSTIC_COMPONENT_SENSOR,
                      DIAGNOSTIC_ACTION_CHECK_SENSOR_UPDATES, true);
            add_cause(report, DIAGNOSTIC_CAUSE_MISSING_UPDATE);
            if (report->simulated) {
                report->evidence_flags |= DIAGNOSTIC_EVIDENCE_SIMULATED;
            }
            return true;
        }
    }

    sensor_sample_t current_valid = {0};
    sensor_sample_t previous_valid = {0};
    size_t valid_count = 0;
    for (size_t i = 0; i < history->count; ++i) {
        sensor_sample_t sample;
        (void)sensor_history_get_recent(history, i, &sample);
        if (!is_usable_reading(&sample, &engine->config)) {
            continue;
        }
        if (valid_count == 0) {
            current_valid = sample;
        } else {
            previous_valid = sample;
            valid_count++;
            break;
        }
        ++valid_count;
    }
    if (valid_count >= 2) {
        const int64_t current_time = acquisition_time(&current_valid);
        const int64_t previous_time = acquisition_time(&previous_valid);
        if (previous_time > 0 && current_time > previous_time &&
            current_time <= now_us) {
            const int64_t elapsed_us = current_time - previous_time;
            const double delta = fabs((double)current_valid.reading.temperature_c -
                                      (double)previous_valid.reading.temperature_c);
            const double rate = delta * 1000000.0 / (double)elapsed_us;
            if (rate >=
                engine->config.temperature_rate_threshold_c_per_second) {
                report->acquired_at_us = current_time;
                report->ingested_at_us = current_valid.timestamp_us;
                report->elapsed_us = elapsed_us;
                report->temperature_delta_c = delta;
                report->temperature_rate_c_per_second = rate;
                report->evidence_flags |=
                    DIAGNOSTIC_EVIDENCE_TEMPERATURE_RATE;
                report->simulated = report->simulated ||
                    current_valid.simulated || previous_valid.simulated;
                set_fault(report,
                          DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE,
                          DIAGNOSTIC_SEVERITY_WARNING,
                          DIAGNOSTIC_COMPONENT_SENSOR,
                          DIAGNOSTIC_ACTION_REPEAT_ACQUISITION, true);
                add_cause(report, DIAGNOSTIC_CAUSE_SENSOR_NOISE);
                add_cause(report, DIAGNOSTIC_CAUSE_CONVERSION_ERROR);
                add_cause(report, DIAGNOSTIC_CAUSE_ENVIRONMENTAL_CHANGE);
                if (report->simulated) {
                    report->evidence_flags |= DIAGNOSTIC_EVIDENCE_SIMULATED;
                }
                return true;
            }
        }
    }

    if (report->simulated) {
        report->evidence_flags |= DIAGNOSTIC_EVIDENCE_SIMULATED;
    }
    return true;
}

bool diagnostic_engine_update(diagnostic_engine_t *engine,
                              const sensor_history_t *history,
                              int64_t now_us,
                              bool simulated,
                              uint32_t cycle,
                              diagnostic_report_t *report)
{
    if (engine == NULL || report == NULL ||
        !diagnostic_engine_analyze(engine, history, now_us, simulated,
                                  cycle, report)) {
        return false;
    }

    bool changed = false;
    if (report->fault == DIAGNOSTIC_FAULT_NONE) {
        if (engine->active) {
            size_t recovery_count = 0;
            int64_t previous_time = 0;
            for (size_t i = 0; i < history->count &&
                               recovery_count <
                                   engine->config.consecutive_recovery_count;
                 ++i) {
                sensor_sample_t sample;
                (void)sensor_history_get_recent(history, i, &sample);
                uint32_t saturation_flag = 0;
                const int64_t current_time = acquisition_time(&sample);
                if (!is_usable_reading(&sample, &engine->config) ||
                    is_saturated(&sample, &engine->config,
                                 &saturation_flag) ||
                    current_time <= 0 ||
                    (previous_time != 0 && current_time >= previous_time)) {
                    break;
                }
                previous_time = current_time;
                ++recovery_count;
            }
            if (recovery_count >= engine->config.consecutive_recovery_count) {
                report->recovered = true;
                report->severity = DIAGNOSTIC_SEVERITY_INFO;
                changed = true;
                engine->active = false;
                engine->active_fault = DIAGNOSTIC_FAULT_NONE;
                engine->active_severity = DIAGNOSTIC_SEVERITY_INFO;
                engine->active_evidence_flags = DIAGNOSTIC_EVIDENCE_NONE;
                engine->active_acquisition_status = ESP_OK;
            }
        } else {
            engine->active_fault = DIAGNOSTIC_FAULT_NONE;
            engine->active_severity = DIAGNOSTIC_SEVERITY_INFO;
            engine->active_evidence_flags = DIAGNOSTIC_EVIDENCE_NONE;
            engine->active_acquisition_status = ESP_OK;
        }
        return changed;
    }

    changed = !engine->active ||
              engine->active_fault != report->fault ||
              engine->active_severity != report->severity ||
              engine->active_evidence_flags !=
                  (report->evidence_flags & ~DIAGNOSTIC_EVIDENCE_SIMULATED) ||
              (report->fault == DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE &&
               engine->active_acquisition_status !=
                   report->acquisition_status);
    engine->active = true;
    engine->active_fault = report->fault;
    engine->active_severity = report->severity;
    engine->active_evidence_flags =
        report->evidence_flags & ~DIAGNOSTIC_EVIDENCE_SIMULATED;
    engine->active_acquisition_status = report->acquisition_status;
    return changed;
}

const char *diagnostic_fault_id(diagnostic_fault_t fault)
{
    switch (fault) {
    case DIAGNOSTIC_FAULT_ADC_SATURATION: return "F201";
    case DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA: return "F202";
    case DIAGNOSTIC_FAULT_STALE_DATA: return "F203";
    case DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE: return "F204";
    case DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE: return "F205";
    case DIAGNOSTIC_FAULT_NONE:
    default: return "F000";
    }
}

const char *diagnostic_fault_name(diagnostic_fault_t fault)
{
    switch (fault) {
    case DIAGNOSTIC_FAULT_ADC_SATURATION: return "ADC_SATURATION";
    case DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA:
        return "REPEATED_INVALID_DATA";
    case DIAGNOSTIC_FAULT_STALE_DATA: return "STALE_DATA";
    case DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE:
        return "ABNORMAL_TEMPERATURE_CHANGE";
    case DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE:
        return "COMMUNICATION_FAILURE";
    case DIAGNOSTIC_FAULT_NONE:
    default: return "NONE";
    }
}

const char *diagnostic_fault_description(diagnostic_fault_t fault)
{
    switch (fault) {
    case DIAGNOSTIC_FAULT_ADC_SATURATION:
        return "Consecutive valid ADC readings are near a measurement limit.";
    case DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA:
        return "Consecutive successful acquisitions contain unusable values.";
    case DIAGNOSTIC_FAULT_STALE_DATA:
        return "No recent valid acquisition is present within the configured timeout.";
    case DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE:
        return "Temperature changed faster than the configured rate threshold.";
    case DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE:
        return "Consecutive acquisition attempts reported errors; physical disconnection is unconfirmed.";
    case DIAGNOSTIC_FAULT_NONE:
    default:
        return "No historical diagnostic fault is currently identified.";
    }
}

const char *diagnostic_severity_name(diagnostic_severity_t severity)
{
    switch (severity) {
    case DIAGNOSTIC_SEVERITY_INFO: return "INFO";
    case DIAGNOSTIC_SEVERITY_WARNING: return "WARNING";
    case DIAGNOSTIC_SEVERITY_ERROR: return "ERROR";
    case DIAGNOSTIC_SEVERITY_CRITICAL: return "CRITICAL";
    default: return "UNKNOWN";
    }
}

const char *diagnostic_component_name(diagnostic_component_t component)
{
    switch (component) {
    case DIAGNOSTIC_COMPONENT_SENSOR: return "NTC Temperature Sensor";
    case DIAGNOSTIC_COMPONENT_ADC: return "ADC";
    case DIAGNOSTIC_COMPONENT_ACQUISITION: return "Sensor Acquisition";
    default: return "Unknown Component";
    }
}

const char *diagnostic_cause_description(diagnostic_cause_t cause)
{
    switch (cause) {
    case DIAGNOSTIC_CAUSE_ADC_RANGE:
        return "Analog input outside configured ADC measurement range.";
    case DIAGNOSTIC_CAUSE_ADC_CONFIGURATION:
        return "Incorrect ADC configuration or attenuation.";
    case DIAGNOSTIC_CAUSE_ABNORMAL_SENSOR_OUTPUT:
        return "Abnormal sensor output.";
    case DIAGNOSTIC_CAUSE_ACQUISITION_ERROR:
        return "Acquisition timeout or driver error.";
    case DIAGNOSTIC_CAUSE_CONVERSION_ERROR:
        return "Sensor data conversion error.";
    case DIAGNOSTIC_CAUSE_SENSOR_NOISE:
        return "Sensor noise.";
    case DIAGNOSTIC_CAUSE_ENVIRONMENTAL_CHANGE:
        return "Unexpected environmental change.";
    case DIAGNOSTIC_CAUSE_MISSING_UPDATE:
        return "Expected sensor acquisition update is missing.";
    case DIAGNOSTIC_CAUSE_NONE:
    default:
        return "No possible cause specified.";
    }
}

const char *diagnostic_action_description(diagnostic_action_t action)
{
    switch (action) {
    case DIAGNOSTIC_ACTION_VERIFY_ADC:
        return "Verify ADC attenuation, voltage-divider wiring, and sensor output voltage.";
    case DIAGNOSTIC_ACTION_INSPECT_ACQUISITION:
        return "Inspect sensor acquisition, ADC configuration, and conversion logic.";
    case DIAGNOSTIC_ACTION_CHECK_SENSOR_UPDATES:
        return "Check the acquisition schedule and verify that fresh sensor updates arrive.";
    case DIAGNOSTIC_ACTION_REPEAT_ACQUISITION:
        return "Repeat acquisition and compare subsequent measurements.";
    case DIAGNOSTIC_ACTION_NONE:
    default:
        return "No corrective action is required.";
    }
}

const char *diagnostic_recommended_action(diagnostic_fault_t fault)
{
    switch (fault) {
    case DIAGNOSTIC_FAULT_ADC_SATURATION:
        return diagnostic_action_description(DIAGNOSTIC_ACTION_VERIFY_ADC);
    case DIAGNOSTIC_FAULT_REPEATED_INVALID_DATA:
    case DIAGNOSTIC_FAULT_COMMUNICATION_FAILURE:
        return diagnostic_action_description(
            DIAGNOSTIC_ACTION_INSPECT_ACQUISITION);
    case DIAGNOSTIC_FAULT_STALE_DATA:
        return diagnostic_action_description(
            DIAGNOSTIC_ACTION_CHECK_SENSOR_UPDATES);
    case DIAGNOSTIC_FAULT_ABNORMAL_TEMPERATURE_CHANGE:
        return diagnostic_action_description(
            DIAGNOSTIC_ACTION_REPEAT_ACQUISITION);
    case DIAGNOSTIC_FAULT_NONE:
    default:
        return diagnostic_action_description(DIAGNOSTIC_ACTION_NONE);
    }
}
