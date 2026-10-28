#!/usr/bin/env python3
"""Host regression tests for the Wi-Fi scan worker module interface."""

from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "view_event.h"
#include "wifi_scan_worker.h"
#include "wifi_scan_worker_adapter.h"

static wifi_scan_worker_execute_t execute_scan;
static struct wifi_scan_work pending;
static bool has_pending;
static bool adapter_busy;
static bool scan_success;
static unsigned scan_calls;
static unsigned result_posts;
static unsigned failure_posts;
static unsigned retry_delays;
static view_event_delivery_t result_deliveries[8];
static unsigned result_delivery_count;
static struct view_data_wifi_list last_list;

bool wifi_scan_worker_adapter_init(wifi_scan_worker_execute_t execute)
{
    execute_scan = execute;
    has_pending = false;
    adapter_busy = false;
    scan_success = true;
    scan_calls = 0;
    result_posts = 0;
    failure_posts = 0;
    retry_delays = 0;
    result_delivery_count = 0;
    return true;
}

bool wifi_scan_worker_adapter_submit(const struct wifi_scan_work *work)
{
    if (adapter_busy) {
        return false;
    }
    pending = *work;
    has_pending = true; /* queue length one: latest request wins */
    return true;
}

bool wifi_scan_worker_adapter_scan(
    struct wifi_scan_record *records, size_t capacity, size_t *count)
{
    assert(capacity >= 3);
    scan_calls++;
    if (!scan_success) {
        return false;
    }
    records[0] = (struct wifi_scan_record){
        .ssid = "alpha", .auth_mode = true, .rssi = -40};
    records[1] = (struct wifi_scan_record){
        .ssid = "alpha", .auth_mode = true, .rssi = -60};
    records[2] = (struct wifi_scan_record){
        .ssid = "beta", .auth_mode = false, .rssi = -50};
    *count = 3;
    return true;
}

void wifi_scan_worker_adapter_retry_delay(void)
{
    retry_delays++;
}

static void drain(void)
{
    assert(has_pending);
    struct wifi_scan_work work = pending;
    has_pending = false;
    execute_scan(&work);
}

view_event_delivery_t view_event_post_command(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    if (id == VIEW_EVENT_WIFI_LIST_FAILED) {
        assert(payload_size == sizeof(struct view_data_wifi_scan_failure));
        const struct view_data_wifi_scan_failure *failure = payload;
        assert(failure->scan_generation != 0);
        failure_posts++;
        return VIEW_EVENT_DELIVERY_ENQUEUED;
    }
    assert(id == VIEW_EVENT_WIFI_LIST);
    assert(payload_size == sizeof(last_list));
    last_list = *(const struct view_data_wifi_list *)payload;
    unsigned call = result_posts++;
    if (call < result_delivery_count) {
        return result_deliveries[call];
    }
    return VIEW_EVENT_DELIVERY_ENQUEUED;
}

