idf_component_register(
    SRCS
        "app_main.c"
        "app_activity.c"
        "app_commands.c"
        "app_settings.c"
        "app_time.c"
        "app_ui_events.c"
        "app_ui_snapshot.c"
        "ui_app_registry.c"
        "ui_locale.c"
        "fonts/ui_font_zh_14.c"
        "app_diag.c"
        "app_ble_commands.c"
        "ui.c"
        "ui/ui_core.c"
        "ui/ui_theme.c"
        "ui/ui_memory.c"
        "ui/ui_activity.c"
        "ui/ui_aod.c"
        "ui/ui_buddy.c"
        "ui/ui_diagnostics.c"
        "ui/ui_format.c"
        "ui/ui_home.c"
        "ui/ui_layout.c"
        "ui/ui_navigation.c"
        "ui/ui_prompt.c"
        "ui/ui_settings.c"
        "ui/ui_status_bar.c"
        "charpack_console.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_desktop_buddy
        esp_desktop_buddy_transport_ble
        esp_desktop_buddy_folder_push
        example_console
        example_app_helpers
        example_status
        example_charpack
        espressif__cjson
        buddy_bsp
        lvgl_runtime_patch
        espressif__esp_lvgl_adapter
        esp_pm
        nvs_flash
        esp_app_format
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wframe-larger-than=3072
    -Werror=frame-larger-than=3072
)

# 字库随应用提供只读备份，避免 storage 未更新时动态中文变成方框。
target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../storage/fonts/zh_cn_14.bin"
    BINARY RENAME_TO ui_zh_cn_14_bin)

# LVGL 从依赖库调用应用提供的分配器，显式保留其所在对象文件。
if(CONFIG_LV_USE_CUSTOM_MALLOC)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_mem_init")
endif()

fatfs_create_spiflash_image(storage ../storage FLASH_IN_PROJECT)
