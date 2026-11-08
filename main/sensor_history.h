
#ifndef SENSOR_HISTORY_H
#define SENSOR_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sensor.h"

#define SENSOR_HISTORY_CAPACITY 16

typedef struct {
    sensor_reading_t reading;
    /* Time the sample was added to history; retained for compatibility. */
    int64_t timestamp_us;
    /* Time the acquisition completed. */
    int64_t acquired_at_us;
    /* False identifies legacy samples that only have timestamp_us. */
    bool acquisition_time_valid;
    bool simulated;
} sensor_sample_t;

typedef struct {
    sensor_sample_t samples[SENSOR_HISTORY_CAPACITY];
    size_t head;
    size_t count;
} sensor_history_t;

void sensor_history_init(sensor_history_t *history);

bool sensor_history_push(
    sensor_history_t *history,
    const sensor_sample_t *sample
);

bool sensor_history_get_recent(
    const sensor_history_t *history,
    size_t offset,
    sensor_sample_t *out
);

#endif
