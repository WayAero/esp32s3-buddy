idf_component_register(
    SRCS "src/example_status.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_desktop_buddy
        espressif__cjson
        esp_system
        esp_timer
)