static double monotonic_seconds(void)
{
    struct timespec now;
    assert(timespec_get(&now, TIME_UTC) == TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

int main(void)
{
    assert(wifi_scan_worker_init());

    struct view_data_wifi_st first = {
        .is_connected = true, .rssi = -30, .ssid = "connected-one"};
    struct view_data_wifi_st latest = {
        .is_connected = true, .rssi = -20, .ssid = "connected-latest"};

    assert(wifi_scan_worker_request(1, &first));
    assert(scan_calls == 0);
    assert(result_posts == 0);
    assert(wifi_scan_worker_request(2, &latest));
    assert(scan_calls == 0);
    drain();

    assert(scan_calls == 1);
    assert(result_posts == 1);
    assert(last_list.scan_generation == 2);
    assert(last_list.is_connect);
    assert(strcmp(last_list.connect.ssid, "connected-latest") == 0);
    assert(last_list.cnt == 2);
    assert(strcmp(last_list.aps[0].ssid, "alpha") == 0);
    assert(strcmp(last_list.aps[1].ssid, "beta") == 0);

    adapter_busy = true;
    double started = monotonic_seconds();
    assert(!wifi_scan_worker_request(3, &first));
    assert(monotonic_seconds() - started < 0.100);
    assert(scan_calls == 1);
    assert(result_posts == 1);

    adapter_busy = false;
    result_deliveries[0] = VIEW_EVENT_DELIVERY_REJECTED;
    result_deliveries[1] = VIEW_EVENT_DELIVERY_REJECTED;
    result_deliveries[2] = VIEW_EVENT_DELIVERY_ENQUEUED;
    result_delivery_count = 3;
    result_posts = 0;
    retry_delays = 0;
    assert(wifi_scan_worker_request(4, &first));
    drain();
    assert(result_posts == 3);
    assert(retry_delays == 2);

    result_deliveries[2] = VIEW_EVENT_DELIVERY_REJECTED;
    result_posts = 0;
    failure_posts = 0;
    retry_delays = 0;
    assert(wifi_scan_worker_request(5, &first));
    drain();
    assert(result_posts == 3);
    assert(failure_posts == 1);
    assert(retry_delays == 2);

    scan_success = false;
    result_posts = 0;
    failure_posts = 0;
    retry_delays = 0;
    assert(wifi_scan_worker_request(6, &first));
    drain();
    assert(result_posts == 0);
    assert(failure_posts == 1);

    return 0;
}
"""


class WifiScanWorkerTests(unittest.TestCase):
    def test_requests_are_nonblocking_coalesced_and_run_in_worker(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "wifi_scan_worker_test"
            harness = Path(tmp) / "wifi_scan_worker_test.c"
            harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "main"),
                    "-I",
                    str(ROOT / "main" / "view_event"),
                    "-I",
                    str(ROOT / "main" / "wifi"),
                    str(harness),
                    str(ROOT / "main" / "wifi" / "wifi_scan_worker.c"),
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=2)

    def test_view_handler_does_not_scan_synchronously(self) -> None:
        source = (ROOT / "main" / "wifi" / "wifi_model.c").read_text(
            encoding="utf-8"
        )
        handler = source.split("static void _view_event_handler", 1)[1]
        handler = handler.split("int indicator_wifi_model_init", 1)[0]
        self.assertIn("wifi_scan_worker_request", handler)
        self.assertNotIn("esp_wifi_scan_start", handler)
        list_request = handler.split(
            "case VIEW_EVENT_WIFI_LIST_REQ:", 1
        )[1].split("case VIEW_EVENT_WIFI_CONNECT:", 1)[0]
        self.assertIn("_wifi_st_try_get", list_request)
        self.assertNotIn("_wifi_st_get(", list_request)
        snapshot = source.split("static bool _wifi_st_try_get", 1)[1]
        snapshot = snapshot.split("\n}", 1)[0]
        self.assertNotIn("portMAX_DELAY", snapshot)

    def test_view_scan_timeout_always_clears_pending_spinner(self) -> None:
        source = (ROOT / "main" / "wifi" / "wifi_view.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("static void _wifi_scan_timeout_cb", source)
        timeout = source.split("static void _wifi_scan_timeout_cb", 1)[1]
        timeout = timeout.split("static void", 1)[0]
        self.assertIn("wifi_scan_ui_timeout", timeout)
        self.assertIn(
            "lv_obj_add_flag(s_wifi_spinner, LV_OBJ_FLAG_HIDDEN);", timeout
        )
        timeout_start = source.split("static bool _wifi_scan_timeout_start", 1)[1]
        timeout_start = timeout_start.split("static void", 1)[0]
        self.assertIn(
            "s_wifi_scan_timeout_generation = scan_generation;", timeout_start
        )
        self.assertIn("_wifi_scan_timeout_start(scan_generation)", source)
        wifi_list = source.split("case VIEW_EVENT_WIFI_LIST:", 1)[1]
        wifi_list = wifi_list.split("case VIEW_EVENT_WIFI_CONNECT_RET:", 1)[0]
        self.assertIn("wifi_scan_ui_complete", wifi_list)
        self.assertIn("WIFI_SCAN_UI_COMPLETION_STALE", wifi_list)
        self.assertIn("_wifi_scan_timeout_cancel();", wifi_list)
        failed = source.split("case VIEW_EVENT_WIFI_LIST_FAILED:", 1)[1]
        failed = failed.split("case VIEW_EVENT_WIFI_CONNECT_RET:", 1)[0]
        self.assertIn("wifi_scan_ui_complete", failed)
        self.assertIn("WIFI_SCAN_UI_COMPLETION_STALE", failed)
        self.assertIn("_wifi_scan_timeout_cancel();", failed)
        self.assertIn(
            "lv_obj_add_flag(s_wifi_spinner, LV_OBJ_FLAG_HIDDEN);", failed
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
