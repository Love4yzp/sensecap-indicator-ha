#include "cmd.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"

#include "home_assistant_config.h"
#include "ha.h"
#include "storage_nvs.h"
#include "indicator_util.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "CMD_RESP";

#define PROMPT_STR "Indicator"

ESP_EVENT_DEFINE_BASE(CMD_CFG_EVENT_BASE);
esp_event_loop_handle_t cmd_cfg_event_handle;

static ha_cfg_interface ha_cfg;

static void print_mqtt_usage(void) {
    printf("\nMQTT configuration\n");
    printf("  Show current config:\n");
    printf("    haconfig\n\n");
    printf("  Set broker, client id and credentials:\n");
    printf("    setmqtt -a 192.168.1.10 -c indicator-01 -u mqtt_user -p mqtt_password\n");
    printf("    setmqtt --addr mqtt://192.168.1.10:1883\n");
    printf("    setmqtt --addr mqtt://broker.emqx.io\n\n");
    printf("  Notes:\n");
    printf("    - The screen MQTT page only asks for the broker IP. It builds mqtt://<ip>:1883.\n");
    printf("    - Restart is automatic after setmqtt succeeds.\n\n");
    printf("MQTT topics and payloads\n");
    printf("  Sensor data from device:\n");
    printf("    topic: %s\n", CONFIG_TOPIC_SENSOR_DATA);
    printf("    data : {\"temp\":\"23.5\"}, {\"humidity\":\"55\"}, {\"co2\":\"600\"}, {\"tvoc\":\"12\"}\n\n");
    printf("  Control device from Home Assistant/MQTT client:\n");
    printf("    topic: %s\n", CONFIG_TOPIC_SWITCH_SET);
    printf("    data : {\"switch1\":1}, {\"switch1\":0}, {\"switch5\":24}, {\"switch8\":50}\n\n");
    printf("  Device publishes control state:\n");
    printf("    topic: %s\n", CONFIG_TOPIC_SWITCH_STATE);
    printf("    data : {\"switch1\":1}, {\"switch5\":24}, {\"switch8\":50}\n\n");
}

static bool normalize_broker_url(const char *input, char *output, size_t output_size) {
    if (!input || input[0] == '\0' || !output || output_size == 0) return false;

    int written = 0;
    if (strncmp(input, "mqtt://", 7) == 0 || strncmp(input, "mqtts://", 8) == 0)
        written = snprintf(output, output_size, "%s", input);
    else
        written = snprintf(output, output_size, "mqtt://%s", input);

    return written > 0 && (size_t)written < output_size;
}

static int read_ha_config(int argc, char **argv) {
    ha_cfg_get(&ha_cfg);
    ESP_LOGI(TAG, "| Broker Address               | %-40s |", ha_cfg.broker_url);
    ESP_LOGI(TAG, "| Client ID                    | %-40s |", ha_cfg.client_id);
    ESP_LOGI(TAG, "| MQTT username                | %-40s |", ha_cfg.username);
    ESP_LOGI(TAG, "| MQTT password                | %-40s |", ha_cfg.password[0] ? "****" : "(not set)");
    ESP_LOGI(TAG, "Run 'mqtthelp' for setmqtt examples and MQTT topic/payload examples.");
    return 0;
}

static int mqtt_help(int argc, char **argv) {
    print_mqtt_usage();
    return 0;
}

