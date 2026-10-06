idf_component_register(
    SRCS "src/example_console.c"
    INCLUDE_DIRS "include"
    REQUIRES
        console
        esp_desktop_buddy
        esp_desktop_buddy_transport_ble
        example_app_helpers
)
