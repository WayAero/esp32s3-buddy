idf_component_register(
    SRCS
        "src/example_charpack_install.c"
        "src/example_charpack_manifest.c"
        "src/example_charpack_events.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_desktop_buddy_folder_push fatfs wear_levelling espressif__cjson
)
