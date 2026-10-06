/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_shared.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "example_console.h"

#define BUDDY_APP_CONSOLE_STACK 4096

static void buddy_app_charpack_console_print_state(void *ctx, FILE *out)
{
    buddy_app_print_state((buddy_app_t *)ctx, out);
}

static int buddy_app_console_diag(void *ctx, int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        fprintf(stdout, "Usage: diag\n");
        fflush(stdout);
        return 1;
    }
    buddy_app_diag_print((buddy_app_t *)ctx, stdout);
    return 0;
}

static example_console_common_cmds_t s_common_console_cmds = {
    .mutex = NULL,
    .buddy = NULL,
    .transport = NULL,
    .state_cache = NULL,
    .print_state = buddy_app_charpack_console_print_state,
    .state_ctx = NULL,
};

static int buddy_app_charpack_console_packs(void *ctx, int argc, char **argv)
{
    buddy_app_t *app = (buddy_app_t *)ctx;
    example_charpack_info_t items[8];
    size_t count = 0;

    (void)argv;

    if (argc != 1) {
        fprintf(stdout, "Usage: packs\n");
        fflush(stdout);
        return 1;
    }

    if (example_charpack_list(app->charpack, items, 8, &count) == ESP_OK) {
        for (size_t i = 0; i < count && i < 8; ++i) {
            fprintf(stdout, "pack[%lu]=%s mode=%d\n",
                    (unsigned long)i,
                    items[i].pack_id,
                    items[i].mode);
        }
    }
    fflush(stdout);
    return 0;
}

static int buddy_app_charpack_console_pack(void *ctx, int argc, char **argv)
{
    buddy_app_t *app = (buddy_app_t *)ctx;
    example_charpack_info_t items[8];
    char *end = NULL;
    size_t count = 0;
    unsigned long index;

    if (argc == 3 && strcmp(argv[1], "use") == 0) {
        esp_err_t err;

        index = strtoul(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0') {
            fprintf(stdout, "Usage: pack use <index>\n");
            fflush(stdout);
            return 1;
        }

        err = example_charpack_list(app->charpack, items, 8, &count);
        if (err == ESP_OK) {
            if (index >= count || index >= 8) {
                err = ESP_ERR_NOT_FOUND;
            } else {
                err = example_charpack_set_active(app->charpack, items[index].pack_id);
            }
        }

        fprintf(stdout, "pack use rc=%s\n", esp_err_to_name(err));
        fflush(stdout);
        return err == ESP_OK ? 0 : 1;
    }

    fprintf(stdout, "Usage: pack use <index>\n");
    fflush(stdout);
    return 1;
}


static int buddy_app_console_reset(void *ctx, int argc, char **argv)
{
    esp_err_t err;

    if (argc == 2 && strcmp(argv[1], "settings") == 0) {
        err = buddy_app_settings_reset((buddy_app_t *)ctx);
        fprintf(stdout, "reset settings rc=%s\n", esp_err_to_name(err));
        fflush(stdout);
        return err == ESP_OK ? 0 : 1;
    }

    fprintf(stdout, "Usage: reset settings\n");
    fflush(stdout);
    return 1;
}

/* 仅在存储未挂载时开放，防止正在读 GIF 的 UI 与格式化并发。
 * 命令中的 ERASE 是用户对 storage 数据损失的明确确认，不自动调用。 */
static int buddy_app_console_storage(void *ctx, int argc, char **argv)
{
    buddy_app_t *app = ctx;
    if (argc != 3 || strcmp(argv[1], "format") != 0 || strcmp(argv[2], "ERASE") != 0) {
        fprintf(stdout, "Storage recovery deletes all character packs and stored fonts.\n"
                        "Only available when storage is unavailable.\nUsage: storage format ERASE\n");
        return 1;
    }
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    bool mounted = app->charpack != NULL;
    xSemaphoreGive(app->mutex);
    if (mounted) {
        fprintf(stdout, "Storage is mounted; refusing concurrent format.\n");
        return 1;
    }
    esp_err_t err = esp_vfs_fat_spiflash_format_rw_wl(CONFIG_EXAMPLE_CHARPACK_MOUNT_POINT,
                                                      CONFIG_EXAMPLE_CHARPACK_SPIFFS_PARTITION_LABEL);
    fprintf(stdout, "Storage format: %s; settings and bonds are outside storage.\n", esp_err_to_name(err));
    fflush(stdout);
    if (err == ESP_OK) esp_restart();
    return err == ESP_OK ? 0 : 1;
}

static const example_console_command_t s_buddy_app_commands[] = {
    { .command = "storage", .help = "Explicit recovery of unavailable storage; erases packs and fonts.",
      .hint = "format ERASE", .handler = buddy_app_console_storage },
    {
        .command = "packs",
        .help = "List installed character packs.",
        .hint = NULL,
        .handler = buddy_app_charpack_console_packs,
    },
    {
        .command = "pack",
        .help = "Manage the active character pack.",
        .hint = "use <index>",
        .handler = buddy_app_charpack_console_pack,
    },
    {
        .command = "diag",
        .help = "Print heap, task and app diagnostics.",
        .hint = NULL,
        .handler = buddy_app_console_diag,
    },
    {
        .command = "reset",
        .help = "Reset persisted settings.",
        .hint = "settings",
        .handler = buddy_app_console_reset,
    },
};

static example_console_config_t s_console = {
    .prompt = "buddy> ",
    .banner =
        "ESP32-S3 Buddy console\n"
        "Type 'help' to list commands.\n",
    .commands = s_buddy_app_commands,
    .command_count = sizeof(s_buddy_app_commands) / sizeof(s_buddy_app_commands[0]),
    .common_cmds = &s_common_console_cmds,
    .ctx = NULL,
    .task_stack_size = BUDDY_APP_CONSOLE_STACK,
    .task_priority = 4,
};

esp_err_t buddy_app_charpack_console_start(buddy_app_t *app)
{
    esp_err_t err;

    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_console.ctx = app;
    s_common_console_cmds.mutex = app->mutex;
    s_common_console_cmds.buddy = app->buddy;
    s_common_console_cmds.transport = app->transport;
    s_common_console_cmds.state_cache = &app->state_cache;
    s_common_console_cmds.state_ctx = app;
    err = example_console_start(&s_console);
    if (err != ESP_OK) {
        ESP_LOGE("buddy_console", "console unavailable: %s", esp_err_to_name(err));
    }
    return err;
}
