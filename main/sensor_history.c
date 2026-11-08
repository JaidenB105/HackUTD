
#include "sensor_history.h"

void sensor_history_init(sensor_history_t *history)
{
    if (history == NULL) {
        return;
    }

    *history = (sensor_history_t){0};
}

bool sensor_history_push(
    sensor_history_t *history,
    const sensor_sample_t *sample)
{
    if (history == NULL || sample == NULL) {
        return false;
    }

    history->samples[history->head] = *sample;

    history->head =
        (history->head + 1) % SENSOR_HISTORY_CAPACITY;

    if (history->count < SENSOR_HISTORY_CAPACITY) {
        history->count++;
    }

    return true;
}

bool sensor_history_get_recent(
    const sensor_history_t *history,
    size_t offset,
    sensor_sample_t *out)
{
    if (history == NULL || out == NULL ||
        offset >= history->count) {
        return false;
        }

    size_t index =
        (history->head + SENSOR_HISTORY_CAPACITY
         - 1 - offset) % SENSOR_HISTORY_CAPACITY;

    *out = history->samples[index];
    return true;
}
