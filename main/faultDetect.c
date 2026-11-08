#include "faultDetect.h"

#include <math.h>
#include <stddef.h>

#define FAILURES_BEFORE_FAILED 3U
#define SUCCESSES_BEFORE_RECOVERY 3U
#define MIN_TEMPERATURE_C (-20.0f)
#define MAX_TEMPERATURE_C 100.0f

static fault_code_t classify_reading(const sensor_reading_t *reading)
{
    if (reading->status == ESP_ERR_TIMEOUT) {
        return FAULT_SENSOR_TIMEOUT;
    }

    if (reading->status == ESP_ERR_INVALID_RESPONSE) {
        return FAULT_INVALID_DATA;
    }

    if (reading->status != ESP_OK) {
        return FAULT_ADC_READ;
    }

    if (reading->raw_adc <= 0 || reading->raw_adc >= 4095 ||
        !isfinite(reading->temperature_c)) {
        return FAULT_INVALID_DATA;
    }

    if (reading->temperature_c < MIN_TEMPERATURE_C ||
        reading->temperature_c > MAX_TEMPERATURE_C) {
        return FAULT_OUT_OF_RANGE;
    }

    return FAULT_NONE;
}

void faultDetect_init(faultDetect_t *detector)
{
    if (detector == NULL) {
        return;
    }

    detector->state = HEALTH_HEALTHY;
    detector->fault = FAULT_NONE;
    detector->consecutive_failures = 0;
    detector->consecutive_successes = 0;
    detector->total_failures = 0;
}

bool faultDetect_update(
    faultDetect_t *detector,
    const sensor_reading_t *reading)
{
    if (detector == NULL || reading == NULL) {
        return false;
    }

    const health_state_t previous_state = detector->state;
    const fault_code_t previous_fault = detector->fault;
    const fault_code_t current_fault = classify_reading(reading);

    if (current_fault != FAULT_NONE) {
        if (detector->consecutive_failures < UINT32_MAX) {
            detector->consecutive_failures++;
        }
        if (detector->total_failures < UINT32_MAX) {
            detector->total_failures++;
        }
        detector->consecutive_successes = 0;
        detector->fault = current_fault;
        detector->state =
            previous_state == HEALTH_FAILED ||
                    detector->consecutive_failures >= FAILURES_BEFORE_FAILED
                ? HEALTH_FAILED
                : HEALTH_SUSPECT;
    } else {
        detector->consecutive_failures = 0;
        if (detector->consecutive_successes < UINT32_MAX) {
            detector->consecutive_successes++;
        }

        if (detector->state != HEALTH_HEALTHY &&
            detector->consecutive_successes >= SUCCESSES_BEFORE_RECOVERY) {
            detector->state = HEALTH_HEALTHY;
            detector->fault = FAULT_NONE;
        }
    }

    return detector->state != previous_state ||
           detector->fault != previous_fault;
}

const char *fault_state_name(health_state_t state)
{
    switch (state) {
    case HEALTH_HEALTHY:
        return "HEALTHY";
    case HEALTH_SUSPECT:
        return "SUSPECT";
    case HEALTH_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

const char *fault_code_name(fault_code_t code)
{
    switch (code) {
    case FAULT_NONE:
        return "F000 - No fault";
    case FAULT_ADC_READ:
        return "F100 - ADC read failure";
    case FAULT_SENSOR_TIMEOUT:
        return "F101 - Sensor timeout";
    case FAULT_INVALID_DATA:
        return "F102 - Invalid sensor data";
    case FAULT_OUT_OF_RANGE:
        return "F103 - Temperature out of range";
    default:
        return "F999 - Unknown fault";
    }
}

const char *fault_description(fault_code_t code)
{
    switch (code) {
    case FAULT_NONE:
        return "Sensor readings are valid and within the expected range.";
    case FAULT_ADC_READ:
        return "The ADC could not acquire a sensor reading.";
    case FAULT_SENSOR_TIMEOUT:
        return "The sensor acquisition timed out.";
    case FAULT_INVALID_DATA:
        return "The sensor returned invalid or non-finite data.";
    case FAULT_OUT_OF_RANGE:
        return "The measured temperature is outside the expected range.";
    default:
        return "The fault code is not recognized.";
    }
}

const char *fault_corrective_action(fault_code_t code)
{
    switch (code) {
    case FAULT_NONE:
        return "No corrective action is required.";
    case FAULT_ADC_READ:
        return "Check the ADC configuration, sensor wiring, and power.";
    case FAULT_SENSOR_TIMEOUT:
        return "Check the sensor connection and retry the acquisition.";
    case FAULT_INVALID_DATA:
        return "Check the sensor output and ADC input for invalid values.";
    case FAULT_OUT_OF_RANGE:
        return "Verify the sensor placement and confirm the temperature.";
    default:
        return "Inspect the sensor and system diagnostics.";
    }
}
