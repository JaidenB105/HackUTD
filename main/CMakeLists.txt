idf_component_register(
        SRCS
        "main.c"
        "sensor.c"
        "faultDetect.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES
        esp_adc
        esp_timer
)