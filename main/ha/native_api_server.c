#include "native_api_server.h"

#include "entity_core.h"
#include "native_api_protocol.h"
#include "native_api_noise.h"
#include "native_config.h"
#include "native_api/api.pb.h"
#include "native_api/native_api_messages.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define NATIVE_API_MAX_CLIENTS 2
#define NATIVE_API_TX_QUEUE_CAPACITY 8
#define NATIVE_API_TX_MAX_PAYLOAD API_PB_H_MAX_SIZE
#define NATIVE_API_TX_FRAME_CAPACITY \
    (NATIVE_API_TX_MAX_PAYLOAD + NATIVE_API_NOISE_DATA_HEADER_SIZE + \
     NATIVE_API_NOISE_MAC_SIZE + NATIVE_API_NOISE_FRAME_HEADER_SIZE)
#define NATIVE_API_RX_CHUNK_SIZE 512
#define NATIVE_API_SERVER_STACK_SIZE 10240
#define NATIVE_API_SERVER_PRIORITY 5
#define NATIVE_API_SELECT_TIMEOUT_MS 100
#define NATIVE_API_STOPPED_WAIT_MS 200
#define NATIVE_API_RETRY_DELAY_MS 5000
#define NATIVE_API_HANDSHAKE_TIMEOUT_MS 30000
#define NATIVE_API_KEEPALIVE_IDLE_MS 30000
#define NATIVE_API_KEEPALIVE_TIMEOUT_MS 15000
#define NATIVE_API_ALL_ENTITIES ((1u << ENTITY_INDEX_COUNT) - 1u)

_Static_assert(ENTITY_INDEX_COUNT <= 32, "Native dirty set must fit in uint32_t");
_Static_assert(NATIVE_API_TX_FRAME_CAPACITY <= UINT16_MAX,
               "Native transmit offsets must fit in uint16_t");

typedef struct {
    uint16_t size;
    uint16_t offset;
    uint8_t data[NATIVE_API_TX_FRAME_CAPACITY];
} native_api_tx_slot_t;

typedef struct {
    int fd;
    native_api_parser_t parser;
    native_api_noise_session_t noise;
    uint8_t payload[NATIVE_API_NOISE_RX_BUFFER_SIZE];
    native_api_tx_slot_t tx_slots[NATIVE_API_TX_QUEUE_CAPACITY];
    uint8_t tx_head;
    uint8_t tx_count;
    uint32_t dirty;
    TickType_t last_rx;
    TickType_t ping_sent;
    bool noise_required;
    bool hello_received;
    bool subscribed;
    bool listing;
    uint8_t list_next;
    bool close_after_write;
    bool remove;
    bool ping_outstanding;
} native_api_client_t;

typedef enum {
    NATIVE_API_SEND_OK = 0,
    NATIVE_API_SEND_FULL,
    NATIVE_API_SEND_ERROR,
} native_api_send_result_t;

typedef struct {
    char name[32];
    char friendly_name[64];
    char mac_address[18];
    char mac_compact[13];
    char version[32];
    char compilation_time[26];
} native_api_identity_t;

static const char *TAG = "native-api";
static native_api_client_t s_clients[NATIVE_API_MAX_CLIENTS];
static native_api_identity_t s_identity;
static TaskHandle_t s_task;
static int s_listen_fd = -1;
static volatile bool s_enabled;
static volatile bool s_has_ip;
static volatile bool s_running;
static volatile uint8_t s_client_count;
static volatile uint32_t s_control_generation;
static uint32_t s_applied_generation;
static uint32_t s_dirty;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_mdns_initialized;
static bool s_mdns_service_added;
static bool s_psk_blocked;
static bool s_noise_required;
static uint8_t s_psk[NATIVE_CONFIG_PSK_SIZE];
static TickType_t s_retry_at;
static bool s_initialized;

static void signal_server(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

static void core_observer(const entity_core_snapshot_t *snapshot, void *context)
{
    (void)context;
    if (snapshot == NULL || snapshot->index >= ENTITY_INDEX_COUNT) {
        return;
    }
    taskENTER_CRITICAL(&s_state_mux);
    s_dirty |= 1u << snapshot->index;
    taskEXIT_CRITICAL(&s_state_mux);
    signal_server();
}

static uint32_t take_dirty(void)
{
    taskENTER_CRITICAL(&s_state_mux);
    uint32_t dirty = s_dirty;
    s_dirty = 0;
    taskEXIT_CRITICAL(&s_state_mux);
    return dirty;
}

static void close_client(native_api_client_t *client)
{
    if (client->fd >= 0) {
        close(client->fd);
        if (s_client_count > 0) {
            s_client_count--;
        }
    }
    native_api_noise_session_cleanup(&client->noise);
    memset(client, 0, sizeof(*client));
    client->fd = -1;
}

static void stop_mdns(void)
{
    if (s_mdns_service_added) {
        esp_err_t err = mdns_service_remove("_esphomelib", "_tcp");
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "mDNS service removal failed: %s", esp_err_to_name(err));
        }
        s_mdns_service_added = false;
    }
    if (s_mdns_initialized) {
        mdns_free();
        s_mdns_initialized = false;
    }
}

