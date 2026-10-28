#include "wifi_scan_worker.h"

#include "view_event.h"
#include "wifi_scan_worker_adapter.h"

#include <stddef.h>
#include <string.h>

#define WIFI_SCAN_RESULT_DELIVERY_ATTEMPTS 3

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define WIFI_SCAN_LOG_ERROR(...) ESP_LOGE("wifi-scan", __VA_ARGS__)
#else
#define WIFI_SCAN_LOG_ERROR(...) ((void)0)
#endif

static void wifi_scan_copy_ssid(char destination[32], const char source[32])
{
    memcpy(destination, source, 32);
    destination[31] = '\0';
}

static bool wifi_scan_list_contains(
    const struct view_data_wifi_list *list, const char *ssid)
{
    for (uint16_t i = 0; i < list->cnt; i++) {
        if (strncmp(list->aps[i].ssid, ssid, sizeof(list->aps[i].ssid)) == 0) {
            return true;
        }
    }
    return false;
}

static bool wifi_scan_worker_post(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    view_event_delivery_t delivery = VIEW_EVENT_DELIVERY_REJECTED;
    for (unsigned attempt = 0;
         attempt < WIFI_SCAN_RESULT_DELIVERY_ATTEMPTS;
         attempt++) {
        delivery = view_event_post_command(id, payload, payload_size);
        if (delivery == VIEW_EVENT_DELIVERY_ENQUEUED) {
            return true;
        }
        if (attempt + 1 < WIFI_SCAN_RESULT_DELIVERY_ATTEMPTS) {
            wifi_scan_worker_adapter_retry_delay();
        }
    }
    WIFI_SCAN_LOG_ERROR(
        "scan completion %d rejected after %u worker attempts: delivery=%d",
        (int)id,
        WIFI_SCAN_RESULT_DELIVERY_ATTEMPTS,
        (int)delivery);
    return false;
}

static void wifi_scan_worker_post_failure(void)
{
    (void)wifi_scan_worker_post(VIEW_EVENT_WIFI_LIST_FAILED, NULL, 0);
}

static void wifi_scan_worker_execute(
    const struct view_data_wifi_st *state)
{
    struct wifi_scan_record records[WIFI_SCAN_LIST_SIZE] = {0};
    size_t count = 0;
    if (!wifi_scan_worker_adapter_scan(
            records, WIFI_SCAN_LIST_SIZE, &count)) {
        WIFI_SCAN_LOG_ERROR("scan adapter failed");
        wifi_scan_worker_post_failure();
        return;
    }
    if (count > WIFI_SCAN_LIST_SIZE) {
        count = WIFI_SCAN_LIST_SIZE;
    }

    struct view_data_wifi_list list = {0};
    list.is_connect = state->is_connected;
    if (state->is_connected) {
        wifi_scan_copy_ssid(list.connect.ssid, state->ssid);
        list.connect.auth_mode = false;
        list.connect.rssi = state->rssi;
    }

    for (size_t i = 0; i < count && list.cnt < WIFI_SCAN_LIST_SIZE; i++) {
        if (wifi_scan_list_contains(&list, records[i].ssid)) {
            continue;
        }
        struct view_data_wifi_item *item = &list.aps[list.cnt++];
        wifi_scan_copy_ssid(item->ssid, records[i].ssid);
        item->auth_mode = records[i].auth_mode;
        item->rssi = records[i].rssi;
    }

    if (!wifi_scan_worker_post(VIEW_EVENT_WIFI_LIST, &list, sizeof(list))) {
        wifi_scan_worker_post_failure();
    }
}

bool wifi_scan_worker_init(void)
{
    return wifi_scan_worker_adapter_init(wifi_scan_worker_execute);
}

bool wifi_scan_worker_request(const struct view_data_wifi_st *state)
{
    return state != NULL && wifi_scan_worker_adapter_submit(state);
}
