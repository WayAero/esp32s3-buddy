/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "example_charpack_internal.h"

static char *example_charpack_read_file(const char *path, size_t *out_len)
{
    FILE *file = NULL;
    long size;
    char *buf = NULL;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size <= 0 || size > CONFIG_EXAMPLE_CHARPACK_MAX_MANIFEST_SIZE) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buf = calloc((size_t)size + 1, 1);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }

    if (fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return NULL;
    }

    fclose(file);
    if (out_len != NULL) {
        *out_len = (size_t)size;
    }
    return buf;
}

bool example_charpack_is_safe_pack_id(const char *pack_id)
{
    const char *p;

    if (pack_id == NULL || pack_id[0] == '\0') {
        return false;
    }
    if (strlen(pack_id) > EXAMPLE_CHARPACK_PACK_ID_MAX) {
        return false;
    }

    for (p = pack_id; *p != '\0'; ++p) {
        if (((*p >= 'a' && *p <= 'z') ||
             (*p >= '0' && *p <= '9') ||
             *p == '-' || *p == '_')) {
            continue;
        }
        return false;
    }

    return true;
}

bool example_charpack_normalize_pack_id(const char *source_name,
                                          char *pack_id,
                                          size_t pack_id_size)
{
    bool last_was_separator = true;
    size_t out_len = 0;

    if (source_name == NULL || pack_id == NULL || pack_id_size == 0 || source_name[0] == '\0') {
        return false;
    }

    for (const char *p = source_name; *p != '\0'; ++p) {
        char ch = *p;

        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }

        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            if (out_len + 1 >= pack_id_size) {
                return false;
            }
            pack_id[out_len++] = ch;
            last_was_separator = false;
            continue;
        }

        if (ch == '-' || ch == '_' || ch == ' ' || ch == '.') {
            if (last_was_separator) {
                continue;
            }
            if (out_len + 1 >= pack_id_size) {
                return false;
            }
            pack_id[out_len++] = '-';
            last_was_separator = true;
            continue;
        }
    }

    while (out_len > 0 && (pack_id[out_len - 1] == '-' || pack_id[out_len - 1] == '_')) {
        out_len--;
    }
    pack_id[out_len] = '\0';

    return out_len > 0 && example_charpack_is_safe_pack_id(pack_id);
}

esp_err_t example_charpack_read_manifest_info(const char *manifest_path,
                                                const char *expected_pack_id,
                                                const char *expected_source_name,
                                                example_charpack_info_t *out_info,
                                                const char **out_error_token)
{
    char *manifest_json = NULL;
    char normalized_pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1] = {0};
    cJSON *root = NULL;
    cJSON *mode_item;
    cJSON *name_item;
    esp_err_t err = ESP_FAIL;

    if (out_error_token != NULL) {
        *out_error_token = NULL;
    }
    if (out_info == NULL || manifest_path == NULL || expected_pack_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    manifest_json = example_charpack_read_file(manifest_path, NULL);
    if (manifest_json == NULL) {
        if (out_error_token != NULL) {
            *out_error_token = "missing_manifest";
        }
        return ESP_ERR_NOT_FOUND;
    }

    root = cJSON_Parse(manifest_json);
    if (!cJSON_IsObject(root)) {
        if (out_error_token != NULL) {
            *out_error_token = "invalid_manifest";
        }
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    memset(out_info, 0, sizeof(*out_info));
    strlcpy(out_info->pack_id, expected_pack_id, sizeof(out_info->pack_id));
    out_info->mode = EXAMPLE_CHARPACK_MODE_GIF;

    name_item = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (cJSON_IsString(name_item) && name_item->valuestring != NULL) {
        if (expected_source_name != NULL &&
            strcmp(name_item->valuestring, expected_source_name) != 0) {
            if (out_error_token != NULL) {
                *out_error_token = "inconsistent_name";
            }
            err = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
        if (!example_charpack_normalize_pack_id(name_item->valuestring,
                                                  normalized_pack_id,
                                                  sizeof(normalized_pack_id)) ||
            strcmp(normalized_pack_id, expected_pack_id) != 0) {
            if (out_error_token != NULL) {
                *out_error_token = "inconsistent_name";
            }
            err = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
    }

    mode_item = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (cJSON_IsString(mode_item) && mode_item->valuestring != NULL) {
        if (strcmp(mode_item->valuestring, "gif") == 0) {
            out_info->mode = EXAMPLE_CHARPACK_MODE_GIF;
        } else if (strcmp(mode_item->valuestring, "text") == 0) {
            out_info->mode = EXAMPLE_CHARPACK_MODE_TEXT;
        } else {
            if (out_error_token != NULL) {
                *out_error_token = "invalid_manifest";
            }
            err = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
    }

    err = ESP_OK;

cleanup:
    cJSON_Delete(root);
    free(manifest_json);
    return err;
}