static bool start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return false;
    }
    s_mdns_initialized = true;
    err = mdns_hostname_set(s_identity.name);
    if (err == ESP_OK) {
        err = mdns_instance_name_set(s_identity.friendly_name);
    }
    mdns_txt_item_t txt[7] = {
        {.key = "mac", .value = s_identity.mac_compact},
        {.key = "friendly_name", .value = s_identity.friendly_name},
        {.key = "version", .value = s_identity.version},
        {.key = "platform", .value = "ESP32"},
        {.key = "board", .value = "sensecap-indicator"},
        {.key = "network", .value = "wifi"},
    };
    size_t txt_count = 6;
    if (s_noise_required) {
        txt[txt_count++] = (mdns_txt_item_t){
            .key = "api_encryption",
            .value = NATIVE_API_NOISE_PROTOCOL_NAME,
        };
    }
    if (err == ESP_OK) {
        err = mdns_service_add(NULL, "_esphomelib", "_tcp",
                               NATIVE_API_DEFAULT_PORT, txt, txt_count);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS Native API advertisement failed: %s",
                 esp_err_to_name(err));
        stop_mdns();
        return false;
    }
    s_mdns_service_added = true;
    ESP_LOGI(TAG, "mDNS advertising %s.local on port %u", s_identity.name,
             NATIVE_API_DEFAULT_PORT);
    return true;
}

static void stop_server(void)
{
    stop_mdns();
    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    for (size_t i = 0; i < NATIVE_API_MAX_CLIENTS; ++i) {
        close_client(&s_clients[i]);
    }
    s_running = false;
    s_noise_required = false;
    memset(s_psk, 0, sizeof(s_psk));
    take_dirty();
}

static bool start_server(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) {
        ESP_LOGE(TAG, "Native listen socket creation failed: errno=%d", errno);
        return false;
    }
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
        ESP_LOGE(TAG, "Native listen socket setup failed: errno=%d", errno);
        close(fd);
        return false;
    }
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(NATIVE_API_DEFAULT_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, NATIVE_API_MAX_CLIENTS) < 0) {
        ESP_LOGE(TAG, "Native listen on port %u failed: errno=%d",
                 NATIVE_API_DEFAULT_PORT, errno);
        close(fd);
        return false;
    }
    s_listen_fd = fd;
    s_running = true;
    if (!start_mdns()) {
        ESP_LOGW(TAG, "Native API remains available by IP without mDNS");
    }
    ESP_LOGI(TAG, "Native API listening on port %u", NATIVE_API_DEFAULT_PORT);
    return true;
}

static bool control_is_current(uint32_t generation, bool enabled, bool has_ip)
{
    taskENTER_CRITICAL(&s_state_mux);
    bool current = generation == s_control_generation &&
                   enabled == s_enabled && has_ip == s_has_ip;
    taskEXIT_CRITICAL(&s_state_mux);
    return current;
}

static void control_snapshot(uint32_t *generation, bool *enabled, bool *has_ip)
{
    taskENTER_CRITICAL(&s_state_mux);
    *generation = s_control_generation;
    *enabled = s_enabled;
    *has_ip = s_has_ip;
    taskEXIT_CRITICAL(&s_state_mux);
}

static void restart_server_with_config(uint32_t generation, bool enabled, bool has_ip)
{
    stop_server();
    s_psk_blocked = false;
    if (!enabled || !has_ip) {
        s_applied_generation = generation;
        return;
    }

    native_config_psk_state_t psk_state = native_config_psk_read(s_psk);
    if (!control_is_current(generation, enabled, has_ip)) {
        return;
    }
    if (psk_state == NATIVE_CONFIG_PSK_ERROR) {
        s_noise_required = false;
        s_psk_blocked = true;
        s_applied_generation = generation;
        ESP_LOGE(TAG, "Native PSK read failed or is malformed; refusing to start");
        return;
    }
    s_noise_required = psk_state == NATIVE_CONFIG_PSK_CONFIGURED;
    if (!start_server()) {
        if (control_is_current(generation, enabled, has_ip)) {
            s_applied_generation = generation;
            s_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(NATIVE_API_RETRY_DELAY_MS);
        }
        return;
    }
    if (!control_is_current(generation, enabled, has_ip)) {
        stop_server();
        return;
    }
    s_applied_generation = generation;
}

