idf_component_register(
    SRCS
        "src/buddy_core.c"
        "src/buddy_command_extensions.c"
        "src/buddy_protocol.c"
        "src/buddy_codec.c"
        "src/buddy_linebuf.c"
        "src/buddy_txq.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES espressif__cjson esp_timer
)
