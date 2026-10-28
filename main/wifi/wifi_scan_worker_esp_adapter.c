#include "wifi_scan_worker_adapter.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stddef.h>
#include <string.h>

#define WIFI_SCAN_WORKER_QUEUE_LENGTH 1
#define WIFI_SCAN_WORKER_STACK_SIZE (5 * 1024)
#define WIFI_SCAN_WORKER_PRIORITY 10
#define WIFI_SCAN_RESULT_RETRY_DELAY_MS 10

static QueueHandle_t s_request_queue;
static wifi_scan_worker_execute_t s_execute;

static void wifi_scan_worker_task(void *context)
{
    (void)context;
    struct view_data_wifi_st state;
    while (true) {
        if (xQueueReceive(s_request_queue, &state, portMAX_DELAY) == pdTRUE) {
            s_execute(&state);
        }
    }
}

bool wifi_scan_worker_adapter_init(wifi_scan_worker_execute_t execute)
{
    if (execute == NULL) {
        return false;
    }
    s_execute = execute;
    s_request_queue = xQueueCreate(
        WIFI_SCAN_WORKER_QUEUE_LENGTH, sizeof(struct view_data_wifi_st));
    if (s_request_queue == NULL) {
        ESP_LOGE("wifi-scan", "failed to create request queue");
        return false;
    }
    if (xTaskCreate(
            wifi_scan_worker_task,
            "wifi_scan_worker",
            WIFI_SCAN_WORKER_STACK_SIZE,
            NULL,
            WIFI_SCAN_WORKER_PRIORITY,
            NULL) != pdPASS) {
        ESP_LOGE("wifi-scan", "failed to create worker task");
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return false;
    }
    return true;
}

bool wifi_scan_worker_adapter_submit(const struct view_data_wifi_st *state)
{
    return s_request_queue != NULL &&
        xQueueOverwrite(s_request_queue, state) == pdTRUE;
}

bool wifi_scan_worker_adapter_scan(
    struct wifi_scan_record *records, size_t capacity, size_t *count)
{
    if (records == NULL || count == NULL || capacity == 0) {
        return false;
    }

    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGE("wifi-scan", "scan start failed: %s", esp_err_to_name(err));
        return false;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGE("wifi-scan", "scan count failed: %s", esp_err_to_name(err));
        return false;
    }

    uint16_t requested = (uint16_t)capacity;
    wifi_ap_record_t ap_info[WIFI_SCAN_LIST_SIZE] = {0};
    err = esp_wifi_scan_get_ap_records(&requested, ap_info);
    if (err != ESP_OK) {
        ESP_LOGE("wifi-scan", "scan records failed: %s", esp_err_to_name(err));
        return false;
    }

    size_t available = requested < ap_count ? requested : ap_count;
    if (available > capacity) {
        available = capacity;
    }
    for (size_t i = 0; i < available; i++) {
        memcpy(records[i].ssid, ap_info[i].ssid, sizeof(records[i].ssid));
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        records[i].auth_mode = ap_info[i].authmode != WIFI_AUTH_OPEN;
        records[i].rssi = ap_info[i].rssi;
    }
    *count = available;
    ESP_LOGI(
        "wifi-scan",
        "Total APs scanned = %u, records returned = %u",
        (unsigned)ap_count,
        (unsigned)available);
    return true;
}

void wifi_scan_worker_adapter_retry_delay(void)
{
    vTaskDelay(pdMS_TO_TICKS(WIFI_SCAN_RESULT_RETRY_DELAY_MS));
}
