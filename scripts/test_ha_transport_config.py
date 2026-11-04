#!/usr/bin/env python3
"""Host tests for HA transport configuration, MQTT lifetime, and LAN state."""

from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CFLAGS = [
    "cc",
    "-D_POSIX_C_SOURCE=200809L",
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unused-parameter",
]

ESP_ERR_STUB = r"""
#ifndef ESP_ERR_H
#define ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_INVALID_STATE 4
#define ESP_ERR_NVS_NOT_FOUND 5
#endif
"""

NATIVE_HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ha_config.h"
#include "native_config.h"

struct stored_blob {
    bool present;
    size_t len;
    uint8_t data[64];
};

static struct stored_blob enabled_blob;
static struct stored_blob psk_blob;
static esp_err_t psk_read_error;

static struct stored_blob *blob_for(const char *key)
{
    if (strcmp(key, "native-enabled") == 0) {
        return &enabled_blob;
    }
    if (strcmp(key, "native-psk") == 0) {
        return &psk_blob;
    }
    return NULL;
}

esp_err_t indicator_nvs_read(char *key, void *data, size_t *len)
{
    struct stored_blob *blob = blob_for(key);
    assert(blob != NULL);
    if (blob == &psk_blob && psk_read_error != ESP_OK) {
        return psk_read_error;
    }
    if (!blob->present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t copy_len = *len < blob->len ? *len : blob->len;
    memcpy(data, blob->data, copy_len);
    *len = blob->len;
    return ESP_OK;
}

esp_err_t indicator_nvs_write(char *key, void *data, size_t len)
{
    struct stored_blob *blob = blob_for(key);
    assert(blob != NULL);
    assert(len <= sizeof(blob->data));
    memcpy(blob->data, data, len);
    blob->len = len;
    blob->present = true;
    return ESP_OK;
}

static void assert_zero(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        assert(data[i] == 0);
    }
}

