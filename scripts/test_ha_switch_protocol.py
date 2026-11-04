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
    def setUp(self):
        self.controller = (
            ROOT / "main" / "ha" / "ha_switch.c"
        ).read_text(encoding="utf-8")
        self.core = (
            ROOT / "main" / "ha" / "entity_core.c"
        ).read_text(encoding="utf-8")
        self.registry = (
            ROOT / "main" / "ha" / "entity_registry.c"
        ).read_text(encoding="utf-8")

    def test_controller_and_screen_cold_start_at_zero(self):
        config = (
            ROOT / "main" / "home_assistant_config.h"
        ).read_text(encoding="utf-8")
        screen = (
            ROOT / "main" / "ha" / "ha_switch_screen.c"
        ).read_text(encoding="utf-8")

        defaults = config.split(
            "#define CONFIG_SWITCH_DEFAULT_VALUES", 1
        )[1].split("}", 1)[0]
        self.assertEqual(
            [
                int(value.replace("\\", "").strip())
                for value in defaults.split("{", 1)[1].split(",")
            ],
            [0] * 8,
        )
        self.assertNotIn("static int switch_state", self.controller)
        self.assertEqual(self.registry.count(".default_value = 0.0f"), 8)
        self.assertIn(
            "static const int "
            "switch_defaults[CONFIG_HA_SWITCH_ENTITY_NUM] =\n"
            "    CONFIG_SWITCH_DEFAULT_VALUES;",
            screen,
        )
        self.assertIn(
            "ha_switch_sync_init(\n"
            "        &slot->sync, switch_defaults[slot->index]);",
            screen,
        )

    def test_core_snapshots_seed_ui_and_reconnect_without_nvs(self):
        init = self.controller.split(
            "void ha_switch_init(void)", 1
        )[1].split("\n}", 1)[0]
        subscribe = self.controller.split(
            "void ha_switch_subscribe", 1
        )[1].split("\n}", 1)[0]

        self.assertIn("entity_core_add_observer", init)
        self.assertIn("publish_current_switch_states();", init)
        self.assertIn("publish_current_switch_states();", subscribe)
        self.assertIn(
            "entity_core_replay(core_state_observer, NULL)", self.controller
        )
        self.assertNotIn("entity_core_get_snapshot", self.controller)
        self.assertNotIn("core_state_observer(&snapshot", self.controller)
        for source in (self.controller, self.core, self.registry):
            self.assertNotIn("indicator_nvs_read", source)
            self.assertNotIn("indicator_nvs_write", source)
            self.assertNotIn("ha_ctrl_cfg_restore", source)
            self.assertNotIn("ha_ctrl_cfg_save", source)
            self.assertNotIn("HA_CFG_STORAGE", source)
        self.assertNotIn("restore_data_task", subscribe)
        self.assertNotIn("xTaskCreate", subscribe)

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
        self.assertIn("VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT", data_types)
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
        self.assertNotIn("view_event", self.core)

    def test_switch_adapter_is_ui_free_and_core_acceptance_is_authoritative(self):
        header = (
            ROOT / "main" / "ha" / "ha_switch.h"
        ).read_text(encoding="utf-8")
        local_command = self.controller.split(
            "static void publish_switch_command", 1
        )[1].split("\n}", 1)[0]

        self.assertNotIn('#include "lv_port.h"', self.controller)
        self.assertNotIn('#include "ha_switch_screen.h"', header)
        self.assertNotIn("ha_switch_attach_screen", self.controller)
        self.assertNotIn("ha_switch_attach_screen", header)
        self.assertIn("mqtt_publish_enqueue", self.controller)
        self.assertNotIn("esp_mqtt_client_publish", self.controller)
        self.assertNotIn("portMAX_DELAY", self.controller)
        self.assertIn("VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT", self.controller)
        self.assertIn("VIEW_EVENT_HA_SWITCH_SET", self.controller)
        self.assertIn("entity_core_submit_command", local_command)
        self.assertIn("core_result.accepted", local_command)
        self.assertNotIn("mqtt_connected_flag", local_command)
        self.assertNotIn("esp_mqtt_client_publish", local_command)
        self.assertNotIn("accepted = message_id", self.controller)

    def test_remote_commands_enter_core_and_json_keys_match_exactly(self):
        remote = self.controller.split(
            "int ha_switch_on_mqtt_data", 1
        )[1].split("\n}", 1)[0]

        self.assertIn("ENTITY_CORE_SOURCE_MQTT", remote)
        self.assertIn("entity_core_submit_command", remote)
        self.assertIn("strcmp(c_key->string, ha_switch_entities[i].key)", remote)
        self.assertIn("cJSON_GetObjectItemCaseSensitive", remote)
        self.assertNotIn("switch_state", remote)
        self.assertIsNone(
            parse_switch_message(SWITCH_SET_TOPIC, '{"switch10": 1}')
        )

    def test_core_ha_and_mqtt_lifecycle_initialize_before_wifi(self):
        model = (
            ROOT / "main" / "indicator_model.c"
        ).read_text(encoding="utf-8")
        core_init = model.index("entity_core_init()")
        ha_init = model.index("indicator_ha_model_init()")
        wifi_init = model.index("indicator_wifi_model_init()")
        mqtt_init = model.index("indicator_mqtt_init()")
        self.assertLess(core_init, ha_init)
        self.assertLess(ha_init, mqtt_init)
        self.assertLess(mqtt_init, wifi_init)

    def test_local_sensor_snapshots_enter_core_before_mqtt_publish(self):
        sensor = (
            ROOT / "main" / "ha" / "ha_sensor.c"
        ).read_text(encoding="utf-8")
        handler = sensor.split("static void view_event_handler", 1)[1].split(
            "\n}", 1
        )[0]
        self.assertLess(
            handler.index("entity_core_set_sensor"),
            handler.index("publish_sensor_data"),
        )
        self.assertIn("entity_core_get_snapshot", handler)
        self.assertIn("mqtt_publish_enqueue", sensor)
        self.assertNotIn("esp_mqtt_client_publish", sensor)
        self.assertNotIn("portMAX_DELAY", sensor)
        self.assertIn("size_t key_len = strlen(c_key->string);", sensor)
        self.assertIn(
            "strncmp(c_key->string, ha_sensor_entities[i].key, key_len)",
            sensor,
        )
        self.assertIn("cJSON_GetObjectItem(root, c_key->string)", sensor)


if __name__ == "__main__":
    unittest.main(verbosity=2)
