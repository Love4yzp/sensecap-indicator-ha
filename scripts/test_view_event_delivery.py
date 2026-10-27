#!/usr/bin/env python3
"""Host regression tests for the public view-event delivery interface."""

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
#include <stdint.h>
#include <time.h>

#include "view_event.h"
#include "view_event_adapter.h"

static view_event_adapter_result_t command_result = VIEW_EVENT_ADAPTER_OK;
static view_event_adapter_result_t state_result = VIEW_EVENT_ADAPTER_OK;
static unsigned command_calls;
static unsigned pending_count;
static size_t pending_slots[32];

bool view_event_adapter_init(void)
{
    command_calls = 0;
    pending_count = 0;
    return true;
}

view_event_adapter_result_t view_event_adapter_post_command(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    (void)id;
    (void)payload;
    (void)payload_size;
    command_calls++;
    return command_result;
}

view_event_adapter_result_t view_event_adapter_post_state(size_t slot)
{
    if (state_result == VIEW_EVENT_ADAPTER_OK) {
        assert(pending_count < 32);
        pending_slots[pending_count++] = slot;
    }
    return state_result;
}

void view_event_adapter_lock(void) {}
void view_event_adapter_unlock(void) {}

static void drain_all(void)
{
    while (pending_count > 0) {
        size_t slot = pending_slots[0];
        for (unsigned i = 1; i < pending_count; i++) {
            pending_slots[i - 1] = pending_slots[i];
        }
        pending_count--;
        view_event_internal_dispatch_state(slot);
    }
}

static unsigned sensor_dispatches;
static struct view_data_sensor_data last_sensor;

static void sensor_handler(void *context, enum view_event_id id, const void *payload)
{
    (void)context;
    assert(id == VIEW_EVENT_SENSOR_DATA);
    last_sensor = *(const struct view_data_sensor_data *)payload;
    sensor_dispatches++;
}

static double monotonic_seconds(void)
{
    struct timespec now;
    assert(timespec_get(&now, TIME_UTC) == TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static view_event_delivery_t simulated_lvgl_callback(void)
{
    struct view_data_ha_switch_data switch_data = {.index = 0, .value = 1};
    return view_event_post_command(
        VIEW_EVENT_HA_SWITCH_ST, &switch_data, sizeof(switch_data));
}

int main(void)
{
    assert(view_event_init());
    assert(view_event_subscribe(VIEW_EVENT_SENSOR_DATA, sensor_handler, NULL));

    command_result = VIEW_EVENT_ADAPTER_FULL;
    double started = monotonic_seconds();
    view_event_delivery_t result = simulated_lvgl_callback();
    double elapsed = monotonic_seconds() - started;

    assert(result == VIEW_EVENT_DELIVERY_REJECTED);
    assert(command_calls == 1);
    assert(elapsed < 0.100);

    struct view_event_stats stats;
    view_event_stats_get(&stats);
    assert(stats.command_rejected == 1);

    command_result = VIEW_EVENT_ADAPTER_OK;
    assert(view_event_publish_state(
               VIEW_EVENT_WIFI_ST, NULL, 0) == VIEW_EVENT_DELIVERY_INVALID);
    assert(view_event_publish_state(
               VIEW_EVENT_SHUTDOWN, NULL, 0) == VIEW_EVENT_DELIVERY_INVALID);

    struct view_data_sensor_data co2 = {
        .sensor_type = SCD41_SENSOR_CO2,
        .value = 800.0f,
    };
    assert(view_event_publish_state(
               VIEW_EVENT_SENSOR_DATA, &co2, sizeof(co2)) ==
           VIEW_EVENT_DELIVERY_ENQUEUED);

    co2.value = 900.0f;
    assert(view_event_publish_state(
               VIEW_EVENT_SENSOR_DATA, &co2, sizeof(co2)) ==
           VIEW_EVENT_DELIVERY_COALESCED);

    struct view_data_sensor_data tvoc = {
        .sensor_type = SGP40_SENSOR_TVOC,
        .value = 42.0f,
    };
    assert(view_event_publish_state(
               VIEW_EVENT_SENSOR_DATA, &tvoc, sizeof(tvoc)) ==
           VIEW_EVENT_DELIVERY_ENQUEUED);
    assert(pending_count == 2);

    drain_all();
    assert(sensor_dispatches == 2);
    assert(last_sensor.sensor_type == SGP40_SENSOR_TVOC);

    view_event_stats_get(&stats);
    assert(stats.state_enqueued == 2);
    assert(stats.state_coalesced == 1);
    assert(stats.state_dispatched == 2);
    assert(stats.invalid_publications == 2);

    struct view_data_sensor_history_data history = {0};
    history.sensor_type = SCD41_SENSOR_CO2;
    assert(view_event_publish_state(
               VIEW_EVENT_SENSOR_DATA_HISTORY, &history, sizeof(history)) ==
           VIEW_EVENT_DELIVERY_ENQUEUED);
    drain_all();

    state_result = VIEW_EVENT_ADAPTER_FULL;
    assert(view_event_publish_state(
               VIEW_EVENT_SENSOR_DATA, &co2, sizeof(co2)) ==
           VIEW_EVENT_DELIVERY_DROPPED);
    view_event_stats_get(&stats);
    assert(stats.state_dropped == 1);

    return 0;
}
"""


class ViewEventDeliveryTests(unittest.TestCase):
    def test_public_interface_handles_queue_pressure_without_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "view_event_test"
            harness = Path(tmp) / "view_event_test.c"
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
                    str(harness),
                    str(ROOT / "main" / "view_event" / "view_event.c"),
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