static void apply_control(void)
{
    uint32_t generation;
    bool enabled;
    bool has_ip;
    control_snapshot(&generation, &enabled, &has_ip);
    if (generation != s_applied_generation) {
        restart_server_with_config(generation, enabled, has_ip);
    }
}

static void retry_server(void)
{
    uint32_t generation;
    bool enabled;
    bool has_ip;
    control_snapshot(&generation, &enabled, &has_ip);
    restart_server_with_config(generation, enabled, has_ip);
}

void native_api_server_set_enabled(bool enabled)
{
    taskENTER_CRITICAL(&s_state_mux);
    if (s_enabled != enabled) {
        s_enabled = enabled;
        s_control_generation++;
    }
    taskEXIT_CRITICAL(&s_state_mux);
    signal_server();
}

void native_api_server_set_has_ip(bool has_ip)
{
    taskENTER_CRITICAL(&s_state_mux);
    if (s_has_ip != has_ip) {
        s_has_ip = has_ip;
        s_control_generation++;
    }
    taskEXIT_CRITICAL(&s_state_mux);
    signal_server();
}

void native_api_server_configuration_changed(void)
{
    taskENTER_CRITICAL(&s_state_mux);
    s_control_generation++;
    taskEXIT_CRITICAL(&s_state_mux);
    signal_server();
}

void native_api_server_get_status(bool *running, bool *has_ip, uint8_t *client_count)
{
    if (running != NULL) {
        *running = s_running;
    }
    if (has_ip != NULL) {
        *has_ip = s_has_ip;
    }
    if (client_count != NULL) {
        *client_count = s_client_count;
    }
}

static native_api_send_result_t reserve_tx_slot(native_api_client_t *client,
                                                bool control,
                                                native_api_tx_slot_t **reserved)
{
    if (client->remove || client->close_after_write ||
        client->tx_count >= NATIVE_API_TX_QUEUE_CAPACITY ||
        (!control && client->tx_count >= NATIVE_API_TX_QUEUE_CAPACITY - 1)) {
        return NATIVE_API_SEND_FULL;
    }
    uint8_t index = (client->tx_head + client->tx_count) %
                    NATIVE_API_TX_QUEUE_CAPACITY;
    *reserved = &client->tx_slots[index];
    return NATIVE_API_SEND_OK;
}

static native_api_send_result_t commit_tx_slot(native_api_client_t *client,
                                               native_api_tx_slot_t *slot,
                                               size_t size)
{
    if (size > sizeof(slot->data) || size > UINT16_MAX) {
        return NATIVE_API_SEND_ERROR;
    }
    slot->size = (uint16_t)size;
    slot->offset = 0;
    client->tx_count++;
    return NATIVE_API_SEND_OK;
}

static native_api_send_result_t queue_raw_frame(native_api_client_t *client,
                                                const uint8_t *data, size_t size)
{
    native_api_tx_slot_t *slot;
    native_api_send_result_t result = reserve_tx_slot(client, true, &slot);
    if (result != NATIVE_API_SEND_OK) {
        return result;
    }
    if (size > sizeof(slot->data)) {
        return NATIVE_API_SEND_ERROR;
    }
    memcpy(slot->data, data, size);
    return commit_tx_slot(client, slot, size);
}

static native_api_send_result_t queue_frame(native_api_client_t *client,
                                            uint32_t message_type,
                                            const uint8_t *payload,
                                            size_t payload_size,
                                            bool control)
{
    native_api_tx_slot_t *slot;
    native_api_send_result_t result = reserve_tx_slot(client, control, &slot);
    if (result != NATIVE_API_SEND_OK) {
        return result;
    }
    size_t size = 0;
    if (client->noise_required) {
        native_api_noise_status_t status = native_api_noise_encode_message(
            &client->noise, message_type, payload, payload_size,
            slot->data, sizeof(slot->data), &size);
        if (status != NATIVE_API_NOISE_OK) {
            ESP_LOGW(TAG, "Native Noise encode failed: type=%lu status=%d",
                     (unsigned long)message_type, (int)status);
            return NATIVE_API_SEND_ERROR;
        }
    } else {
        native_api_status_t status = native_api_encode_frame(
            message_type, payload, payload_size, NATIVE_API_TX_MAX_PAYLOAD,
            slot->data, sizeof(slot->data), &size);
        if (status != NATIVE_API_STATUS_OK) {
            ESP_LOGW(TAG, "Native frame encode failed: type=%lu status=%d",
                     (unsigned long)message_type, (int)status);
            return NATIVE_API_SEND_ERROR;
        }
    }
    return commit_tx_slot(client, slot, size);
}

