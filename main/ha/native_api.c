#include "native_api.h"

#include "native_api_server.h"
#include "native_config.h"
#include "view_event.h"

#include <string.h>

static bool s_initialized;

static void view_event_handler(
    void *handler_args, enum view_event_id id, const void *event_data)
{
    (void)handler_args;
    if (id != VIEW_EVENT_WIFI_ST || event_data == NULL) {
        return;
    }
    const struct view_data_wifi_st *wifi_status = event_data;
    native_api_server_set_has_ip(wifi_status->has_ip);
}

esp_err_t native_api_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t err = native_api_server_init();
    if (err != ESP_OK) {
        return err;
    }
    if (!view_event_subscribe(VIEW_EVENT_WIFI_ST, view_event_handler, NULL)) {
        return ESP_FAIL;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t native_api_set_enabled(bool enabled)
{
    esp_err_t err = native_config_enabled_set(enabled);
    if (err == ESP_OK) {
        native_api_server_set_enabled(enabled);
    }
    return err;
}

void native_api_configuration_changed(void)
{
    native_api_server_configuration_changed();
}

void native_api_get_status(native_api_runtime_status_t *status)
{
    if (status == NULL) {
        return;
    }
    uint8_t psk[NATIVE_CONFIG_PSK_SIZE];
    native_config_psk_state_t psk_state = native_config_psk_read(psk);
    memset(psk, 0, sizeof(psk));
    *status = (native_api_runtime_status_t){
        .enabled = native_config_enabled_get(),
        .psk_configured = psk_state != NATIVE_CONFIG_PSK_NOT_CONFIGURED,
    };
    native_api_server_get_status(&status->running, &status->has_ip,
                                 &status->client_count);
}
