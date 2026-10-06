idf_component_register(
    SRCS "buddy_bsp.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_spi
        esp_lcd
        espressif__esp_lvgl_adapter
        espressif__esp_lcd_touch
        atanisoft__esp_lcd_touch_xpt2046
        espressif__led_strip
        nvs_flash
)