static native_api_send_result_t queue_empty(native_api_client_t *client,
                                            uint32_t message_type, bool control)
{
    return queue_frame(client, message_type, NULL, 0, control);
}

static native_api_send_result_t queue_proto(native_api_client_t *client,
                                            uint32_t message_type,
                                            const pb_msgdesc_t *fields,
                                            const void *message,
                                            bool control)
{
    uint8_t payload[NATIVE_API_TX_MAX_PAYLOAD];
    pb_ostream_t stream = pb_ostream_from_buffer(payload, sizeof(payload));
    if (!pb_encode(&stream, fields, message)) {
        ESP_LOGW(TAG, "Native protobuf encode failed: type=%lu: %s",
                 (unsigned long)message_type, PB_GET_ERROR(&stream));
        return NATIVE_API_SEND_ERROR;
    }
    return queue_frame(client, message_type, payload, stream.bytes_written,
                       control);
}

static void require_control(native_api_client_t *client,
                            native_api_send_result_t result)
{
    if (result != NATIVE_API_SEND_OK) {
        ESP_LOGW(TAG, "Closing Native client after control queue failure");
        client->remove = true;
    }
}

static bool decode_proto(const native_api_frame_t *frame,
                         const pb_msgdesc_t *fields, void *message)
{
    pb_istream_t stream = pb_istream_from_buffer(frame->payload,
                                                 frame->payload_length);
    if (!pb_decode(&stream, fields, message)) {
        ESP_LOGW(TAG, "Native protobuf decode failed: type=%lu: %s",
                 (unsigned long)frame->message_type, PB_GET_ERROR(&stream));
        return false;
    }
    return true;
}

static const entity_descriptor_t *find_entity(uint32_t key, entity_kind_t kind)
{
    for (uint8_t i = 0; i < ENTITY_INDEX_COUNT; ++i) {
        const entity_descriptor_t *entity = entity_registry_get(i);
        if (entity->native_key == key) {
            return entity->kind == kind ? entity : NULL;
        }
    }
    return NULL;
}

static void submit_command(uint32_t key, entity_kind_t kind, float value)
{
    const entity_descriptor_t *entity = find_entity(key, kind);
    if (entity == NULL) {
        ESP_LOGW(TAG, "Native command rejected: unknown key=%lu kind=%d",
                 (unsigned long)key, (int)kind);
        return;
    }
    entity_core_command_t command = {
        .index = entity->index,
        .value = value,
        .source = ENTITY_CORE_SOURCE_NATIVE,
    };
    entity_core_command_result_t result;
    entity_core_status_t status = entity_core_submit_command(&command, &result);
    if (status != ENTITY_CORE_STATUS_OK) {
        ESP_LOGW(TAG, "Native command rejected: key=%lu status=%d",
                 (unsigned long)key, (int)status);
    }
}

static void send_hello(native_api_client_t *client)
{
    HelloResponse response = HelloResponse_init_zero;
    response.api_version_major = 1;
    response.api_version_minor = 15;
    snprintf(response.server_info, sizeof(response.server_info),
             "SenseCAP Indicator Native API");
    snprintf(response.name, sizeof(response.name), "%s", s_identity.name);
    require_control(client,
                    queue_proto(client, NATIVE_API_MESSAGE_HELLO_RESPONSE,
                                HelloResponse_fields, &response, true));
}

static void send_device_info(native_api_client_t *client)
{
    DeviceInfoResponse response = DeviceInfoResponse_init_zero;
    response.api_encryption_supported = s_noise_required;
    snprintf(response.name, sizeof(response.name), "%s", s_identity.name);
    snprintf(response.mac_address, sizeof(response.mac_address), "%s",
             s_identity.mac_address);
    snprintf(response.model, sizeof(response.model), "SenseCAP Indicator");
    snprintf(response.manufacturer, sizeof(response.manufacturer), "Seeed Studio");
    snprintf(response.friendly_name, sizeof(response.friendly_name), "%s",
             s_identity.friendly_name);
    snprintf(response.compilation_time, sizeof(response.compilation_time), "%s",
             s_identity.compilation_time);
    snprintf(response.project_name, sizeof(response.project_name),
             "Seeed Studio.sensecap-indicator-ha");
    snprintf(response.project_version, sizeof(response.project_version), "%s",
             s_identity.version);
    require_control(client,
                    queue_proto(client, NATIVE_API_MESSAGE_DEVICE_INFO_RESPONSE,
                                DeviceInfoResponse_fields, &response, true));
}

