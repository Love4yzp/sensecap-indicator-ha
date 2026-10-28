#!/usr/bin/env python3
"""Host-side tests for the Home Assistant switch MQTT protocol."""

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SWITCH_SET_TOPIC = "indicator/switch/set"
SWITCH_STATE_TOPIC = "indicator/switch/state"
SWITCH_KEYS = [f"switch{i}" for i in range(1, 9)]
SWITCH_COUNT = 8


def parse_switch_message(topic: str, payload: str) -> dict | None:
    """Parse an incoming Home Assistant switch command message."""
    if topic != SWITCH_SET_TOPIC:
        return None

    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        return None

    if not isinstance(data, dict):
        return None

    for index, key in enumerate(SWITCH_KEYS):
        if key not in data:
            continue

        value = data[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return None

        return {"index": index, "value": int(value)}

    return None


def format_switch_publish(index: int, value: int) -> tuple[str, str] | None:
    """Format an outgoing Home Assistant switch state message."""
    if index < 0 or index >= SWITCH_COUNT:
        return None

    payload = json.dumps({SWITCH_KEYS[index]: value})
    return SWITCH_STATE_TOPIC, payload


class TestSwitchIncoming(unittest.TestCase):
    def test_valid_switch_on(self):
        self.assertEqual(
            parse_switch_message(SWITCH_SET_TOPIC, '{"switch1": 1}'),
            {"index": 0, "value": 1},
        )

    def test_valid_switch_off(self):
        self.assertEqual(
            parse_switch_message(SWITCH_SET_TOPIC, '{"switch2": 0}'),
            {"index": 1, "value": 0},
        )

    def test_all_switch_indices(self):
        for i in range(SWITCH_COUNT):
            key = f"switch{i + 1}"
            with self.subTest(key=key):
                result = parse_switch_message(SWITCH_SET_TOPIC, json.dumps({key: 1}))
                self.assertIsNotNone(result)
                self.assertEqual(result["index"], i)

    def test_wrong_topic(self):
        self.assertIsNone(parse_switch_message("other/topic", '{"switch1": 1}'))

    def test_invalid_json(self):
        self.assertIsNone(parse_switch_message(SWITCH_SET_TOPIC, "not_json"))

    def test_missing_key(self):
        self.assertIsNone(parse_switch_message(SWITCH_SET_TOPIC, '{"unknown_key": 1}'))

    def test_non_number_value(self):
        self.assertIsNone(parse_switch_message(SWITCH_SET_TOPIC, '{"switch1": "on"}'))


class TestSwitchPublish(unittest.TestCase):
    def test_publish_format_on(self):
        self.assertEqual(
            format_switch_publish(0, 1),
            (SWITCH_STATE_TOPIC, '{"switch1": 1}'),
        )

    def test_publish_format_off(self):
        self.assertEqual(
            format_switch_publish(3, 0),
            (SWITCH_STATE_TOPIC, '{"switch4": 0}'),
        )

    def test_invalid_index_negative(self):
        self.assertIsNone(format_switch_publish(-1, 1))

    def test_invalid_index_overflow(self):
        self.assertIsNone(format_switch_publish(8, 1))


class TestSwitchScreenDeliveryState(unittest.TestCase):
    def setUp(self):
        self.source = (
            ROOT / "main" / "ha" / "ha_switch_screen.c"
        ).read_text(encoding="utf-8")
        self.header = (
            ROOT / "main" / "ha" / "ha_switch_screen.h"
        ).read_text(encoding="utf-8")

    def test_screen_owns_model_and_publish_result_subscriptions(self):
        self.assertNotIn("ha_switch_screen_update", self.header)
        self.assertIn("VIEW_EVENT_HA_SWITCH_SET", self.source)
        self.assertIn(
            "VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT", self.source
        )
        self.assertGreaterEqual(
            self.source.count("view_event_subscribe("), 2
        )
        handler = self.source.split(
            "static void _view_event_handler", 1
        )[1].split("\n}", 1)[0]
        self.assertIn("lv_port_sem_take();", handler)
        self.assertIn("lv_port_sem_give();", handler)

    def test_continuous_controls_are_debounced_but_toggles_are_immediate(self):
        self.assertIn("#define HA_SWITCH_DEBOUNCE_MS 200", self.source)
        toggle = self.source.split(
            "static void _switch_toggle_event_cb", 1
        )[1].split("\n}", 1)[0]
        arc = self.source.split(
            "static void _arc_event_cb", 1
        )[1].split("\n}", 1)[0]
        slider = self.source.split(
            "static void _slider_event_cb", 1
        )[1].split("\n}", 1)[0]
        self.assertIn("HA_SWITCH_SYNC_IMMEDIATE", toggle)
        self.assertIn("HA_SWITCH_SYNC_DEBOUNCED", arc)
        self.assertIn("HA_SWITCH_SYNC_DEBOUNCED", slider)
        self.assertIn("lv_timer_reset", self.source)
        self.assertIn("ha_switch_sync_debounce_elapsed", self.source)

    def test_model_updates_and_results_use_generation_state(self):
        self.assertIn("ha_switch_sync_t sync;", self.source)
        self.assertNotIn("int accepted_value;", self.source)
        self.assertIn("bool suppress_post;", self.source)
        self.assertIn("ha_switch_sync_model_changed", self.source)
        self.assertIn("ha_switch_sync_publish_result", self.source)
        self.assertIn("#define HA_SWITCH_RESULT_TIMEOUT_MS 2000", self.source)
        self.assertIn("_cancel_debounce_timer(slot);", self.source)
        self.assertIn("_cancel_result_timer(slot);", self.source)


class TestSwitchEventContract(unittest.TestCase):
    def test_user_command_model_state_and_publish_result_are_distinct(self):
        data_types = (
            ROOT / "main" / "view_data_types.h"
        ).read_text(encoding="utf-8")
        view_event = (
            ROOT / "main" / "view_event" / "view_event.c"
        ).read_text(encoding="utf-8")

        self.assertIn("struct view_data_ha_switch_command", data_types)
        self.assertIn(
            "struct view_data_ha_switch_publish_result", data_types
        )
        self.assertIn(
            "VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT", data_types
        )
        self.assertIn(
            "[VIEW_EVENT_HA_SWITCH_ST] =\n"
            "        COMMAND(struct view_data_ha_switch_command)",
            view_event,
        )
        self.assertIn(
            "[VIEW_EVENT_HA_SWITCH_SET] = STATE_KEYED(\n"
            "        struct view_data_ha_switch_data, "
            "VIEW_EVENT_KEY_HA_SWITCH_INDEX)",
            view_event,
        )
        self.assertIn(
            "[VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT] =\n"
            "        COMMAND(struct view_data_ha_switch_publish_result)",
            view_event,
        )

    def test_switch_controller_is_ui_free_and_reports_publish_results(self):
        controller = (
            ROOT / "main" / "ha" / "ha_switch.c"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "main" / "ha" / "ha_switch.h"
        ).read_text(encoding="utf-8")

        self.assertNotIn('#include "lv_port.h"', controller)
        self.assertNotIn('#include "ha_switch_screen.h"', header)
        self.assertNotIn("ha_switch_attach_screen", controller)
        self.assertNotIn("ha_switch_attach_screen", header)
        self.assertIn("esp_mqtt_client_publish", controller)
        self.assertIn("message_id >= 0", controller)
        self.assertIn(
            "VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT", controller
        )
        self.assertIn(
            "view_event_publish_state(\n"
            "                    VIEW_EVENT_HA_SWITCH_SET",
            controller,
        )
        self.assertIn(
            "switch_state[i] = switch_data.value;", controller
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