static void register_read_config(void) {
    const esp_console_cmd_t cmd = {
        .command = "haconfig",
        .help    = "Show current MQTT broker/client configuration",
        .hint    = NULL,
        .func    = &read_ha_config,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_mqtt_help(void) {
    const esp_console_cmd_t cmd = {
        .command = "mqtthelp",
        .help    = "Show MQTT setup examples, topics and payloads",
        .hint    = NULL,
        .func    = &mqtt_help,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

struct {
    struct arg_str *username;
    struct arg_str *password;
    struct arg_str *broker_url;
    struct arg_str *client_id;
    struct arg_end *end;
} mqtt_args;

static int mqtt_config_set(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&mqtt_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, mqtt_args.end, argv[0]);
        print_mqtt_usage();
        return 1;
    }

    if (!mqtt_args.username->count && !mqtt_args.password->count &&
        !mqtt_args.broker_url->count && !mqtt_args.client_id->count) {
        print_mqtt_usage();
        return 0;
    }

    ha_cfg_get(&ha_cfg);

    if (mqtt_args.username->count > 0) {
        memset(ha_cfg.username, 0, sizeof(ha_cfg.username));
        strncpy(ha_cfg.username, mqtt_args.username->sval[0], sizeof(ha_cfg.username) - 1);
        ESP_LOGI(TAG, "Set MQTT username: %s", ha_cfg.username);
    }
    if (mqtt_args.password->count > 0) {
        memset(ha_cfg.password, 0, sizeof(ha_cfg.password));
        strncpy(ha_cfg.password, mqtt_args.password->sval[0], sizeof(ha_cfg.password) - 1);
        ESP_LOGI(TAG, "Set MQTT password: %s", ha_cfg.password[0] ? "****" : "(not set)");
    }
    if (mqtt_args.broker_url->count > 0) {
        char broker_url[sizeof(ha_cfg.broker_url)];
        if (!normalize_broker_url(mqtt_args.broker_url->sval[0], broker_url, sizeof(broker_url))) {
            ESP_LOGE(TAG, "Invalid or too long broker URL: %s", mqtt_args.broker_url->sval[0]);
            return 1;
        }
        memset(ha_cfg.broker_url, 0, sizeof(ha_cfg.broker_url));
        strncpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url) - 1);
        ESP_LOGI(TAG, "Set MQTT broker URL: %s", ha_cfg.broker_url);
    }
    if (mqtt_args.client_id->count > 0) {
        memset(ha_cfg.client_id, 0, sizeof(ha_cfg.client_id));
        strncpy(ha_cfg.client_id, mqtt_args.client_id->sval[0], sizeof(ha_cfg.client_id) - 1);
        ESP_LOGI(TAG, "Set MQTT client ID: %s", ha_cfg.client_id);
    }

    if (ha_cfg_set(&ha_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MQTT configuration");
        return 1;
    }
    esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_SET, NULL, 0, portMAX_DELAY);
    ESP_LOGI(TAG, "MQTT configuration saved. Reconnecting MQTT client.");
    return 0;
}

static void register_mqtt_config(void) {
    mqtt_args.username   = arg_str0("u", "usr", "<username>", "MQTT username");
    mqtt_args.password   = arg_str0("p", "psw", "<password>", "MQTT password");
    mqtt_args.broker_url = arg_str0("a", "addr", "<broker_url>",
                                    "MQTT broker URL, e.g. 192.168.1.10 or mqtt://host:1883");
    mqtt_args.client_id  = arg_str0("c", "id", "<client_id>", "MQTT client ID");
    mqtt_args.end        = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command  = "setmqtt",
        .help     = "Set MQTT config. Example: setmqtt -a 192.168.1.10 -c indicator-01 -u user -p pass",
        .hint     = NULL,
        .func     = &mqtt_config_set,
        .argtable = &mqtt_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

int indicator_cmd_init(void) {
    esp_console_repl_t       *repl        = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt                    = PROMPT_STR ">";
    repl_config.max_cmdline_length        = 1024;

    esp_event_loop_args_t cmd_event_task_args = {
        .queue_size      = 2,
        .task_name       = "cmd_event_task",
        .task_priority   = uxTaskPriorityGet(NULL),
        .task_stack_size = 4096,
        .task_core_id    = tskNO_AFFINITY,
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&cmd_event_task_args, &cmd_cfg_event_handle));

    ha_cfg_get(&ha_cfg);
    register_read_config();
    register_mqtt_help();
    register_mqtt_config();

    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return ESP_OK;
}