static native_api_send_result_t send_list_entity(native_api_client_t *client,
                                                 uint8_t index)
{
    const entity_descriptor_t *entity = entity_registry_get(index);
    if (entity == NULL) {
        return NATIVE_API_SEND_ERROR;
    }
    switch (entity->kind) {
        case ENTITY_KIND_SENSOR: {
            ListEntitiesSensorResponse response = ListEntitiesSensorResponse_init_zero;
            snprintf(response.object_id, sizeof(response.object_id), "%s",
                     entity->object_id);
            snprintf(response.name, sizeof(response.name), "%s", entity->name);
            snprintf(response.unit_of_measurement,
                     sizeof(response.unit_of_measurement), "%s",
                     entity->unit == NULL ? "" : entity->unit);
            response.key = entity->native_key;
            response.accuracy_decimals =
                index == ENTITY_INDEX_SENSOR_TEMP ||
                        index == ENTITY_INDEX_SENSOR_HUMIDITY
                    ? 1
                    : 0;
            response.state_class = SensorStateClass_STATE_CLASS_MEASUREMENT;
            return queue_proto(client,
                               NATIVE_API_MESSAGE_LIST_ENTITIES_SENSOR_RESPONSE,
                               ListEntitiesSensorResponse_fields, &response, false);
        }
        case ENTITY_KIND_SWITCH: {
            ListEntitiesSwitchResponse response = ListEntitiesSwitchResponse_init_zero;
            snprintf(response.object_id, sizeof(response.object_id), "%s",
                     entity->object_id);
            snprintf(response.name, sizeof(response.name), "%s", entity->name);
            response.key = entity->native_key;
            return queue_proto(client,
                               NATIVE_API_MESSAGE_LIST_ENTITIES_SWITCH_RESPONSE,
                               ListEntitiesSwitchResponse_fields, &response, false);
        }
        case ENTITY_KIND_NUMBER: {
            ListEntitiesNumberResponse response = ListEntitiesNumberResponse_init_zero;
            snprintf(response.object_id, sizeof(response.object_id), "%s",
                     entity->object_id);
            snprintf(response.name, sizeof(response.name), "%s", entity->name);
            snprintf(response.unit_of_measurement,
                     sizeof(response.unit_of_measurement), "%s",
                     entity->unit == NULL ? "" : entity->unit);
            response.key = entity->native_key;
            response.min_value = entity->min_value;
            response.max_value = entity->max_value;
            response.step = entity->step;
            response.mode = NumberMode_NUMBER_MODE_SLIDER;
            return queue_proto(client,
                               NATIVE_API_MESSAGE_LIST_ENTITIES_NUMBER_RESPONSE,
                               ListEntitiesNumberResponse_fields, &response, false);
        }
        default:
            return NATIVE_API_SEND_ERROR;
    }
}

static native_api_send_result_t send_state(native_api_client_t *client,
                                           uint8_t index)
{
    const entity_descriptor_t *entity = entity_registry_get(index);
    entity_core_snapshot_t snapshot;
    if (entity == NULL || !entity_core_get_snapshot(index, &snapshot)) {
        return NATIVE_API_SEND_ERROR;
    }
    switch (entity->kind) {
        case ENTITY_KIND_SENSOR: {
            SensorStateResponse response = SensorStateResponse_init_zero;
            response.key = entity->native_key;
            response.state = snapshot.valid ? snapshot.value : 0.0f;
            response.missing_state = !snapshot.valid;
            return queue_proto(client, NATIVE_API_MESSAGE_SENSOR_STATE_RESPONSE,
                               SensorStateResponse_fields, &response, false);
        }
        case ENTITY_KIND_SWITCH: {
            SwitchStateResponse response = SwitchStateResponse_init_zero;
            response.key = entity->native_key;
            response.state = snapshot.value != 0.0f;
            response.missing_state = !snapshot.valid;
            return queue_proto(client, NATIVE_API_MESSAGE_SWITCH_STATE_RESPONSE,
                               SwitchStateResponse_fields, &response, false);
        }
        case ENTITY_KIND_NUMBER: {
            NumberStateResponse response = NumberStateResponse_init_zero;
            response.key = entity->native_key;
            response.state = snapshot.valid ? snapshot.value : 0.0f;
            response.missing_state = !snapshot.valid;
            return queue_proto(client, NATIVE_API_MESSAGE_NUMBER_STATE_RESPONSE,
                               NumberStateResponse_fields, &response, false);
        }
        default:
            return NATIVE_API_SEND_ERROR;
    }
}

