/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "buddy_transport_ble_internal.h"

static const char *TAG = "esp_desktop_buddy_transport_ble";

static const ble_uuid128_t BUDDY_NUS_SERVICE_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t BUDDY_NUS_RX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t BUDDY_NUS_TX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static int esp_desktop_buddy_transport_ble_gatt_rx_access(uint16_t conn_handle,
                                              uint16_t attr_handle,
                                              struct ble_gatt_access_ctxt *ctxt,
                                              void *arg)
{
    esp_desktop_buddy_transport_ble_t *transport = (esp_desktop_buddy_transport_ble_t *)arg;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t flat[ESP_DESKTOP_BUDDY_TRANSPORT_BLE_RX_WRITE_MAX];
    esp_err_t err;
    int rc;

    (void)attr_handle;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (len == 0) {
        return 0;
    }
    if (len > ESP_DESKTOP_BUDDY_TRANSPORT_BLE_RX_WRITE_MAX) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (transport == NULL || transport->buddy == NULL || transport->shutting_down) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    rc = ble_hs_mbuf_to_flat(ctxt->om, flat, sizeof(flat), &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
    if (conn_handle != transport->conn_handle || !transport->state.connected ||
        !transport->state.encrypted) {
        xSemaphoreGive(transport->state_mutex);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    transport->last_rx_us = esp_timer_get_time();
    xSemaphoreGive(transport->state_mutex);
    esp_desktop_buddy_transport_ble_schedule_stale_link_check(transport);

    err = esp_desktop_buddy_receive_bytes(transport->buddy, flat, len);
    if (err == ESP_OK) {
        return 0;
    }
    if (err == ESP_ERR_INVALID_SIZE) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int esp_desktop_buddy_transport_ble_gatt_tx_access(uint16_t conn_handle,
                                              uint16_t attr_handle,
                                              struct ble_gatt_access_ctxt *ctxt,
                                              void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return BLE_ATT_ERR_UNLIKELY;
}

void esp_desktop_buddy_transport_ble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char uuid_str[BLE_UUID_STR_LEN];

    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG,
                 "registered service %s handle=%u",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, uuid_str),
                 ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG,
                 "registered characteristic %s def_handle=%u val_handle=%u",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, uuid_str),
                 ctxt->chr.def_handle,
                 ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI(TAG,
                 "registered descriptor %s handle=%u",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, uuid_str),
                 ctxt->dsc.handle);
        break;
    default:
        break;
    }
}

void esp_desktop_buddy_transport_ble_prepare_gatt(esp_desktop_buddy_transport_ble_t *transport)
{
    memset(transport->gatt_chars, 0, sizeof(transport->gatt_chars));
    memset(transport->gatt_svcs, 0, sizeof(transport->gatt_svcs));

    transport->gatt_chars[0].uuid = &BUDDY_NUS_RX_UUID.u;
    transport->gatt_chars[0].access_cb = esp_desktop_buddy_transport_ble_gatt_rx_access;
    transport->gatt_chars[0].arg = transport;
    transport->gatt_chars[0].flags = BLE_GATT_CHR_F_WRITE |
                                     BLE_GATT_CHR_F_WRITE_NO_RSP |
                                     BLE_GATT_CHR_F_WRITE_ENC;

    transport->gatt_chars[1].uuid = &BUDDY_NUS_TX_UUID.u;
    transport->gatt_chars[1].access_cb = esp_desktop_buddy_transport_ble_gatt_tx_access;
    transport->gatt_chars[1].arg = transport;
    /*
     * NimBLE auto-generates the CCCD for notify characteristics. This
     * transport enforces TX policy at the link level instead: initiate
     * security on connect and only treat TX as ready once the link is
     * encrypted.
     */
    transport->gatt_chars[1].flags = BLE_GATT_CHR_F_NOTIFY;
    transport->gatt_chars[1].val_handle = &transport->tx_handle;

    transport->gatt_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    transport->gatt_svcs[0].uuid = &BUDDY_NUS_SERVICE_UUID.u;
    transport->gatt_svcs[0].characteristics = transport->gatt_chars;
}
