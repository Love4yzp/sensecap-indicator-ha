#ifndef WIFI_SCAN_WORKER_H
#define WIFI_SCAN_WORKER_H

#include "view_data_types.h"

#include <stdbool.h>

/*
 * Owns asynchronous Wi-Fi scan execution and result publication. Requests are
 * nonblocking and coalesce to the latest connected-state snapshot.
 */
bool wifi_scan_worker_init(void);
bool wifi_scan_worker_request(const struct view_data_wifi_st *state);

#endif /* WIFI_SCAN_WORKER_H */