static void handle_frame(const native_api_frame_t *frame, void *context)
{
    native_api_client_t *client = context;
    if (client->remove || client->close_after_write) {
        return;
    }
    client->last_rx = xTaskGetTickCount();
    client->ping_outstanding = false;

    switch (frame->message_type) {
        case NATIVE_API_MESSAGE_HELLO_REQUEST: {
            HelloRequest request = HelloRequest_init_zero;
            if (!decode_proto(frame, HelloRequest_fields, &request)) {
                return;
            }
            client->hello_received = true;
            send_hello(client);
            return;
        }
        case NATIVE_API_MESSAGE_DISCONNECT_REQUEST:
            require_control(client,
                            queue_empty(client,
                                        NATIVE_API_MESSAGE_DISCONNECT_RESPONSE,
                                        true));
            client->close_after_write = !client->remove;
            return;
        case NATIVE_API_MESSAGE_DISCONNECT_RESPONSE:
            client->remove = true;
            return;
        case NATIVE_API_MESSAGE_PING_REQUEST:
            require_control(client,
                            queue_empty(client, NATIVE_API_MESSAGE_PING_RESPONSE,
                                        true));
            return;
        case NATIVE_API_MESSAGE_PING_RESPONSE:
            return;
        default:
            break;
    }

    if (!client->hello_received) {
        ESP_LOGW(TAG, "Ignoring Native message before Hello: type=%lu",
                 (unsigned long)frame->message_type);
        return;
    }

    switch (frame->message_type) {
        case NATIVE_API_MESSAGE_DEVICE_INFO_REQUEST:
            send_device_info(client);
            break;
        case NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_REQUEST: {
            DeviceCapabilitiesResponse response = DeviceCapabilitiesResponse_init_zero;
            require_control(client,
                            queue_proto(client,
                                        NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_RESPONSE,
                                        DeviceCapabilitiesResponse_fields, &response,
                                        true));
            break;
        }
        case NATIVE_API_MESSAGE_LIST_ENTITIES_REQUEST:
            client->listing = true;
            client->list_next = 0;
            break;
        case NATIVE_API_MESSAGE_SUBSCRIBE_STATES_REQUEST:
            client->subscribed = true;
            client->dirty = NATIVE_API_ALL_ENTITIES;
            break;
        case NATIVE_API_MESSAGE_SWITCH_COMMAND_REQUEST: {
            SwitchCommandRequest request = SwitchCommandRequest_init_zero;
            if (decode_proto(frame, SwitchCommandRequest_fields, &request)) {
                submit_command(request.key, ENTITY_KIND_SWITCH,
                               request.state ? 1.0f : 0.0f);
            }
            break;
        }
        case NATIVE_API_MESSAGE_NUMBER_COMMAND_REQUEST: {
            NumberCommandRequest request = NumberCommandRequest_init_zero;
            if (decode_proto(frame, NumberCommandRequest_fields, &request)) {
                submit_command(request.key, ENTITY_KIND_NUMBER, request.state);
            }
            break;
        }
        default:
            break;
    }
}

static void flush_client(native_api_client_t *client)
{
    if (client->fd < 0 || client->remove) {
        return;
    }
    uint8_t remaining_frames = client->tx_count;
    while (client->tx_count > 0 && remaining_frames-- > 0) {
        native_api_tx_slot_t *slot = &client->tx_slots[client->tx_head];
        ssize_t written = send(client->fd, slot->data + slot->offset,
                               slot->size - slot->offset, 0);
        if (written > 0) {
            slot->offset += (uint16_t)written;
            if (slot->offset == slot->size) {
                client->tx_head = (client->tx_head + 1) %
                                  NATIVE_API_TX_QUEUE_CAPACITY;
                client->tx_count--;
                continue;
            }
            return;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        client->remove = true;
        return;
    }
    if (client->close_after_write && client->tx_count == 0) {
        client->remove = true;
    }
}

static void produce_client(native_api_client_t *client)
{
    if (client->fd < 0 || client->remove || client->close_after_write) {
        return;
    }
    if (client->listing) {
        if (client->list_next < ENTITY_INDEX_COUNT) {
            native_api_send_result_t result = send_list_entity(client,
                                                               client->list_next);
            if (result == NATIVE_API_SEND_OK) {
                client->list_next++;
            } else if (result == NATIVE_API_SEND_ERROR) {
                client->remove = true;
                return;
            }
        } else {
            native_api_send_result_t result = queue_empty(
                client, NATIVE_API_MESSAGE_LIST_ENTITIES_DONE_RESPONSE, false);
            if (result == NATIVE_API_SEND_OK) {
                client->listing = false;
            } else if (result == NATIVE_API_SEND_ERROR) {
                client->remove = true;
                return;
            }
        }
    }
    if (client->subscribed && client->dirty != 0) {
        uint8_t index = (uint8_t)__builtin_ctz(client->dirty);
        native_api_send_result_t result = send_state(client, index);
        if (result != NATIVE_API_SEND_FULL) {
            client->dirty &= ~(1u << index);
        }
        if (result == NATIVE_API_SEND_ERROR) {
            ESP_LOGW(TAG, "Dropping Native state after encode/read failure: index=%u",
                     index);
        }
    }
}

static void accept_clients(void)
{
    while (s_listen_fd >= 0) {
        int fd = accept(s_listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(TAG, "Native accept failed: errno=%d", errno);
            }
            return;
        }
        native_api_client_t *client = NULL;
        for (size_t i = 0; i < NATIVE_API_MAX_CLIENTS; ++i) {
            if (s_clients[i].fd < 0) {
                client = &s_clients[i];
                break;
            }
        }
        if (client == NULL) {
            ESP_LOGW(TAG, "Native client limit reached; rejecting connection");
            close(fd);
            continue;
        }
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
            close(fd);
            continue;
        }
        memset(client, 0, sizeof(*client));
        client->fd = fd;
        client->last_rx = xTaskGetTickCount();
        client->noise_required = s_noise_required;
        if (client->noise_required) {
            native_api_noise_session_init(&client->noise, client->payload,
                                          sizeof(client->payload));
        } else if (native_api_parser_init(&client->parser, client->payload,
                                          sizeof(client->payload),
                                          NATIVE_API_DEFAULT_MAX_PAYLOAD_SIZE) !=
                   NATIVE_API_STATUS_OK) {
            close_client(client);
            continue;
        }
        s_client_count++;
        ESP_LOGI(TAG, "Native client connected (%u/%u)", s_client_count,
                 NATIVE_API_MAX_CLIENTS);
    }
}

