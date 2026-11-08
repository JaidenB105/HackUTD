
#include <math.h>
#include "sensor.h"
#include "esp_adc/adc_oneshot.h"

#define SENSOR_CHANNEL ADC_CHANNEL_3

#define ADC_MAX 4095.0f
#define NTC_BETA 3950.0f
#define NTC_T0 298.15f

static adc_oneshot_unit_handle_t adc_handle;

esp_err_t sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };

    esp_err_t err = adc_oneshot_new_unit(
        &init_cfg, &adc_handle
    );

    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t channel_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12
    };

    return adc_oneshot_config_channel(
        adc_handle, SENSOR_CHANNEL, &channel_cfg
    );
}

sensor_reading_t sensor_read(void)
{
    sensor_reading_t reading = {0};

    reading.status = adc_oneshot_read(
        adc_handle,
        SENSOR_CHANNEL,
        &reading.raw_adc
    );

    if (reading.status != ESP_OK) {
        return reading;
    }

    int raw = reading.raw_adc;

    // Avoid division by zero or logarithm errors.
    if (raw <= 0 || raw >= 4095) {
        reading.status = ESP_ERR_INVALID_RESPONSE;
        return reading;
    }

    // Initial approximate conversion for Wokwi.
    float ratio = (float)raw / (ADC_MAX - raw);

    float kelvin = 1.0f /
        (logf(ratio) / NTC_BETA + 1.0f / NTC_T0);

    reading.temperature_c = kelvin - 273.15f;

    if (!isfinite(reading.temperature_c)) {
        reading.status = ESP_ERR_INVALID_RESPONSE;
    }

    return reading;
}
