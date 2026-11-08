#ifndef SENSOR_H
#define SENSOR_H

#include "esp_err.h"

typedef struct {
    int raw_adc;
    float temperature_c;
    esp_err_t status;
} sensor_reading_t;

esp_err_t sensor_init(void);
sensor_reading_t sensor_read(void);

#endif