static void handle_noise_frame(uint8_t *frame, size_t frame_size, void *context)
{
    native_api_client_t *client = context;
    client->last_rx = xTaskGetTickCount();
    client->ping_outstanding = false;
    uint8_t response[NATIVE_API_NOISE_MAX_HANDSHAKE_FRAME_SIZE];
    native_api_frame_t message;
    bool message_available = false;
    size_t response_size = 0;
    native_api_noise_status_t status = native_api_noise_process_frame(
        &client->noise, frame, frame_size, s_identity.name,
        s_identity.mac_compact, s_psk, response, sizeof(response),
        &response_size, &message, &message_available);
    if (response_size > 0 &&
        queue_raw_frame(client, response, response_size) != NATIVE_API_SEND_OK) {
        client->remove = true;
        return;
    }
    if (status != NATIVE_API_NOISE_OK) {
        ESP_LOGW(TAG, "Native Noise session rejected: status=%d", (int)status);
        if (response_size > 0) {
            client->close_after_write = true;
        } else {
            client->remove = true;
        }
        return;
    }
    if (message_available) {
        handle_frame(&message, client);
    }
}

static void reject_noise_client(native_api_client_t *client,
                                native_api_noise_status_t status)
{
    const char *reason = status == NATIVE_API_NOISE_INVALID_INDICATOR
                             ? "Bad indicator byte"
                             : "Bad handshake packet len";
    uint8_t response[32];
    size_t response_size = 0;
    if (native_api_noise_reject(&client->noise, reason, response,
                                sizeof(response), &response_size) ==
            NATIVE_API_NOISE_OK &&
        queue_raw_frame(client, response, response_size) == NATIVE_API_SEND_OK) {
        client->close_after_write = true;
    } else {
        client->remove = true;
    }
}

static void read_client(native_api_client_t *client)
{
    uint8_t input[NATIVE_API_RX_CHUNK_SIZE];
    ssize_t size = recv(client->fd, input, sizeof(input), 0);
    if (size == 0) {
        client->remove = true;
        return;
    }
    if (size < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            client->remove = true;
        }
        return;
    }
    if (client->noise_required) {
        bool was_ready = native_api_noise_is_ready(&client->noise);
        native_api_noise_status_t status = native_api_noise_feed(
            &client->noise, input, (size_t)size, handle_noise_frame, client);
        if (status != NATIVE_API_NOISE_OK && !client->close_after_write) {
            if (!was_ready && (status == NATIVE_API_NOISE_INVALID_INDICATOR ||
                               status == NATIVE_API_NOISE_FRAME_TOO_LARGE)) {
                reject_noise_client(client, status);
            } else {
                client->remove = true;
            }
        }
        return;
    }
    native_api_status_t status = native_api_parser_feed(
        &client->parser, input, (size_t)size, handle_frame, client);
    if (status != NATIVE_API_STATUS_OK) {
        ESP_LOGW(TAG, "Closing Native client after framing error: %d", (int)status);
        client->remove = true;
    }
}

