idf_component_register(
    SRCS "src/example_app_helpers.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_desktop_buddy
        esp_desktop_buddy_transport_ble
        example_store_nvs
        nvs_flash
)
