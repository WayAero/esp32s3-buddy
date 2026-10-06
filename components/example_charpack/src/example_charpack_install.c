/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wear_levelling.h"

#include "example_charpack_internal.h"

static const char *TAG = "example_charpack";
static const char *EXAMPLE_CHARPACK_ACTIVE_FILENAME = "active_pack";

static void example_charpack_safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, dst_size);
}

static bool example_charpack_info_matches(const example_charpack_info_t *lhs,
                                          const example_charpack_info_t *rhs)
{
    return lhs != NULL &&
           rhs != NULL &&
           lhs->mode == rhs->mode &&
           strcmp(lhs->pack_id, rhs->pack_id) == 0;
}

static bool example_charpack_is_dir(const char *path)
{
    struct stat st;

    if (path == NULL || stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

static esp_err_t example_charpack_ensure_dir(const char *path)
{
    char buf[EXAMPLE_CHARPACK_PATH_MAX];
    size_t len;

    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(path) >= sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }

    strlcpy(buf, path, sizeof(buf));
    len = strlen(buf);
    for (size_t i = 1; i < len; ++i) {
        if (buf[i] != '/') {
            continue;
        }
        buf[i] = '\0';
        if (!example_charpack_is_dir(buf) && mkdir(buf, 0777) != 0 && errno != EEXIST) {
            return ESP_FAIL;
        }
        buf[i] = '/';
    }

    if (!example_charpack_is_dir(buf) && mkdir(buf, 0777) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void example_charpack_remove_tree(const char *path)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir(path);
    if (dir == NULL) {
        unlink(path);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char full[EXAMPLE_CHARPACK_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >= (int)sizeof(full)) {
            continue;
        }
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            example_charpack_remove_tree(full);
            rmdir(full);
        } else {
            unlink(full);
        }
    }

    closedir(dir);
}

static void example_charpack_remove_path(const char *path)
{
    struct stat st;

    if (path == NULL || stat(path, &st) != 0) {
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        example_charpack_remove_tree(path);
        rmdir(path);
    } else {
        unlink(path);
    }
}

static void example_charpack_clear_transfer_locked(example_charpack_t *charpack)
{
    if (charpack == NULL) {
        return;
    }

    if (charpack->transfer.file != NULL) {
        fclose(charpack->transfer.file);
        charpack->transfer.file = NULL;
    }
    memset(&charpack->transfer, 0, sizeof(charpack->transfer));
}

static esp_err_t example_charpack_close_file(FILE **file, bool flush)
{
    int flush_rc = 0;
    int close_rc;
    int sync_rc = 0;

    if (file == NULL || *file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (flush) {
        flush_rc = fflush(*file);
        if (flush_rc == 0) sync_rc = fsync(fileno(*file));
    }
    close_rc = fclose(*file);
    *file = NULL;

    if ((flush && (flush_rc != 0 || sync_rc != 0)) || close_rc != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t example_charpack_write_active_file(example_charpack_t *charpack,
                                                      const char *pack_id)
{
    char tmp_path[EXAMPLE_CHARPACK_PATH_MAX];
    char backup_path[EXAMPLE_CHARPACK_PATH_MAX];
    FILE *file;

    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", charpack->active_path) >= (int)sizeof(tmp_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (snprintf(backup_path, sizeof(backup_path), "%s.bak", charpack->active_path) >=
        (int)sizeof(backup_path)) return ESP_ERR_INVALID_SIZE;

    file = fopen(tmp_path, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    if (fwrite(pack_id, 1, strlen(pack_id), file) != strlen(pack_id)) {
        (void)example_charpack_close_file(&file, false);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    if (example_charpack_close_file(&file, true) != ESP_OK) {
        unlink(tmp_path);
        return ESP_FAIL;
    }
    /* FAT 的 rename 不覆盖已有文件。保留旧记录直到新记录提交，
     * 启动恢复会校验正式、临时、备份记录所指向的角色包。 */
    struct stat st;
    if (stat(charpack->active_path, &st) == 0) {
        if (unlink(backup_path) != 0 && errno != ENOENT) return ESP_FAIL;
        if (rename(charpack->active_path, backup_path) != 0) return ESP_FAIL;
    }
    if (rename(tmp_path, charpack->active_path) != 0) {
        (void)rename(backup_path, charpack->active_path);
        return ESP_FAIL;
    }
    if (unlink(backup_path) != 0 && errno != ENOENT) {
        ESP_LOGW(TAG, "active selection committed; backup cleanup deferred");
    }
    return ESP_OK;
}

static void example_charpack_clear_active_locked(example_charpack_t *charpack, bool emit_event)
{
    bool had_active = charpack->active_present;

    charpack->active_present = false;
    memset(&charpack->active_info, 0, sizeof(charpack->active_info));
    unlink(charpack->active_path);

    if (emit_event && had_active) {
        example_charpack_emit_event(charpack,
                                      EXAMPLE_CHARPACK_EVENT_ACTIVE_CLEARED,
                                      NULL,
                                      NULL,
                                      0,
                                      0,
                                      0);
    }
}

/* 此清单只由完整传输创建。没有它的 staging 不得在启动时提升为正式包。 */
#define EXAMPLE_CHARPACK_COMPLETE_FILE ".buddy_complete"
#define EXAMPLE_CHARPACK_COMPLETE_MAGIC 0x42504331U
#define EXAMPLE_CHARPACK_COMPLETE_MAX_FILES 64U

typedef struct {
    char name[128];
    uint32_t size;
    uint32_t crc;
} example_charpack_file_record_t;

static bool example_charpack_record_name_valid(const char *name)
{
    size_t len = strnlen(name, 128);
    if (len == 0 || len >= 128 || name[0] == '.') return false;
    for (size_t i = 0; i < len; ++i) {
        if ((unsigned char)name[i] < 32 || name[i] == '/' || name[i] == 92) return false;
    }
    return true;
}

static bool example_charpack_file_crc(const char *path, uint32_t *size, uint32_t *crc)
{
    FILE *file = fopen(path, "rb");
    unsigned char buffer[256];
    uint32_t value = UINT32_MAX;
    uint32_t bytes = 0;
    size_t count;
    if (file == NULL) return false;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes += (uint32_t)count;
        for (size_t i = 0; i < count; ++i) {
            value ^= buffer[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
        }
    }
    bool valid = !ferror(file);
    if (fclose(file) != 0) valid = false;
    *size = bytes;
    *crc = value ^ UINT32_MAX;
    return valid;
}

static bool example_charpack_complete_record(const char *root, bool write_record)
{
    char path[EXAMPLE_CHARPACK_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", root, EXAMPLE_CHARPACK_COMPLETE_FILE) >=
        (int)sizeof(path)) return false;
    FILE *record_file = fopen(path, write_record ? "wb" : "rb");
    if (record_file == NULL) return false;
    uint32_t header[2] = {EXAMPLE_CHARPACK_COMPLETE_MAGIC, 0};
    bool valid = true;
    if (write_record) {
        DIR *dir = opendir(root);
        struct dirent *entry;
        valid = dir != NULL && fwrite(header, sizeof(header), 1, record_file) == 1;
        while (valid && (entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            example_charpack_file_record_t record = {0};
            if (!example_charpack_record_name_valid(entry->d_name) ||
                ++header[1] > EXAMPLE_CHARPACK_COMPLETE_MAX_FILES ||
                snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= (int)sizeof(path)) {
                valid = false;
                break;
            }
            strlcpy(record.name, entry->d_name, sizeof(record.name));
            valid = example_charpack_file_crc(path, &record.size, &record.crc) &&
                    fwrite(&record, sizeof(record), 1, record_file) == 1;
        }
        if (dir != NULL) closedir(dir);
        valid = valid && header[1] > 0 && fseek(record_file, 0, SEEK_SET) == 0 &&
                fwrite(header, sizeof(header), 1, record_file) == 1;
        if (example_charpack_close_file(&record_file, true) != ESP_OK) valid = false;
    } else {
        valid = fread(header, sizeof(header), 1, record_file) == 1 &&
                header[0] == EXAMPLE_CHARPACK_COMPLETE_MAGIC && header[1] > 0 &&
                header[1] <= EXAMPLE_CHARPACK_COMPLETE_MAX_FILES;
        bool have_manifest = false;
        for (uint32_t i = 0; valid && i < header[1]; ++i) {
            example_charpack_file_record_t record;
            uint32_t size, crc;
            valid = fread(&record, sizeof(record), 1, record_file) == 1 &&
                    example_charpack_record_name_valid(record.name) &&
                    snprintf(path, sizeof(path), "%s/%s", root, record.name) < (int)sizeof(path) &&
                    example_charpack_file_crc(path, &size, &crc) && size == record.size && crc == record.crc;
            if (valid && strcmp(record.name, "manifest.json") == 0) have_manifest = true;
        }
        valid = valid && have_manifest && fgetc(record_file) == EOF && !ferror(record_file);
        if (fclose(record_file) != 0) valid = false;
    }
    return valid;
}

static bool example_charpack_root_valid(const char *root, const char *id, bool require_record)
{
    char path[EXAMPLE_CHARPACK_PATH_MAX];
    example_charpack_info_t info;
    struct stat st;
    if (snprintf(path, sizeof(path), "%s/manifest.json", root) >= (int)sizeof(path) ||
        example_charpack_read_manifest_info(path, id, NULL, &info, NULL) != ESP_OK) return false;
    if (snprintf(path, sizeof(path), "%s/%s", root, EXAMPLE_CHARPACK_COMPLETE_FILE) >=
        (int)sizeof(path)) return false;
    if (stat(path, &st) == 0) {
        if (!example_charpack_complete_record(root, false)) return false;
    } else if (require_record || errno != ENOENT) return false;
    /* 旧版正式包没有 CRC 清单；至少确认必需的 idle GIF 存在。
     * 旧目录仅用于兼容读取，绝不以同样条件提升 staging。 */
    if (info.mode == EXAMPLE_CHARPACK_MODE_TEXT) return true;
    if (snprintf(path, sizeof(path), "%s/idle.gif", root) >= (int)sizeof(path)) return false;
    FILE *file = fopen(path, "rb");
    unsigned char signature[13];
    if (file == NULL) return false;
    bool valid = fread(signature, 1, sizeof(signature), file) == sizeof(signature) &&
                 (memcmp(signature, "GIF89a", 6) == 0 || memcmp(signature, "GIF87a", 6) == 0) &&
                 (signature[6] != 0 || signature[7] != 0) &&
                 (signature[8] != 0 || signature[9] != 0) &&
                 fseek(file, -1, SEEK_END) == 0 && fgetc(file) == 0x3b;
    if (fclose(file) != 0) valid = false;
    return valid;
}

static esp_err_t example_charpack_recover_one(example_charpack_t *charpack, const char *id)
{
    char dest[EXAMPLE_CHARPACK_PATH_MAX], backup[EXAMPLE_CHARPACK_PATH_MAX];
    char staging[EXAMPLE_CHARPACK_PATH_MAX], quarantine[EXAMPLE_CHARPACK_PATH_MAX];
    if (!example_charpack_is_safe_pack_id(id)) return ESP_ERR_INVALID_ARG;
    if (snprintf(dest, sizeof(dest), "%s/%s", charpack->packs_root, id) >= (int)sizeof(dest) ||
        snprintf(backup, sizeof(backup), "%s/.rollback_%s", charpack->mount_point, id) >= (int)sizeof(backup) ||
        snprintf(staging, sizeof(staging), "%s/%s", charpack->staging_root, id) >= (int)sizeof(staging) ||
        snprintf(quarantine, sizeof(quarantine), "%s/.damaged_%s", charpack->mount_point, id) >= (int)sizeof(quarantine))
        return ESP_ERR_INVALID_SIZE;
    if (example_charpack_root_valid(dest, id, false)) return ESP_OK;
    const char *replacement = example_charpack_root_valid(backup, id, false) ? backup :
                              example_charpack_root_valid(staging, id, true) ? staging : NULL;
    if (replacement == NULL) return ESP_OK;
    /* 不删唯一有效副本。损坏正式目录留作诊断，移动失败时停止恢复。 */
    if (example_charpack_is_dir(dest)) {
        struct stat st;
        unsigned attempt;
        for (attempt = 0; attempt < 256; ++attempt) {
            if (snprintf(quarantine, sizeof(quarantine), "%s/.damaged_%s_%u", charpack->mount_point,
                         id, attempt) >= (int)sizeof(quarantine)) return ESP_ERR_INVALID_SIZE;
            if (stat(quarantine, &st) != 0) {
                if (errno != ENOENT) return ESP_FAIL;
                break;
            }
        }
        if (attempt == 256 || rename(dest, quarantine) != 0) return ESP_FAIL;
    }
    if (rename(replacement, dest) != 0) return ESP_FAIL;
    return example_charpack_root_valid(dest, id, false) ? ESP_OK : ESP_FAIL;
}

static esp_err_t example_charpack_recover_directories(example_charpack_t *charpack)
{
    const char *roots[] = {charpack->mount_point, charpack->staging_root};
    for (size_t i = 0; i < 2; ++i) {
        DIR *dir = opendir(roots[i]);
        if (dir == NULL) return ESP_FAIL;
        struct dirent *entry;
        esp_err_t result = ESP_OK;
        while ((entry = readdir(dir)) != NULL) {
            const char *id = entry->d_name;
            if (i == 0) {
                if (strncmp(id, ".rollback_", 10) != 0) continue;
                id += 10;
            }
            if (!example_charpack_is_safe_pack_id(id)) continue;
            result = example_charpack_recover_one(charpack, id);
            if (result != ESP_OK) break;
        }
        closedir(dir);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

static esp_err_t example_charpack_validate_installed_pack(example_charpack_t *charpack,
                                                            const char *pack_id,
                                                            example_charpack_info_t *out_info)
{
    char manifest_path[EXAMPLE_CHARPACK_PATH_MAX];
    const char *error_token = NULL;

    if (!example_charpack_is_safe_pack_id(pack_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (snprintf(manifest_path,
                 sizeof(manifest_path),
                 "%s/%s/manifest.json",
                 charpack->packs_root,
                 pack_id) >= (int)sizeof(manifest_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char root[EXAMPLE_CHARPACK_PATH_MAX];
    if (snprintf(root, sizeof(root), "%s/%s", charpack->packs_root, pack_id) >= (int)sizeof(root) ||
        !example_charpack_root_valid(root, pack_id, false)) return ESP_ERR_INVALID_RESPONSE;
    return example_charpack_read_manifest_info(
        manifest_path, pack_id, NULL, out_info, &error_token);
}

static bool example_charpack_active_record_valid(example_charpack_t *charpack, const char *path)
{
    char id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1] = {0};
    example_charpack_info_t info;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    size_t size = fread(id, 1, sizeof(id) - 1, file);
    bool valid = size > 0 && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) valid = false;
    return valid && example_charpack_validate_installed_pack(charpack, id, &info) == ESP_OK;
}

static esp_err_t example_charpack_recover_active_record(example_charpack_t *charpack)
{
    char tmp[EXAMPLE_CHARPACK_PATH_MAX], backup[EXAMPLE_CHARPACK_PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", charpack->active_path) >= (int)sizeof(tmp) ||
        snprintf(backup, sizeof(backup), "%s.bak", charpack->active_path) >= (int)sizeof(backup))
        return ESP_ERR_INVALID_SIZE;
    if (example_charpack_active_record_valid(charpack, charpack->active_path)) return ESP_OK;
    const char *candidate = example_charpack_active_record_valid(charpack, tmp) ? tmp :
                            example_charpack_active_record_valid(charpack, backup) ? backup : NULL;
    if (candidate == NULL) return ESP_OK;
    if (unlink(charpack->active_path) != 0 && errno != ENOENT) return ESP_FAIL;
    return rename(candidate, charpack->active_path) == 0 ? ESP_OK : ESP_FAIL;
}

static void example_charpack_refresh_active_from_disk(example_charpack_t *charpack)
{
    char pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1] = {0};
    FILE *file;
    example_charpack_info_t info = {0};
    size_t bytes_read;

    file = fopen(charpack->active_path, "rb");
    if (file == NULL) {
        charpack->active_present = false;
        memset(&charpack->active_info, 0, sizeof(charpack->active_info));
        return;
    }

    bytes_read = fread(pack_id, 1, sizeof(pack_id) - 1, file);
    if (bytes_read == 0) {
        fclose(file);
        example_charpack_clear_active_locked(charpack, false);
        return;
    }
    if (bytes_read == sizeof(pack_id) - 1 && fgetc(file) != EOF) {
        fclose(file);
        example_charpack_clear_active_locked(charpack, false);
        return;
    }
    fclose(file);

    if (example_charpack_validate_installed_pack(charpack, pack_id, &info) != ESP_OK) {
        example_charpack_clear_active_locked(charpack, false);
        return;
    }

    charpack->active_info = info;
    charpack->active_present = true;
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_sink_ok(void)
{
    return esp_desktop_buddy_folder_push_result_ok();
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_sink_storage_failed(const char *detail)
{
    return esp_desktop_buddy_folder_push_result_err(ESP_FAIL,
                                                   ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_STORAGE_FAILED,
                                                   detail);
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_sink_invalid_content(const char *detail)
{
    return esp_desktop_buddy_folder_push_result_err(ESP_ERR_INVALID_ARG,
                                                   ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_INVALID_CONTENT,
                                                   detail);
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_begin_transfer(void *ctx,
                                                                    const char *name,
                                                                    uint32_t total_bytes)
{
    example_charpack_t *charpack = (example_charpack_t *)ctx;
    char pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1];
    char transfer_root[EXAMPLE_CHARPACK_PATH_MAX];

    if (!example_charpack_normalize_pack_id(name, pack_id, sizeof(pack_id))) {
        return example_charpack_sink_invalid_content("invalid_pack_id");
    }
    if (snprintf(transfer_root, sizeof(transfer_root), "%s/%s", charpack->staging_root, pack_id) >=
        (int)sizeof(transfer_root)) {
        return example_charpack_sink_storage_failed("staging_path_too_long");
    }

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    if (charpack->transfer.active || charpack->selection_active) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("pack_operation_busy");
    }
    memset(&charpack->transfer, 0, sizeof(charpack->transfer));
    charpack->transfer.active = true;
    charpack->transfer.total_bytes = total_bytes;
    example_charpack_safe_copy(charpack->transfer.pack_id,
                                 sizeof(charpack->transfer.pack_id),
                                 pack_id);
    example_charpack_safe_copy(charpack->transfer.source_name,
                                 sizeof(charpack->transfer.source_name),
                                 name);
    xSemaphoreGive(charpack->mutex);

    example_charpack_remove_tree(transfer_root);
    if (example_charpack_ensure_dir(transfer_root) != ESP_OK) {
        xSemaphoreTake(charpack->mutex, portMAX_DELAY);
        example_charpack_clear_transfer_locked(charpack);
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("staging_dir_create_failed");
    }

    example_charpack_info_t info = {0};
    example_charpack_safe_copy(info.pack_id, sizeof(info.pack_id), pack_id);
    info.mode = EXAMPLE_CHARPACK_MODE_GIF;
    example_charpack_emit_event(charpack,
                                  EXAMPLE_CHARPACK_EVENT_TRANSFER_STARTED,
                                  &info,
                                  NULL,
                                  0,
                                  0,
                                  total_bytes);
    return example_charpack_sink_ok();
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_begin_file(void *ctx,
                                                                const char *path,
                                                                uint32_t size)
{
    example_charpack_t *charpack = (example_charpack_t *)ctx;
    char full_path[EXAMPLE_CHARPACK_PATH_MAX];
    FILE *file;
    example_charpack_info_t info = {0};

    if (path == NULL || path[0] == '.')
        return example_charpack_sink_invalid_content("reserved_file_name");
    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    if (!charpack->transfer.active) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("transfer_not_active");
    }
    if (snprintf(full_path,
                 sizeof(full_path),
                 "%s/%s/%s",
                 charpack->staging_root,
                 charpack->transfer.pack_id,
                 path) >= (int)sizeof(full_path)) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("file_path_too_long");
    }
    file = fopen(full_path, "wb");
    if (file == NULL) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("file_open_failed");
    }

    charpack->transfer.file = file;
    charpack->transfer.file_size = size;
    example_charpack_safe_copy(charpack->transfer.current_path,
                                 sizeof(charpack->transfer.current_path),
                                 path);
    example_charpack_safe_copy(info.pack_id, sizeof(info.pack_id), charpack->transfer.pack_id);
    xSemaphoreGive(charpack->mutex);

    example_charpack_emit_event(charpack,
                                  EXAMPLE_CHARPACK_EVENT_FILE_STARTED,
                                  &info,
                                  path,
                                  size,
                                  0,
                                  0);
    return example_charpack_sink_ok();
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_write_chunk(void *ctx,
                                                                 const uint8_t *data,
                                                                 size_t len)
{
    example_charpack_t *charpack = (example_charpack_t *)ctx;
    example_charpack_info_t info = {0};
    int64_t write_started_us;

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    if (!charpack->transfer.active || charpack->transfer.file == NULL) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("file_not_open");
    }
    write_started_us = esp_timer_get_time();
    if (fwrite(data, 1, len, charpack->transfer.file) != len) {
        (void)example_charpack_close_file(&charpack->transfer.file, false);
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("file_write_failed");
    }
    const uint32_t write_elapsed_us = (uint32_t)(esp_timer_get_time() - write_started_us);
    charpack->transfer.bytes_written += (uint32_t)len;
    charpack->diagnostics.chunk_write_count++;
    charpack->diagnostics.fwrite_total_us += write_elapsed_us;
    if (write_elapsed_us > charpack->diagnostics.fwrite_max_us) {
        charpack->diagnostics.fwrite_max_us = write_elapsed_us;
    }
    example_charpack_safe_copy(info.pack_id, sizeof(info.pack_id), charpack->transfer.pack_id);
    uint32_t bytes_written = charpack->transfer.bytes_written;
    uint32_t total_bytes = charpack->transfer.total_bytes;
    xSemaphoreGive(charpack->mutex);

    example_charpack_emit_event(charpack,
                                  EXAMPLE_CHARPACK_EVENT_TRANSFER_PROGRESS,
                                  &info,
                                  NULL,
                                  0,
                                  bytes_written,
                                  total_bytes);
    return example_charpack_sink_ok();
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_end_file(void *ctx)
{
    example_charpack_t *charpack = (example_charpack_t *)ctx;
    example_charpack_info_t info = {0};
    char path[EXAMPLE_CHARPACK_PATH_MAX];
    uint32_t size;
    uint32_t finalize_elapsed_us;

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    if (!charpack->transfer.active || charpack->transfer.file == NULL) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("file_not_open");
    }
    const int64_t finalize_started_us = esp_timer_get_time();
    const esp_err_t finalize_err = example_charpack_close_file(&charpack->transfer.file, true);
    finalize_elapsed_us = (uint32_t)(esp_timer_get_time() - finalize_started_us);
    charpack->diagnostics.file_finalize_count++;
    charpack->diagnostics.file_finalize_total_us += finalize_elapsed_us;
    if (finalize_elapsed_us > charpack->diagnostics.file_finalize_max_us) {
        charpack->diagnostics.file_finalize_max_us = finalize_elapsed_us;
    }
    if (finalize_err != ESP_OK) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("file_close_failed");
    }
    size = charpack->transfer.file_size;
    example_charpack_safe_copy(path, sizeof(path), charpack->transfer.current_path);
    charpack->transfer.current_path[0] = '\0';
    example_charpack_safe_copy(info.pack_id, sizeof(info.pack_id), charpack->transfer.pack_id);
    xSemaphoreGive(charpack->mutex);

    example_charpack_emit_event(charpack,
                                  EXAMPLE_CHARPACK_EVENT_FILE_FINISHED,
                                  &info,
                                  path,
                                  size,
                                  0,
                                  0);
    return example_charpack_sink_ok();
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_commit_transfer(void *ctx, example_charpack_info_t *out_info, bool *out_changed)
{
    example_charpack_t *charpack = (example_charpack_t *)ctx;
    char staging_path[EXAMPLE_CHARPACK_PATH_MAX];
    char manifest_path[EXAMPLE_CHARPACK_PATH_MAX];
    char dest_path[EXAMPLE_CHARPACK_PATH_MAX];
    char backup_path[EXAMPLE_CHARPACK_PATH_MAX];
    char expected_pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1];
    char expected_source_name[EXAMPLE_CHARPACK_PATH_MAX];
    const char *error_token = NULL;
    example_charpack_info_t info = {0};
    example_charpack_info_t previous_active = {0};
    bool had_previous_active = false;
    bool active_changed = false;
    bool moved_existing_dest = false;

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    example_charpack_safe_copy(info.pack_id, sizeof(info.pack_id), charpack->transfer.pack_id);
    *out_info = info;
    example_charpack_safe_copy(expected_pack_id,
                                 sizeof(expected_pack_id),
                                 charpack->transfer.pack_id);
    example_charpack_safe_copy(expected_source_name,
                                 sizeof(expected_source_name),
                                 charpack->transfer.source_name);
    had_previous_active = charpack->active_present;
    previous_active = charpack->active_info;
    if (snprintf(staging_path,
                 sizeof(staging_path),
                 "%s/%s",
                 charpack->staging_root,
                 charpack->transfer.pack_id) >= (int)sizeof(staging_path) ||
        snprintf(manifest_path,
                 sizeof(manifest_path),
                 "%s/manifest.json",
                 staging_path) >= (int)sizeof(manifest_path) ||
        snprintf(dest_path,
                 sizeof(dest_path),
                 "%s/%s",
                 charpack->packs_root,
                 charpack->transfer.pack_id) >= (int)sizeof(dest_path) ||
        snprintf(backup_path,
                 sizeof(backup_path),
                 "%s/.rollback_%s",
                 charpack->mount_point,
                 charpack->transfer.pack_id) >= (int)sizeof(backup_path)) {
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("backup_path_too_long");
    }
    xSemaphoreGive(charpack->mutex);

    if (example_charpack_read_manifest_info(manifest_path,
                                              expected_pack_id,
                                              expected_source_name,
                                              &info,
                                              &error_token) != ESP_OK) {
        example_charpack_remove_path(staging_path);
        xSemaphoreTake(charpack->mutex, portMAX_DELAY);
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_invalid_content(error_token != NULL ? error_token : "invalid_manifest");
    }

    *out_info = info;
    if (!example_charpack_complete_record(staging_path, true) ||
        !example_charpack_root_valid(staging_path, expected_pack_id, true)) {
        return example_charpack_sink_storage_failed("completion_record_failed");
    }
    if (example_charpack_recover_one(charpack, expected_pack_id) != ESP_OK)
        return example_charpack_sink_storage_failed("previous_install_recovery_failed");
    /* 若首次安装的完整 staging 已被恢复为正式包，继续写活动记录即可。 */
    if (!example_charpack_is_dir(staging_path)) {
        if (example_charpack_write_active_file(charpack, info.pack_id) != ESP_OK)
            return example_charpack_sink_storage_failed("set_active_failed");
        goto installation_committed;
    }
    if (example_charpack_is_dir(backup_path) &&
        !example_charpack_root_valid(dest_path, expected_pack_id, false))
        return example_charpack_sink_storage_failed("backup_is_only_valid_copy");
    example_charpack_remove_path(backup_path);
    if (example_charpack_is_dir(dest_path)) {
        if (rename(dest_path, backup_path) != 0) {
            example_charpack_remove_path(staging_path);
            xSemaphoreTake(charpack->mutex, portMAX_DELAY);
                xSemaphoreGive(charpack->mutex);
            return example_charpack_sink_storage_failed("backup_existing_pack_failed");
        }
        moved_existing_dest = true;
    }

    if (rename(staging_path, dest_path) != 0) {
        example_charpack_remove_path(staging_path);
        if (moved_existing_dest) {
            (void)rename(backup_path, dest_path);
        }
        xSemaphoreTake(charpack->mutex, portMAX_DELAY);
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("activate_staged_pack_failed");
    }

    if (example_charpack_write_active_file(charpack, info.pack_id) != ESP_OK) {
        example_charpack_remove_path(dest_path);
        if (moved_existing_dest && rename(backup_path, dest_path) != 0) {
            ESP_LOGW(TAG, "failed to restore previous pack after active-pack write failure");
        }
        xSemaphoreTake(charpack->mutex, portMAX_DELAY);
        xSemaphoreGive(charpack->mutex);
        return example_charpack_sink_storage_failed("set_active_failed");
    }

    if (moved_existing_dest) {
        example_charpack_remove_path(backup_path);
    }

installation_committed:
    active_changed = !had_previous_active || !example_charpack_info_matches(&previous_active, &info);
    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    charpack->active_info = info;
    charpack->active_present = true;
    xSemaphoreGive(charpack->mutex);
    *out_info = info;
    *out_changed = active_changed;
    return example_charpack_sink_ok();
}

static esp_desktop_buddy_folder_push_sink_result_t example_charpack_end_transfer(void *ctx)
{
    example_charpack_t *charpack = ctx;
    example_charpack_info_t info = {0};
    bool changed = false;
    xSemaphoreTake(charpack->files_mutex, portMAX_DELAY);
    esp_desktop_buddy_folder_push_sink_result_t result = example_charpack_commit_transfer(ctx, &info, &changed);
    xSemaphoreGive(charpack->files_mutex);
    example_charpack_emit_event(charpack, result.err == ESP_OK ? EXAMPLE_CHARPACK_EVENT_INSTALL_SUCCEEDED :
                               EXAMPLE_CHARPACK_EVENT_INSTALL_FAILED, &info, NULL, 0, 0, 0);
    if (result.err == ESP_OK && changed)
        example_charpack_emit_event(charpack, EXAMPLE_CHARPACK_EVENT_ACTIVE_CHANGED, &info, NULL, 0, 0, 0);
    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    example_charpack_clear_transfer_locked(charpack);
    xSemaphoreGive(charpack->mutex);
    return result;
}

static void example_charpack_abort_transfer(void *ctx)
{
    example_charpack_t *charpack = (example_charpack_t *)ctx;
    char staging_path[EXAMPLE_CHARPACK_PATH_MAX] = {0};
    example_charpack_info_t info = {0};
    bool emit_abort = false;

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    if (charpack->transfer.active) {
        emit_abort = true;
        example_charpack_safe_copy(info.pack_id, sizeof(info.pack_id), charpack->transfer.pack_id);
        if (snprintf(staging_path,
                     sizeof(staging_path),
                     "%s/%s",
                     charpack->staging_root,
                     charpack->transfer.pack_id) >= (int)sizeof(staging_path)) {
            staging_path[0] = '\0';
        }
        example_charpack_clear_transfer_locked(charpack);
    }
    xSemaphoreGive(charpack->mutex);

    if (staging_path[0] != '\0') {
        example_charpack_remove_path(staging_path);
    }
    if (emit_abort) {
        example_charpack_emit_event(charpack,
                                      EXAMPLE_CHARPACK_EVENT_TRANSFER_ABORTED,
                                      &info,
                                      NULL,
                                      0,
                                      0,
                                      0);
    }
}

esp_err_t example_charpack_new(const example_charpack_config_t *config,
                                 example_charpack_t **out_charpack)
{
    example_charpack_t *charpack;
    esp_vfs_fat_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    esp_err_t ret = ESP_OK;

    if (config == NULL || out_charpack == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    charpack = calloc(1, sizeof(*charpack));
    if (charpack == NULL) {
        return ESP_ERR_NO_MEM;
    }

    charpack->mutex = xSemaphoreCreateMutex();
    if (charpack->mutex == NULL) {
        free(charpack);
        return ESP_ERR_NO_MEM;
    }

    charpack->files_mutex = xSemaphoreCreateMutex();
    if (charpack->files_mutex == NULL) {
        vSemaphoreDelete(charpack->mutex);
        free(charpack);
        return ESP_ERR_NO_MEM;
    }
    charpack->on_event = config->on_event;
    charpack->event_ctx = config->event_ctx;
    charpack->partition_label = CONFIG_EXAMPLE_CHARPACK_SPIFFS_PARTITION_LABEL;
    charpack->format_if_mount_failed = config->format_if_mount_failed;
    charpack->wl_handle = WL_INVALID_HANDLE;
    example_charpack_safe_copy(charpack->mount_point,
                                 sizeof(charpack->mount_point),
                                 config->mount_point != NULL ? config->mount_point
                                                             : CONFIG_EXAMPLE_CHARPACK_MOUNT_POINT);
    example_charpack_safe_copy(charpack->packs_root,
                                 sizeof(charpack->packs_root),
                                 config->packs_root != NULL ? config->packs_root
                                                            : CONFIG_EXAMPLE_CHARPACK_PACKS_ROOT);
    example_charpack_safe_copy(charpack->staging_root,
                                 sizeof(charpack->staging_root),
                                 config->staging_root != NULL ? config->staging_root
                                                              : CONFIG_EXAMPLE_CHARPACK_STAGING_ROOT);
    if (strlcpy(charpack->active_path,
                charpack->mount_point,
                sizeof(charpack->active_path)) >= sizeof(charpack->active_path) ||
        strlcat(charpack->active_path,
                "/",
                sizeof(charpack->active_path)) >= sizeof(charpack->active_path) ||
        strlcat(charpack->active_path,
                EXAMPLE_CHARPACK_ACTIVE_FILENAME,
                sizeof(charpack->active_path)) >= sizeof(charpack->active_path)) {
        vSemaphoreDelete(charpack->files_mutex);
        vSemaphoreDelete(charpack->mutex);
        free(charpack);
        return ESP_ERR_INVALID_SIZE;
    }

    mount_config.format_if_mount_failed = charpack->format_if_mount_failed;
    mount_config.max_files = 8;
    mount_config.allocation_unit_size = 4096;

    ret = esp_vfs_fat_spiflash_mount_rw_wl(charpack->mount_point,
                                           charpack->partition_label,
                                           &mount_config,
                                           &charpack->wl_handle);
    if (ret != ESP_OK) {
        vSemaphoreDelete(charpack->files_mutex);
        vSemaphoreDelete(charpack->mutex);
        free(charpack);
        return ret;
    }
    charpack->mounted = true;

    ESP_GOTO_ON_ERROR(example_charpack_ensure_dir(charpack->packs_root), fail, TAG, "packs root");
    ESP_GOTO_ON_ERROR(example_charpack_ensure_dir(charpack->staging_root), fail, TAG, "staging root");

    ESP_GOTO_ON_ERROR(example_charpack_recover_directories(charpack), fail, TAG, "recover pack directories");
    ESP_GOTO_ON_ERROR(example_charpack_recover_active_record(charpack), fail, TAG, "recover active selection");

    charpack->sink.begin_transfer = example_charpack_begin_transfer;
    charpack->sink.begin_file = example_charpack_begin_file;
    charpack->sink.write_chunk = example_charpack_write_chunk;
    charpack->sink.end_file = example_charpack_end_file;
    charpack->sink.end_transfer = example_charpack_end_transfer;
    charpack->sink.abort_transfer = example_charpack_abort_transfer;
    charpack->sink.ctx = charpack;

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    example_charpack_refresh_active_from_disk(charpack);
    xSemaphoreGive(charpack->mutex);

    *out_charpack = charpack;
    return ESP_OK;

fail:
    example_charpack_delete(charpack);
    return ret;
}

void example_charpack_delete(example_charpack_t *charpack)
{
    if (charpack == NULL) {
        return;
    }

    example_charpack_abort_transfer(charpack);
    if (charpack->mounted) {
        esp_vfs_fat_spiflash_unmount_rw_wl(charpack->mount_point, charpack->wl_handle);
    }
    if (charpack->mutex != NULL) {
        vSemaphoreDelete(charpack->mutex);
    }
    if (charpack->files_mutex != NULL) vSemaphoreDelete(charpack->files_mutex);
    free(charpack);
}

const esp_desktop_buddy_folder_push_sink_t *example_charpack_get_sink(example_charpack_t *charpack)
{
    if (charpack == NULL) {
        return NULL;
    }
    return &charpack->sink;
}

esp_err_t example_charpack_list(example_charpack_t *charpack,
                                  example_charpack_info_t *items,
                                  size_t capacity,
                                  size_t *out_count)
{
    DIR *dir;
    struct dirent *entry;
    size_t count = 0;

    if (charpack == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(charpack->files_mutex, portMAX_DELAY);
    dir = opendir(charpack->packs_root);
    if (dir == NULL) {
        xSemaphoreGive(charpack->files_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    while ((entry = readdir(dir)) != NULL) {
        example_charpack_info_t info = {0};

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!example_charpack_is_safe_pack_id(entry->d_name)) {
            continue;
        }
        if (example_charpack_validate_installed_pack(charpack, entry->d_name, &info) != ESP_OK) continue;
        if (items != NULL && count < capacity) {
            items[count] = info;
        }
        count++;
    }

    closedir(dir);
    xSemaphoreGive(charpack->files_mutex);
    if (out_count != NULL) {
        *out_count = count;
    }
    return ESP_OK;
}

esp_err_t example_charpack_get_active(example_charpack_t *charpack,
                                        example_charpack_info_t *out_info)
{
    if (charpack == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    if (!charpack->active_present) {
        xSemaphoreGive(charpack->mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *out_info = charpack->active_info;
    xSemaphoreGive(charpack->mutex);
    return ESP_OK;
}

esp_err_t example_charpack_get_diagnostics(example_charpack_t *charpack,
                                            example_charpack_diagnostics_t *out_diagnostics)
{
    if (charpack == NULL || out_diagnostics == NULL || charpack->mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    *out_diagnostics = charpack->diagnostics;
    xSemaphoreGive(charpack->mutex);
    return ESP_OK;
}

esp_err_t example_charpack_set_active(example_charpack_t *charpack, const char *pack_id)
{
    if (charpack == NULL || !example_charpack_is_safe_pack_id(pack_id)) return ESP_ERR_INVALID_ARG;
    example_charpack_info_t info = {0};
    bool changed = false;
    xSemaphoreTake(charpack->files_mutex, portMAX_DELAY);
    xSemaphoreTake(charpack->mutex, portMAX_DELAY);
    bool busy = charpack->transfer.active || charpack->selection_active;
    if (!busy) charpack->selection_active = true;
    xSemaphoreGive(charpack->mutex);
    esp_err_t err = busy ? ESP_ERR_INVALID_STATE : example_charpack_validate_installed_pack(charpack, pack_id, &info);
    if (err == ESP_OK) {
        xSemaphoreTake(charpack->mutex, portMAX_DELAY);
        changed = !charpack->active_present || !example_charpack_info_matches(&charpack->active_info, &info);
        xSemaphoreGive(charpack->mutex);
        if (changed) err = example_charpack_write_active_file(charpack, pack_id);
    }
    if (err == ESP_OK) {
        xSemaphoreTake(charpack->mutex, portMAX_DELAY);
        charpack->active_info = info;
        charpack->active_present = true;
        xSemaphoreGive(charpack->mutex);
    }
    xSemaphoreGive(charpack->files_mutex);
    if (err == ESP_OK && changed)
        example_charpack_emit_event(charpack, EXAMPLE_CHARPACK_EVENT_ACTIVE_CHANGED, &info, NULL, 0, 0, 0);
    if (!busy) {
        xSemaphoreTake(charpack->mutex, portMAX_DELAY);
        charpack->selection_active = false;
        xSemaphoreGive(charpack->mutex);
    }
    return err;
}
