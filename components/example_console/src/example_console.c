/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "example_console.h"

#include "example_app_helpers.h"

typedef struct {
    example_console_cmd_fn_t handler;
    void *ctx;
} example_console_runtime_cmd_t;

static esp_console_repl_t *s_repl;
static example_console_runtime_cmd_t *s_runtime_cmds;
static size_t s_runtime_cmd_count;

static int example_console_invoke(void *context, int argc, char **argv)
{
    example_console_runtime_cmd_t *runtime = (example_console_runtime_cmd_t *)context;

    if (runtime == NULL || runtime->handler == NULL) {
        return 1;
    }

    return runtime->handler(runtime->ctx, argc, argv);
}

static esp_err_t example_console_register_command(size_t *slot,
                                                    const char *command,
                                                    const char *help,
                                                    const char *hint,
                                                    example_console_cmd_fn_t handler,
                                                    void *ctx)
{
    esp_console_cmd_t cmd = {
        .command = command,
        .help = help,
        .hint = hint,
        .func_w_context = example_console_invoke,
        .context = NULL,
    };

    if (slot == NULL || command == NULL || handler == NULL || s_runtime_cmds == NULL ||
        *slot >= s_runtime_cmd_count) {
        return ESP_ERR_INVALID_ARG;
    }

    s_runtime_cmds[*slot].handler = handler;
    s_runtime_cmds[*slot].ctx = ctx;
    cmd.context = &s_runtime_cmds[*slot];
    ++(*slot);
    return esp_console_cmd_register(&cmd);
}

static esp_err_t example_reply_current_prompt_and_log(
    const example_console_common_cmds_t *config,
    esp_desktop_buddy_permission_decision_t decision,
    const char *label)
{
    esp_err_t err = example_reply_current_prompt(config->mutex,
                                                   config->buddy,
                                                   config->transport,
                                                   config->state_cache,
                                                   decision);

    fprintf(stdout, "%s rc=%s\n", label, esp_err_to_name(err));
    fflush(stdout);
    return err;
}

static int example_console_status_cmd(void *ctx, int argc, char **argv)
{
    const example_console_common_cmds_t *config = (const example_console_common_cmds_t *)ctx;

    (void)argv;

    if (argc != 1) {
        fprintf(stdout, "Usage: status\n");
        fflush(stdout);
        return 1;
    }
    if (config == NULL || config->print_state == NULL) {
        fprintf(stdout, "ERR status unavailable\n");
        fflush(stdout);
        return 1;
    }

    config->print_state(config->state_ctx, stdout);
    fflush(stdout);
    return 0;
}

static int example_console_unpair_cmd(void *ctx, int argc, char **argv)
{
    const example_console_common_cmds_t *config = (const example_console_common_cmds_t *)ctx;
    esp_desktop_buddy_command_result_t result;

    (void)argv;

    if (argc != 1) {
        fprintf(stdout, "Usage: unpair\n");
        fflush(stdout);
        return 1;
    }
    if (config == NULL) {
        fprintf(stdout, "ERR unpair unavailable\n");
        fflush(stdout);
        return 1;
    }

    result = example_clear_bonds(config->transport);
    fprintf(stdout, "unpair rc=%s\n", esp_err_to_name(result.err));
    fflush(stdout);
    return result.err == ESP_OK ? 0 : 1;
}

static int example_console_reply_cmd(void *ctx, int argc, char **argv)
{
    const example_console_common_cmds_t *config = (const example_console_common_cmds_t *)ctx;
    esp_desktop_buddy_permission_decision_t decision;

    if (config == NULL) {
        fprintf(stdout, "ERR reply unavailable\n");
        fflush(stdout);
        return 1;
    }
    if (argc != 2) {
        fprintf(stdout, "Usage: reply once|deny\n");
        fflush(stdout);
        return 1;
    }

    if (strcmp(argv[1], "once") == 0) {
        decision = ESP_DESKTOP_BUDDY_PERMISSION_DECISION_ONCE;
    } else if (strcmp(argv[1], "deny") == 0) {
        decision = ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY;
    } else {
        fprintf(stdout, "Usage: reply once|deny\n");
        fflush(stdout);
        return 1;
    }

    return example_reply_current_prompt_and_log(config, decision, argv[1]) == ESP_OK ? 0 : 1;
}

void example_console_init(void)
{
}

esp_err_t example_console_start(example_console_config_t *config)
{
    esp_err_t err;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    size_t slot = 0;
    size_t common_count = config != NULL && config->common_cmds != NULL ? 3 : 0;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_repl != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    repl_config.prompt = config->prompt != NULL ? config->prompt : "buddy> ";
    repl_config.task_core_id = 0;
    if (config->task_stack_size != 0) {
        repl_config.task_stack_size = config->task_stack_size;
    }
    if (config->task_priority != 0) {
        repl_config.task_priority = config->task_priority;
    }
    if (config->max_cmdline_length != 0) {
        repl_config.max_cmdline_length = config->max_cmdline_length;
    }

    s_runtime_cmd_count = config->command_count + common_count;
    if (s_runtime_cmd_count != 0) {
        s_runtime_cmds = calloc(s_runtime_cmd_count, sizeof(*s_runtime_cmds));
        if (s_runtime_cmds == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &s_repl);
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &s_repl);
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err = esp_console_new_repl_uart(&hw_config, &repl_config, &s_repl);
#else
    err = ESP_ERR_NOT_SUPPORTED;
#endif
    if (err != ESP_OK) {
        goto fail;
    }

    if (config->common_cmds != NULL) {
        err = example_console_register_command(&slot,
                                                 "status",
                                                 "Dump current Buddy state and transport state.",
                                                 NULL,
                                                 example_console_status_cmd,
                                                 (void *)config->common_cmds);
        if (err != ESP_OK) {
            goto fail;
        }
        err = example_console_register_command(&slot,
                                                 "reply",
                                                 "Reply to the current prompt.",
                                                 "once|deny",
                                                 example_console_reply_cmd,
                                                 (void *)config->common_cmds);
        if (err != ESP_OK) {
            goto fail;
        }
        err = example_console_register_command(&slot,
                                                 "unpair",
                                                 "Clear stored BLE bonds.",
                                                 NULL,
                                                 example_console_unpair_cmd,
                                                 (void *)config->common_cmds);
        if (err != ESP_OK) {
            goto fail;
        }
    }

    for (size_t i = 0; i < config->command_count; ++i) {
        const example_console_command_t *command = &config->commands[i];

        err = example_console_register_command(&slot,
                                                 command->command,
                                                 command->help,
                                                 command->hint,
                                                 command->handler,
                                                 config->ctx);
        if (err != ESP_OK) {
            goto fail;
        }
    }

    if (config->banner != NULL) {
        fprintf(stdout, "%s", config->banner);
        fflush(stdout);
    }

    err = esp_console_start_repl(s_repl);
    if (err != ESP_OK) {
        goto fail;
    }

    return ESP_OK;

fail:
    if (s_repl != NULL) {
        s_repl->del(s_repl);
    }
    free(s_runtime_cmds);
    s_runtime_cmds = NULL;
    s_runtime_cmd_count = 0;
    s_repl = NULL;
    return err;
}