static void update_keepalive(native_api_client_t *client, TickType_t now)
{
    if (client->fd < 0 || client->remove || client->close_after_write) {
        return;
    }
    if (!client->hello_received) {
        if (now - client->last_rx >= pdMS_TO_TICKS(NATIVE_API_HANDSHAKE_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "Native client Hello timeout");
            client->remove = true;
        }
        return;
    }
    if (client->ping_outstanding) {
        if (now - client->ping_sent >=
            pdMS_TO_TICKS(NATIVE_API_KEEPALIVE_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "Native client keepalive timeout");
            client->remove = true;
        }
        return;
    }
    if (now - client->last_rx >= pdMS_TO_TICKS(NATIVE_API_KEEPALIVE_IDLE_MS)) {
        if (queue_empty(client, NATIVE_API_MESSAGE_PING_REQUEST, true) ==
            NATIVE_API_SEND_OK) {
            client->ping_outstanding = true;
            client->ping_sent = now;
        }
    }
}

static void distribute_dirty(void)
{
    uint32_t dirty = take_dirty();
    if (dirty == 0) {
        return;
    }
    for (size_t i = 0; i < NATIVE_API_MAX_CLIENTS; ++i) {
        if (s_clients[i].fd >= 0 && s_clients[i].subscribed &&
            !s_clients[i].close_after_write) {
            s_clients[i].dirty |= dirty;
        }
    }
}

static void native_api_server_task(void *argument)
{
    (void)argument;
    while (true) {
        apply_control();
        TickType_t now = xTaskGetTickCount();
        if (!s_running && !s_psk_blocked &&
            (int32_t)(now - s_retry_at) >= 0) {
            retry_server();
        }
        if (!s_running) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NATIVE_API_STOPPED_WAIT_MS));
            continue;
        }

        distribute_dirty();
        for (size_t i = 0; i < NATIVE_API_MAX_CLIENTS; ++i) {
            flush_client(&s_clients[i]);
            produce_client(&s_clients[i]);
            update_keepalive(&s_clients[i], now);
            if (s_clients[i].remove) {
                close_client(&s_clients[i]);
            }
        }

        fd_set read_set;
        fd_set write_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_SET(s_listen_fd, &read_set);
        int max_fd = s_listen_fd;
        for (size_t i = 0; i < NATIVE_API_MAX_CLIENTS; ++i) {
            native_api_client_t *client = &s_clients[i];
            if (client->fd < 0) {
                continue;
            }
            if (!client->close_after_write && !client->remove) {
                FD_SET(client->fd, &read_set);
            }
            if (client->tx_count > 0 && !client->remove) {
                FD_SET(client->fd, &write_set);
            }
            if (client->fd > max_fd) {
                max_fd = client->fd;
            }
        }
        struct timeval timeout = {
            .tv_sec = 0,
            .tv_usec = NATIVE_API_SELECT_TIMEOUT_MS * 1000,
        };
        int ready = select(max_fd + 1, &read_set, &write_set, NULL, &timeout);
        if (ready < 0) {
            if (errno != EINTR) {
                ESP_LOGW(TAG, "Native select failed: errno=%d", errno);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }
        if (s_listen_fd >= 0 && FD_ISSET(s_listen_fd, &read_set)) {
            accept_clients();
        }
        for (size_t i = 0; i < NATIVE_API_MAX_CLIENTS; ++i) {
            native_api_client_t *client = &s_clients[i];
            if (client->fd < 0) {
                continue;
            }
            if (!client->close_after_write && FD_ISSET(client->fd, &read_set)) {
                read_client(client);
            }
            flush_client(client);
            if (client->remove) {
                close_client(client);
            }
        }
    }
}

static bool build_identity(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read base MAC: %s", esp_err_to_name(err));
        return false;
    }
    snprintf(s_identity.name, sizeof(s_identity.name),
             "sensecap-indicator-%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(s_identity.friendly_name, sizeof(s_identity.friendly_name),
             "SenseCAP Indicator %02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(s_identity.mac_address, sizeof(s_identity.mac_address),
             "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    snprintf(s_identity.mac_compact, sizeof(s_identity.mac_compact),
             "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(s_identity.version, sizeof(s_identity.version), "%s", app->version);
    snprintf(s_identity.compilation_time, sizeof(s_identity.compilation_time),
             "%.*s %.*s", 11, app->date, 8, app->time);
    return true;
}

esp_err_t native_api_server_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (!build_identity()) {
        return ESP_FAIL;
    }
    for (size_t i = 0; i < NATIVE_API_MAX_CLIENTS; ++i) {
        s_clients[i].fd = -1;
    }
    s_enabled = native_config_enabled_get();
    s_control_generation = 1;
    if (xTaskCreate(native_api_server_task, "native_api",
                    NATIVE_API_SERVER_STACK_SIZE, NULL,
                    NATIVE_API_SERVER_PRIORITY, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (!entity_core_add_observer(core_observer, NULL)) {
        vTaskDelete(s_task);
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    return ESP_OK;
}
