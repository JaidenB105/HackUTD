
#ifndef faultDetect_H
#define faultDetect_H

#include <stdbool.h>
#include <stdint.h>
#include "sensor.h"

typedef enum {
    HEALTH_HEALTHY,
    HEALTH_SUSPECT,
    HEALTH_FAILED
} health_state_t;

typedef enum {
    FAULT_NONE = 0,
    FAULT_ADC_READ = 100,
    FAULT_SENSOR_TIMEOUT = 101,
    FAULT_INVALID_DATA = 102,
    FAULT_OUT_OF_RANGE = 103
} fault_code_t;

typedef struct {
    health_state_t state;
    fault_code_t fault;
    uint32_t consecutive_failures;
    uint32_t consecutive_successes;
    uint32_t total_failures;
} faultDetect_t;

void faultDetect_init(faultDetect_t *detector);

// Returns true if the health state or fault code changed.
bool faultDetect_update(
    faultDetect_t *detector,
    const sensor_reading_t *reading
);

const char *fault_state_name(health_state_t state);
const char *fault_code_name(fault_code_t code);
const char *fault_description(fault_code_t code);
const char *fault_corrective_action(fault_code_t code);

#endif
