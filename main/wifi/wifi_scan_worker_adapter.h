#ifndef WIFI_SCAN_WORKER_ADAPTER_H
#define WIFI_SCAN_WORKER_ADAPTER_H

#include "view_data_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wifi_scan_record {
    char ssid[32];
    bool auth_mode;
    int8_t rssi;
};

typedef void (*wifi_scan_worker_execute_t)(
    const struct view_data_wifi_st *state);

bool wifi_scan_worker_adapter_init(wifi_scan_worker_execute_t execute);
bool wifi_scan_worker_adapter_submit(const struct view_data_wifi_st *state);
bool wifi_scan_worker_adapter_scan(
    struct wifi_scan_record *records, size_t capacity, size_t *count);

#endif /* WIFI_SCAN_WORKER_ADAPTER_H */
