idf_component_register(
    SRCS "src/buddy_transport_ble.c"
         "src/buddy_transport_ble_tx.c"
         "src/buddy_transport_ble_gatt.c"
         "src/buddy_transport_ble_gap.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES bt esp_desktop_buddy
    PRIV_REQUIRES esp_timer
)
