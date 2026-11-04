#ifndef MQTT_H
#define MQTT_H

#include "nvs.h"
#include "esp_event.h"
#include "view_data.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_wifi_types.h"
#include "mqtt_client.h"
#include <esp_mac.h>

#include <string.h>

#define MQTT_BROKER_STORAGE "MQTT"

ESP_EVENT_DECLARE_BASE(MQTT_APP_EVENT_BASE);
extern esp_event_loop_handle_t mqtt_app_event_handle;

#define MQTT_APP_EVENT_QUEUE_SIZE 16

enum MQTT_APP_EVENT {
    MQTT_APP_START,
    MQTT_APP_RESTART,
    MQTT_APP_STOP,
    MQTT_APP_CONTROL,
    MQTT_APP_PUBLISH,
    MQTT_APP_ALL,
};

typedef struct instance_mqtt instance_mqtt;
typedef instance_mqtt *instance_mqtt_t;

typedef struct {
    instance_mqtt_t instance;
    char topic[64];
    char data[256];
    size_t data_len;
} mqtt_data_t;

typedef struct {
    instance_mqtt_t instance;
    esp_mqtt_client_handle_t client;
    uint32_t generation;
} mqtt_client_event_t;

typedef struct {
    bool lifecycle_pending;
    enum MQTT_APP_EVENT lifecycle_event;
    bool connected_pending;
    mqtt_client_event_t connected;
} mqtt_control_mailbox_t;

typedef void (*MQTTStartFn)(instance_mqtt *instance);
typedef void (*MQTTStopFn)(instance_mqtt *instance);
typedef void (*MQTTConnectedFn)(instance_mqtt *instance);
struct instance_mqtt {
    char                     *mqtt_name;
    bool                      mqtt_connected_flag;
    esp_mqtt_client_handle_t  mqtt_client;
    esp_mqtt_client_config_t *mqtt_cfg;
    esp_event_handler_t       mqtt_event_handler;
    MQTTStartFn               mqtt_starter;
    MQTTStopFn                mqtt_stopper;
    MQTTConnectedFn           mqtt_connected_handler;
    bool                      is_using;
    uint32_t                  generation;
    mqtt_control_mailbox_t    control;
};

void log_error_if_nonzero(const char *message, int error_code);
void mqtt_instance_request(instance_mqtt *instance, enum MQTT_APP_EVENT event);
void mqtt_instance_connected(
    instance_mqtt *instance, esp_mqtt_client_handle_t client);
void mqtt_instance_stop(instance_mqtt *instance);
esp_err_t mqtt_publish_enqueue(
    instance_mqtt *instance, const char *topic, const char *data, size_t data_len);
int  indicator_mqtt_init(void);
bool get_mqtt_net_flag(void);

#endif /* MQTT_H */