int main(void)
{
    _Static_assert(sizeof(ha_cfg_interface) == 240, "legacy NVS blob size");
    _Static_assert(offsetof(ha_cfg_interface, client_id) == 128, "legacy layout");
    _Static_assert(offsetof(ha_cfg_interface, username) == 144, "legacy layout");
    _Static_assert(offsetof(ha_cfg_interface, password) == 176, "legacy layout");
    assert(strcmp(MQTT_HA_CFG_STORAGE, "ha-mqtt") == 0);
    assert(strcmp(MQTT_HA_ENABLED_STORAGE, "mqtt-enabled") == 0);

    uint8_t psk[NATIVE_CONFIG_PSK_SIZE];
    memset(psk, 0xa5, sizeof(psk));
    assert(native_config_enabled_get());
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_NOT_CONFIGURED);
    assert_zero(psk, sizeof(psk));
    assert(native_config_psk_read(NULL) == NATIVE_CONFIG_PSK_ERROR);

    assert(native_config_enabled_set(true) == ESP_OK);
    assert(native_config_enabled_get());
    assert(enabled_blob.len == 1 && enabled_blob.data[0] == 1);
    assert(native_config_enabled_set(false) == ESP_OK);
    assert(!native_config_enabled_get());

    uint8_t expected[NATIVE_CONFIG_PSK_SIZE];
    for (size_t i = 0; i < sizeof(expected); ++i) {
        expected[i] = (uint8_t)i;
    }
    const char *encoded = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
    assert(strlen(encoded) == NATIVE_CONFIG_PSK_BASE64_LEN);
    assert(native_config_psk_decode_base64(encoded, psk) == ESP_OK);
    assert(memcmp(psk, expected, sizeof(psk)) == 0);
    assert(native_config_psk_set_base64(encoded) == ESP_OK);
    assert(psk_blob.present && psk_blob.len == NATIVE_CONFIG_PSK_SIZE);
    assert(memcmp(psk_blob.data, expected, sizeof(expected)) == 0);
    memset(psk, 0, sizeof(psk));
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_CONFIGURED);
    assert(memcmp(psk, expected, sizeof(psk)) == 0);
    assert(!native_config_enabled_get());

    const char *invalid[] = {
        "",
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8",
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh9=",
        "AAEC*wQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh9=",
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8==",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memset(psk, 0xa5, sizeof(psk));
        assert(native_config_psk_decode_base64(invalid[i], psk) != ESP_OK);
        assert(native_config_psk_set_base64(invalid[i]) != ESP_OK);
        assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_CONFIGURED);
        assert(memcmp(psk, expected, sizeof(psk)) == 0);
    }
    assert(native_config_psk_decode_base64(NULL, psk) != ESP_OK);
    assert(native_config_psk_decode_base64(encoded, NULL) != ESP_OK);
    assert(native_config_psk_set(NULL) != ESP_OK);
    uint8_t zero[NATIVE_CONFIG_PSK_SIZE] = {0};
    assert(native_config_psk_set(zero) != ESP_OK);
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_CONFIGURED);
    assert(memcmp(psk, expected, sizeof(psk)) == 0);

    assert(native_config_psk_clear() == ESP_OK);
    memset(psk, 0xa5, sizeof(psk));
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_NOT_CONFIGURED);
    assert_zero(psk, sizeof(psk));

    const size_t bad_lengths[] = {0, 2, 31, 33};
    for (size_t i = 0; i < sizeof(bad_lengths) / sizeof(bad_lengths[0]); ++i) {
        psk_blob.present = true;
        psk_blob.len = bad_lengths[i];
        psk_blob.data[0] = 1;
        memset(psk, 0xa5, sizeof(psk));
        assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_ERROR);
        assert_zero(psk, sizeof(psk));
    }
    psk_blob.len = 1;
    psk_blob.data[0] = 1;
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_ERROR);
    psk_blob.len = NATIVE_CONFIG_PSK_SIZE;
    memset(psk_blob.data, 0, sizeof(psk_blob.data));
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_ERROR);
    psk_read_error = ESP_FAIL;
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_ERROR);
    assert_zero(psk, sizeof(psk));
    psk_read_error = ESP_OK;
    memcpy(psk_blob.data, expected, sizeof(expected));
    assert(native_config_psk_read(psk) == NATIVE_CONFIG_PSK_CONFIGURED);
    assert(memcmp(psk, expected, sizeof(psk)) == 0);
    assert(native_config_enabled_set(true) == ESP_OK);
    assert(native_config_enabled_get());
    return 0;
}
"""

ESP_EVENT_STUB = r"""
#ifndef ESP_EVENT_H
#define ESP_EVENT_H
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef const char *esp_event_base_t;
typedef void *esp_event_loop_handle_t;
typedef void *esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(
    void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
typedef struct {
    int queue_size;
    const char *task_name;
    unsigned task_priority;
    unsigned task_stack_size;
    int task_core_id;
} esp_event_loop_args_t;
#define ESP_EVENT_ANY_ID (-1)
#define ESP_EVENT_DECLARE_BASE(base) extern esp_event_base_t base
#define ESP_EVENT_DEFINE_BASE(base) esp_event_base_t base = #base
#define ESP_ERROR_CHECK(call) do { if ((call) != ESP_OK) __builtin_trap(); } while (0)
esp_err_t esp_event_loop_create(
    const esp_event_loop_args_t *args, esp_event_loop_handle_t *handle);
esp_err_t esp_event_handler_instance_register_with(
    esp_event_loop_handle_t handle, esp_event_base_t base, int32_t event_id,
    esp_event_handler_t handler, void *handler_args,
    esp_event_handler_instance_t *instance);
esp_err_t esp_event_post_to(
    esp_event_loop_handle_t handle, esp_event_base_t base, int32_t event_id,
    void *event_data, size_t event_data_size, uint32_t ticks_to_wait);
#endif
"""

MQTT_CLIENT_STUB = r"""
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H
#include "esp_err.h"
typedef void *esp_mqtt_client_handle_t;
typedef struct { int unused; } esp_mqtt_client_config_t;
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_publish(
    esp_mqtt_client_handle_t client, const char *topic, const char *data,
    int data_len, int qos, int retain);
#endif
"""

ESP_LOG_STUB = r"""
#ifndef ESP_LOG_H
#define ESP_LOG_H
#define ESP_LOGI(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGE(tag, ...) do { (void)(tag); } while (0)
#endif
"""

FREERTOS_STUB = r"""
#ifndef FREERTOS_H
#define FREERTOS_H
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define tskNO_AFFINITY 0
#endif
"""

TASK_STUB = r"""
#ifndef TASK_H
#define TASK_H
static inline unsigned uxTaskPriorityGet(void *task) { (void)task; return 1; }
#endif
"""

MQTT_HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mqtt.h"
#include "view_event.h"

typedef struct {
    int32_t id;
    union {
        mqtt_data_t publish;
        instance_mqtt_t instance;
    } payload;
} test_event_t;

static view_event_handler_t wifi_handler;
static esp_event_handler_t app_handler;
static unsigned start_calls;
static unsigned stop_calls;
static unsigned client_stop_calls;
static unsigned client_destroy_calls;
static unsigned connected_calls;
static unsigned post_calls;
static unsigned publish_calls;
static unsigned wake_failures;
static uint32_t last_post_timeout = UINT32_MAX;
static test_event_t queued[MQTT_APP_EVENT_QUEUE_SIZE];
static size_t queued_count;
static char published_topics[64][64];
static char published_data[64][256];
static unsigned published_connected_calls[64];
static bool connect_on_start;

esp_err_t esp_event_loop_create(
    const esp_event_loop_args_t *args, esp_event_loop_handle_t *handle)
{
    assert(args->queue_size == MQTT_APP_EVENT_QUEUE_SIZE);
    *handle = (void *)1;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register_with(
    esp_event_loop_handle_t handle, esp_event_base_t base, int32_t event_id,
    esp_event_handler_t handler, void *handler_args,
    esp_event_handler_instance_t *instance)
{
    assert(base == MQTT_APP_EVENT_BASE);
    app_handler = handler;
    return ESP_OK;
}

esp_err_t esp_event_post_to(
    esp_event_loop_handle_t handle, esp_event_base_t base, int32_t event_id,
    void *event_data, size_t event_data_size, uint32_t ticks_to_wait)
{
    assert(handle != NULL && base == MQTT_APP_EVENT_BASE);
    assert(event_data != NULL && ticks_to_wait == 0);
    last_post_timeout = ticks_to_wait;
    post_calls++;
    if (queued_count == MQTT_APP_EVENT_QUEUE_SIZE) {
        if (event_id == MQTT_APP_CONTROL) {
            wake_failures++;
        }
        return ESP_FAIL;
    }

    test_event_t *event = &queued[queued_count++];
    event->id = event_id;
    if (event_id == MQTT_APP_PUBLISH) {
        assert(event_data_size == sizeof(mqtt_data_t));
        event->payload.publish = *(const mqtt_data_t *)event_data;
    } else {
        assert(event_id == MQTT_APP_CONTROL);
        assert(event_data_size == sizeof(instance_mqtt_t));
        event->payload.instance = *(const instance_mqtt_t *)event_data;
    }
    return ESP_OK;
}

bool view_event_subscribe(
    enum view_event_id id, view_event_handler_t handler, void *context)
{
    assert(id == VIEW_EVENT_WIFI_ST);
    wifi_handler = handler;
    return true;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client)
{
    assert(client != NULL);
    client_stop_calls++;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client)
{
    assert(client != NULL);
    client_destroy_calls++;
    return ESP_OK;
}

int esp_mqtt_client_publish(
    esp_mqtt_client_handle_t client, const char *topic, const char *data,
    int data_len, int qos, int retain)
{
    assert(client != NULL && qos == 0 && retain == 0);
    assert(data_len >= 0 && data_len < 256);
    assert(publish_calls < 64);
    snprintf(published_topics[publish_calls], 64, "%s", topic);
    memcpy(published_data[publish_calls], data, (size_t)data_len);
    published_data[publish_calls][data_len] = '\0';
    published_connected_calls[publish_calls] = connected_calls;
    publish_calls++;
    return (int)publish_calls;
}

static void starter(instance_mqtt *instance)
{
    mqtt_instance_stop(instance);
    start_calls++;
    instance->mqtt_client = (void *)0x100;
    instance->mqtt_connected_flag = connect_on_start;
}

static void stopper(instance_mqtt *instance)
{
    stop_calls++;
}

static void connected_handler(instance_mqtt *instance)
{
    assert(instance->mqtt_client != NULL);
    connected_calls++;
}

static void wifi_state(bool has_ip, bool is_network)
{
    struct view_data_wifi_st state = {
        .has_ip = has_ip,
        .is_network = is_network,
    };
    wifi_handler(NULL, VIEW_EVENT_WIFI_ST, &state);
}

static void drain_one(void)
{
    assert(queued_count > 0);
    test_event_t event = queued[0];
    queued_count--;
    memmove(queued, &queued[1], queued_count * sizeof(queued[0]));
    void *payload = event.id == MQTT_APP_PUBLISH
        ? (void *)&event.payload.publish : (void *)&event.payload.instance;
    app_handler(NULL, MQTT_APP_EVENT_BASE, event.id, payload);
}

static void drain_all(void)
{
    while (queued_count > 0) {
        drain_one();
    }
}

static void queue_message(
    instance_mqtt *instance, const char *topic, const char *data)
{
    assert(mqtt_publish_enqueue(instance, topic, data, strlen(data)) == ESP_OK);
}

static void fill_publish(instance_mqtt *instance)
{
    assert(queued_count == 0);
    for (unsigned i = 0; i < MQTT_APP_EVENT_QUEUE_SIZE; ++i) {
        queue_message(instance, "indicator/switch/state", "fill");
    }
    assert(queued_count == MQTT_APP_EVENT_QUEUE_SIZE);
}

int main(void)
{
    assert(indicator_mqtt_init() == ESP_OK);
    assert(wifi_handler != NULL && app_handler != NULL);
    instance_mqtt instance = {
        .mqtt_name = "host-test",
        .mqtt_starter = starter,
        .mqtt_stopper = stopper,
        .mqtt_connected_handler = connected_handler,
        .is_using = true,
    };

    wifi_state(true, false);
    assert(get_mqtt_net_flag());
    mqtt_instance_request(&instance, MQTT_APP_START);
    drain_one();
    assert(start_calls == 1 && instance.mqtt_client != NULL);
    assert(client_destroy_calls == 0);
    mqtt_instance_request(&instance, MQTT_APP_START);
    drain_one();
    assert(start_calls == 1);

    instance.mqtt_connected_flag = true;
    mqtt_instance_connected(&instance, instance.mqtt_client);
    drain_one();
    assert(connected_calls == 1);

    char mutable_data[] = "{\"switch1\": 1}";
    queue_message(&instance, "indicator/switch/state", mutable_data);
    queue_message(&instance, "indicator/sensor", "{\"temp\":\"23.5\"}");
    assert(last_post_timeout == 0 && queued_count == 2);
    memset(mutable_data, 0, strlen(mutable_data));
    drain_all();
    assert(publish_calls == 2);
    assert(strcmp(published_topics[0], "indicator/switch/state") == 0);
    assert(strcmp(published_data[0], "{\"switch1\": 1}") == 0);
    assert(strcmp(published_topics[1], "indicator/sensor") == 0);
    assert(strcmp(published_data[1], "{\"temp\":\"23.5\"}") == 0);

    instance.mqtt_connected_flag = false;
    queue_message(&instance, "indicator/switch/state", "offline");
    drain_one();
    assert(publish_calls == 2);
    instance.mqtt_connected_flag = true;

    fill_publish(&instance);
    mqtt_instance_connected(&instance, instance.mqtt_client);
    assert(queued_count == MQTT_APP_EVENT_QUEUE_SIZE && wake_failures == 1);
    drain_one();
    assert(connected_calls == 2 && publish_calls == 3);
    assert(published_connected_calls[2] == 2);
    drain_all();
    assert(publish_calls == 18);

    fill_publish(&instance);
    mqtt_instance_request(&instance, MQTT_APP_STOP);
    assert(queued_count == MQTT_APP_EVENT_QUEUE_SIZE && wake_failures == 2);
    drain_one();
    assert(instance.mqtt_client == NULL && !instance.mqtt_connected_flag);
    assert(client_destroy_calls == 1 && publish_calls == 18);
    drain_all();
    mqtt_instance_request(&instance, MQTT_APP_STOP);
    drain_one();
    assert(client_destroy_calls == 1);

    fill_publish(&instance);
    mqtt_instance_request(&instance, MQTT_APP_START);
    assert(queued_count == MQTT_APP_EVENT_QUEUE_SIZE && wake_failures == 3);
    drain_one();
    assert(start_calls == 2 && instance.mqtt_client != NULL);
    assert(publish_calls == 18);
    drain_all();

    instance.mqtt_connected_flag = true;
    mqtt_instance_connected(&instance, instance.mqtt_client);
    mqtt_instance_request(&instance, MQTT_APP_RESTART);
    connect_on_start = true;
    drain_one();
    assert(start_calls == 3 && client_destroy_calls == 2);
    assert(connected_calls == 2);
    assert(instance.mqtt_client == (void *)0x100);
    drain_one();
    connect_on_start = false;
    mqtt_instance_connected(&instance, instance.mqtt_client);
    drain_one();
    assert(connected_calls == 3);

    fill_publish(&instance);
    mqtt_instance_request(&instance, MQTT_APP_RESTART);
    assert(queued_count == MQTT_APP_EVENT_QUEUE_SIZE && wake_failures == 4);
    drain_one();
    assert(start_calls == 4 && client_destroy_calls == 3);
    assert(publish_calls == 18);
    drain_all();

    fill_publish(&instance);
    assert(mqtt_publish_enqueue(
        &instance, "indicator/switch/state", "full", 4) == ESP_FAIL);
    assert(queued_count == MQTT_APP_EVENT_QUEUE_SIZE);
    drain_all();
    assert(publish_calls == 18);
    assert(mqtt_publish_enqueue(NULL, "topic", "data", 4) == ESP_ERR_INVALID_ARG);

    instance.is_using = false;
    mqtt_instance_request(&instance, MQTT_APP_RESTART);
    drain_one();
    assert(start_calls == 4);
    mqtt_instance_request(&instance, MQTT_APP_STOP);
    drain_one();
    assert(client_destroy_calls == 4);

    instance.is_using = true;
    wifi_state(false, true);
    assert(!get_mqtt_net_flag());
    mqtt_instance_request(&instance, MQTT_APP_START);
    drain_one();
    assert(start_calls == 4);
    wifi_state(true, false);
    mqtt_instance_request(&instance, MQTT_APP_RESTART);
    drain_one();
    assert(start_calls == 5);

    app_handler(NULL, MQTT_APP_EVENT_BASE, MQTT_APP_CONTROL, NULL);
    app_handler(NULL, MQTT_APP_EVENT_BASE, MQTT_APP_PUBLISH, NULL);
    mqtt_instance_request(NULL, MQTT_APP_STOP);
    mqtt_instance_request(&instance, MQTT_APP_PUBLISH);
    mqtt_instance_connected(NULL, instance.mqtt_client);
    mqtt_instance_connected(&instance, NULL);
    mqtt_instance_stop(NULL);
    assert(queued_count == 0 && stop_calls > 0 && post_calls > 0);
    return 0;
}
"""

WIFI_HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "wifi_model.h"

static void assert_zeroed(const struct view_data_wifi_st *state)
{
    const uint8_t *bytes = (const uint8_t *)state;
    for (size_t i = 0; i < sizeof(*state); ++i) {
        assert(bytes[i] == 0);
    }
}

int main(void)
{
    struct view_data_wifi_st state;
    memset(&state, 0xff, sizeof(state));
    wifi_model_state_reset(&state);
    assert_zeroed(&state);

    wifi_model_state_start(&state);
    assert(state.is_connecting);
    assert(!state.is_connected && !state.has_ip && !state.is_network);

    wifi_model_state_got_ip(&state);
    assert(state.is_connected && state.has_ip);
    assert(!state.is_connecting && !state.is_network);

    state.is_network = true;
    wifi_model_state_disconnected(&state, true);
    assert(!state.is_connected && !state.has_ip && !state.is_network);
    assert(state.is_connecting);

    state.has_ip = true;
    state.is_network = true;
    wifi_model_state_disconnected(&state, false);
    assert(!state.has_ip && !state.is_network && !state.is_connecting);

    memset(&state, 0xff, sizeof(state));
    wifi_model_state_reset(&state);
    assert_zeroed(&state);
    return 0;
}
"""


def compile_and_run(
    tmp: Path,
    name: str,
    harness_source: str,
    sources: list[Path],
    includes: list[Path],
    stubs: dict[str, str] | None = None,
) -> None:
    harness = tmp / f"{name}.c"
    binary = tmp / name
    harness.write_text(textwrap.dedent(harness_source), encoding="utf-8")
    for relative, content in (stubs or {}).items():
        path = tmp / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(content), encoding="utf-8")
    command = CFLAGS.copy()
    command += ["-I", str(tmp)]
    for include in includes:
        command += ["-I", str(include)]
    command += [str(harness), *(str(source) for source in sources), "-o", str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=2)


class NativeConfigTests(unittest.TestCase):
    def test_real_config_defaults_psk_validation_and_legacy_layout(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            compile_and_run(
                tmp,
                "native_config_test",
                NATIVE_HARNESS,
                [ROOT / "main" / "ha" / "native_config.c"],
                [ROOT / "main" / "ha"],
                {
                    "esp_err.h": ESP_ERR_STUB,
                    "storage_nvs.h": """
                        #ifndef STORAGE_NVS_H
                        #define STORAGE_NVS_H
                        #include "esp_err.h"
                        #include <stddef.h>
                        esp_err_t indicator_nvs_write(char *key, void *data, size_t len);
                        esp_err_t indicator_nvs_read(char *key, void *data, size_t *len);
                        #endif
                    """,
                },
            )

    def test_native_module_has_no_server_or_secret_output(self) -> None:
        source = (ROOT / "main" / "ha" / "native_config.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("native_api", source)
        self.assertNotIn("ESP_LOG", source)
        self.assertNotIn("printf", source)


class MqttLifecycleTests(unittest.TestCase):
    def test_real_controller_is_idempotent_and_uses_lan_not_wan(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            compile_and_run(
                tmp,
                "mqtt_lifecycle_test",
                MQTT_HARNESS,
                [ROOT / "main" / "mqtt" / "mqtt.c"],
                [ROOT / "main", ROOT / "main" / "mqtt", ROOT / "main" / "view_event"],
                {
                    "esp_err.h": ESP_ERR_STUB,
                    "esp_event.h": ESP_EVENT_STUB,
                    "mqtt_client.h": MQTT_CLIENT_STUB,
                    "esp_log.h": ESP_LOG_STUB,
                    "nvs.h": '#include "esp_err.h"\n',
                    "esp_wifi_types.h": "",
                    "esp_wifi.h": "",
                    "esp_mac.h": "",
                    "freertos/FreeRTOS.h": FREERTOS_STUB,
                    "freertos/queue.h": "",
                    "freertos/semphr.h": "",
                    "freertos/task.h": TASK_STUB,
                },
            )

    def test_ha_reconfiguration_uses_strict_config_and_common_stop(self) -> None:
        source = (ROOT / "main" / "ha" / "ha_mqtt.c").read_text(
            encoding="utf-8"
        )
        start = source.split("static void _mqtt_ha_start", 2)[2]
        start = start.split("int indicator_ha_model_init", 1)[0]
        self.assertIn("mqtt_instance_stop(instance);", start)
        self.assertIn("ha_cfg_get_stored(&hf_cfg)", start)
        self.assertNotIn("ha_cfg_get(&hf_cfg)", start)
        self.assertNotIn("esp_mqtt_client_set_uri", source)
        self.assertIn(".is_using = ha_mqtt_enabled_get(),", source)
        self.assertIn("wifi_status->has_ip", source)
        self.assertIn("mqtt_instance_connected(instance_ptr, client);", source)
        self.assertIn("mqtt_instance_request(instance_ptr, event);", source)
        self.assertNotIn("MQTT_APP_CONNECTED", source)
        self.assertIn(".mqtt_connected_handler = _mqtt_ha_connected,", source)
        self.assertNotIn("portMAX_DELAY", source)
        controller = (ROOT / "main" / "mqtt" / "mqtt.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("mqtt_control_dispatch(instance);", controller)
        self.assertIn("instance->generation != connected->generation", controller)
        self.assertNotIn("portMAX_DELAY", controller)

    def test_mqtt_enable_defaults_to_valid_legacy_config_only(self) -> None:
        source = (ROOT / "main" / "ha" / "ha_config.c").read_text(
            encoding="utf-8"
        )
        enabled = source.split("bool ha_mqtt_enabled_get(void)", 1)[1]
        enabled = enabled.split("esp_err_t ha_mqtt_enabled_set", 1)[0]
        self.assertIn("ESP_ERR_NVS_NOT_FOUND", enabled)
        self.assertIn("ha_cfg_get_stored(&stored_cfg) == ESP_OK", enabled)
        self.assertNotIn("CONFIG_BROKER_URL", enabled)
        stored = source.split("esp_err_t ha_cfg_get_stored", 1)[1]
        stored = stored.split("esp_err_t ha_cfg_get(", 1)[0]
        self.assertIn("len != sizeof(*ha_cfg)", stored)
        self.assertIn("!ha_cfg_is_valid(ha_cfg)", stored)


class WifiLanStateTests(unittest.TestCase):
    def test_real_state_helpers_cover_got_ip_disconnect_and_reconfigure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            compile_and_run(
                Path(directory),
                "wifi_lan_state_test",
                WIFI_HARNESS,
                [],
                [ROOT / "main", ROOT / "main" / "wifi"],
            )

    def test_every_lan_transition_is_published(self) -> None:
        source = (ROOT / "main" / "wifi" / "wifi_model.c").read_text(
            encoding="utf-8"
        )
        got_ip = source.split("static void _ip_event_handler", 1)[1]
        got_ip = got_ip.split("static int _wifi_connect", 1)[0]
        self.assertIn("wifi_model_state_got_ip(&st);", got_ip)
        self.assertIn("view_event_publish_state(", got_ip)
        disconnected = source.split("case WIFI_EVENT_STA_DISCONNECTED:", 1)[1]
        disconnected = disconnected.split("default:", 1)[0]
        self.assertIn("wifi_model_state_disconnected(&st, retrying);", disconnected)
        self.assertIn("view_event_publish_state(", disconnected)
        reconfigure = source.split("static int _wifi_connect", 1)[1]
        reconfigure = reconfigure.split("static void _wifi_cfg_restore", 1)[0]
        self.assertIn("wifi_model_state_reset(&st);", reconfigure)
        self.assertIn("view_event_publish_state(", reconfigure)
        self.assertIn("st.is_network = st.has_ip && received > 0;", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
