
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "faultDetect.h"

// ---------- Test infrastructure ----------

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(condition) do {              \
    if (!(condition)) {                           \
        printf("  FAIL at line %d: %s\n",          \
               __LINE__, #condition);             \
        return false;                             \
    }                                             \
} while (0)

#define ASSERT_EQ(expected, actual) \
    ASSERT_TRUE((expected) == (actual))

#define RUN_TEST(fn) do {                         \
    tests_run++;                                  \
    printf("[TEST] %s\n", #fn);                    \
    if (fn()) {                                   \
        tests_passed++;                           \
        printf("  PASS\n");                       \
    }                                             \
} while (0)

// ---------- Sensor injection helpers ----------

static sensor_reading_t valid_reading(float temp)
{
    sensor_reading_t r = {
        .raw_adc = 2000,
        .temperature_c = temp,
        .status = ESP_OK
    };
    return r;
}

static sensor_reading_t error_reading(esp_err_t error)
{
    sensor_reading_t r = {
        .raw_adc = 0,
        .temperature_c = 0,
        .status = error
    };
    return r;
}

static void send_reading(
    faultDetect_t *detector,
    sensor_reading_t reading)
{
    faultDetect_update(detector, &reading);
}

// ---------- Test cases ----------

// FT-01: initialization
static bool test_initial_state(void)
{
    faultDetect_t detector;
    faultDetect_init(&detector);

    ASSERT_EQ(HEALTH_HEALTHY, detector.state);
    ASSERT_EQ(FAULT_NONE, detector.fault);
    ASSERT_EQ(0u, detector.total_failures);
    ASSERT_EQ(0u, detector.consecutive_failures);
    ASSERT_EQ(0u, detector.consecutive_successes);

    return true;
}

// FT-02: normal readings
static bool test_normal_readings(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    for (int i = 0; i < 10; i++) {
        sensor_reading_t r = valid_reading(24.5f);
        bool changed = faultDetect_update(&d, &r);

        ASSERT_TRUE(!changed);
        ASSERT_EQ(HEALTH_HEALTHY, d.state);
        ASSERT_EQ(FAULT_NONE, d.fault);
    }

    ASSERT_EQ(0u, d.total_failures);
    return true;
}

// FT-03: first timeout
static bool test_first_timeout(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    sensor_reading_t r = error_reading(ESP_ERR_TIMEOUT);
    bool changed = faultDetect_update(&d, &r);

    ASSERT_TRUE(changed);
    ASSERT_EQ(HEALTH_SUSPECT, d.state);
    ASSERT_EQ(FAULT_SENSOR_TIMEOUT, d.fault);
    ASSERT_EQ(1u, d.consecutive_failures);

    return true;
}

// FT-04: three consecutive timeouts
static bool test_three_timeouts(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    for (int i = 0; i < 3; i++) {
        send_reading(&d, error_reading(ESP_ERR_TIMEOUT));
    }

    ASSERT_EQ(HEALTH_FAILED, d.state);
    ASSERT_EQ(FAULT_SENSOR_TIMEOUT, d.fault);
    ASSERT_EQ(3u, d.consecutive_failures);
    ASSERT_EQ(3u, d.total_failures);

    return true;
}

// FT-05: generic ADC failure
static bool test_adc_error(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    send_reading(&d, error_reading(ESP_FAIL));

    ASSERT_EQ(HEALTH_SUSPECT, d.state);
    ASSERT_EQ(FAULT_ADC_READ, d.fault);

    return true;
}

// FT-06: invalid response
static bool test_invalid_data(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    send_reading(&d,
        error_reading(ESP_ERR_INVALID_RESPONSE));

    ASSERT_EQ(FAULT_INVALID_DATA, d.fault);
    return true;
}

// FT-07: excessive temperature
static bool test_high_temperature(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    send_reading(&d, valid_reading(125.0f));

    ASSERT_EQ(FAULT_OUT_OF_RANGE, d.fault);
    ASSERT_EQ(HEALTH_SUSPECT, d.state);

    return true;
}

// FT-08: low temperature
static bool test_low_temperature(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    send_reading(&d, valid_reading(-30.0f));

    ASSERT_EQ(FAULT_OUT_OF_RANGE, d.fault);
    return true;
}

// FT-09: NaN
static bool test_nan_temperature(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    send_reading(&d, valid_reading(NAN));

    ASSERT_EQ(FAULT_INVALID_DATA, d.fault);
    return true;
}

// FT-10/11: recovery threshold
static bool test_recovery_threshold(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    for (int i = 0; i < 3; i++) {
        send_reading(&d, error_reading(ESP_ERR_TIMEOUT));
    }

    ASSERT_EQ(HEALTH_FAILED, d.state);

    for (int i = 0; i < 2; i++) {
        send_reading(&d, valid_reading(25.0f));
        ASSERT_EQ(HEALTH_FAILED, d.state);
    }

    send_reading(&d, valid_reading(25.0f));

    ASSERT_EQ(HEALTH_HEALTHY, d.state);
    ASSERT_EQ(FAULT_NONE, d.fault);
    ASSERT_EQ(3u, d.consecutive_successes);

    return true;
}

// FT-12: recovery interrupted by another fault
static bool test_interrupted_recovery(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    for (int i = 0; i < 3; i++) {
        send_reading(&d, error_reading(ESP_ERR_TIMEOUT));
    }

    send_reading(&d, valid_reading(25.0f));
    send_reading(&d, valid_reading(25.0f));

    ASSERT_EQ(2u, d.consecutive_successes);

    send_reading(&d, error_reading(ESP_FAIL));

    ASSERT_EQ(HEALTH_FAILED, d.state);
    ASSERT_EQ(0u, d.consecutive_successes);
    ASSERT_EQ(FAULT_ADC_READ, d.fault);

    return true;
}

// FT-13: alternating fault types
static bool test_mixed_failures(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    send_reading(&d, error_reading(ESP_ERR_TIMEOUT));
    send_reading(&d, error_reading(ESP_FAIL));
    send_reading(&d,
        error_reading(ESP_ERR_INVALID_RESPONSE));

    ASSERT_EQ(HEALTH_FAILED, d.state);
    ASSERT_EQ(FAULT_INVALID_DATA, d.fault);
    ASSERT_EQ(3u, d.total_failures);

    return true;
}

// FT-14: corrective-action lookup
static bool test_corrective_actions(void)
{
    ASSERT_TRUE(
        strlen(fault_corrective_action(
            FAULT_ADC_READ)) > 0);

    ASSERT_TRUE(
        strlen(fault_corrective_action(
            FAULT_SENSOR_TIMEOUT)) > 0);

    ASSERT_TRUE(
        strlen(fault_corrective_action(
            FAULT_INVALID_DATA)) > 0);

    ASSERT_TRUE(
        strlen(fault_corrective_action(
            FAULT_OUT_OF_RANGE)) > 0);

    ASSERT_TRUE(
        strstr(fault_code_name(
            FAULT_SENSOR_TIMEOUT), "F101") != NULL);

    return true;
}

// FT-15: cumulative failure accounting
static bool test_failure_history(void)
{
    faultDetect_t d;
    faultDetect_init(&d);

    send_reading(&d, error_reading(ESP_FAIL));
    send_reading(&d, valid_reading(25.0f));
    send_reading(&d, error_reading(ESP_FAIL));
    send_reading(&d, valid_reading(25.0f));
    send_reading(&d, error_reading(ESP_FAIL));

    ASSERT_EQ(3u, d.total_failures);
    ASSERT_EQ(1u, d.consecutive_failures);
    ASSERT_EQ(HEALTH_SUSPECT, d.state);

    return true;
}

// ---------- Runner ----------

int main(void)
{
    RUN_TEST(test_initial_state);
    RUN_TEST(test_normal_readings);
    RUN_TEST(test_first_timeout);
    RUN_TEST(test_three_timeouts);
    RUN_TEST(test_adc_error);
    RUN_TEST(test_invalid_data);
    RUN_TEST(test_high_temperature);
    RUN_TEST(test_low_temperature);
    RUN_TEST(test_nan_temperature);
    RUN_TEST(test_recovery_threshold);
    RUN_TEST(test_interrupted_recovery);
    RUN_TEST(test_mixed_failures);
    RUN_TEST(test_corrective_actions);
    RUN_TEST(test_failure_history);

    printf("\n================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n",
           tests_run - tests_passed);
    printf("================================\n");

    return tests_run == tests_passed
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
