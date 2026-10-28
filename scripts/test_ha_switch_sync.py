#!/usr/bin/env python3
"""Host-side behavior tests for Home Assistant switch synchronization."""

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class HaSwitchSyncTests(unittest.TestCase):
    def compile_and_run(self, harness: str) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            harness_path = tmp / "ha_switch_sync_harness.c"
            binary_path = tmp / "ha_switch_sync_harness"
            harness_path.write_text(textwrap.dedent(harness), encoding="utf-8")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "main" / "ha"),
                    str(ROOT / "main" / "ha" / "ha_switch_sync.c"),
                    str(harness_path),
                    "-o",
                    str(binary_path),
                ],
                check=True,
            )
            subprocess.run([str(binary_path)], check=True)

    def test_debounced_input_publishes_only_the_latest_value(self):
        self.compile_and_run(
            r"""
            #include "ha_switch_sync.h"

            #include <assert.h>

            int main(void)
            {
                ha_switch_sync_t state;
                ha_switch_sync_init(&state, 70);

                ha_switch_sync_action_t first = ha_switch_sync_user_changed(
                    &state, 71, HA_SWITCH_SYNC_DEBOUNCED);
                assert(first.kind == HA_SWITCH_SYNC_ACTION_SCHEDULE);

                ha_switch_sync_action_t second = ha_switch_sync_user_changed(
                    &state, 72, HA_SWITCH_SYNC_DEBOUNCED);
                assert(second.kind == HA_SWITCH_SYNC_ACTION_SCHEDULE);
                assert(second.generation > first.generation);

                ha_switch_sync_action_t flush =
                    ha_switch_sync_debounce_elapsed(&state);
                assert(flush.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);
                assert(flush.generation == second.generation);
                assert(flush.value == 72);

                ha_switch_sync_action_t duplicate =
                    ha_switch_sync_debounce_elapsed(&state);
                assert(duplicate.kind == HA_SWITCH_SYNC_ACTION_NONE);
                return 0;
            }
            """
        )

    def test_out_of_order_results_preserve_newer_visible_input(self):
        self.compile_and_run(
            r"""
            #include "ha_switch_sync.h"

            #include <assert.h>

            int main(void)
            {
                ha_switch_sync_t state;
                ha_switch_sync_init(&state, 0);

                ha_switch_sync_action_t first = ha_switch_sync_user_changed(
                    &state, 1, HA_SWITCH_SYNC_IMMEDIATE);
                ha_switch_sync_action_t second = ha_switch_sync_user_changed(
                    &state, 0, HA_SWITCH_SYNC_IMMEDIATE);
                assert(first.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);
                assert(second.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);

                ha_switch_sync_action_t old_accepted =
                    ha_switch_sync_publish_result(
                        &state, first.generation, first.value, true);
                assert(old_accepted.kind == HA_SWITCH_SYNC_ACTION_NONE);

                ha_switch_sync_action_t current_rejected =
                    ha_switch_sync_publish_result(
                        &state, second.generation, 1, false);
                assert(current_rejected.kind == HA_SWITCH_SYNC_ACTION_APPLY);
                assert(current_rejected.value == 1);
                return 0;
            }
            """
        )

    def test_model_update_cancels_pending_input_and_masks_stale_result(self):
        self.compile_and_run(
            r"""
            #include "ha_switch_sync.h"

            #include <assert.h>

            int main(void)
            {
                ha_switch_sync_t state;
                ha_switch_sync_init(&state, 70);

                ha_switch_sync_action_t pending = ha_switch_sync_user_changed(
                    &state, 75, HA_SWITCH_SYNC_DEBOUNCED);
                assert(pending.kind == HA_SWITCH_SYNC_ACTION_SCHEDULE);

                ha_switch_sync_action_t model =
                    ha_switch_sync_model_changed(&state, 68);
                assert(
                    model.kind ==
                    HA_SWITCH_SYNC_ACTION_CANCEL_AND_APPLY);
                assert(model.value == 68);

                ha_switch_sync_action_t expired =
                    ha_switch_sync_debounce_elapsed(&state);
                assert(expired.kind == HA_SWITCH_SYNC_ACTION_NONE);

                ha_switch_sync_action_t stale =
                    ha_switch_sync_publish_result(
                        &state, pending.generation, pending.value, true);
                assert(stale.kind == HA_SWITCH_SYNC_ACTION_NONE);

                ha_switch_sync_action_t next = ha_switch_sync_user_changed(
                    &state, 69, HA_SWITCH_SYNC_IMMEDIATE);
                ha_switch_sync_action_t rejected =
                    ha_switch_sync_publish_result(
                        &state, next.generation, 68, false);
                assert(rejected.kind == HA_SWITCH_SYNC_ACTION_APPLY);
                assert(rejected.value == 68);
                return 0;
            }
            """
        )

    def test_rejection_uses_controller_authoritative_rollback_value(self):
        self.compile_and_run(
            r"""
            #include "ha_switch_sync.h"

            #include <assert.h>

            int main(void)
            {
                ha_switch_sync_t state;
                ha_switch_sync_init(&state, 0);

                ha_switch_sync_action_t first = ha_switch_sync_user_changed(
                    &state, 1, HA_SWITCH_SYNC_IMMEDIATE);
                ha_switch_sync_action_t second = ha_switch_sync_user_changed(
                    &state, 0, HA_SWITCH_SYNC_IMMEDIATE);
                assert(first.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);
                assert(second.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);

                /*
                 * The first publish succeeded in the controller, but its
                 * result event was lost. The second publish result carries
                 * the controller's persisted value as the rollback truth.
                 */
                ha_switch_sync_action_t rejected =
                    ha_switch_sync_publish_result(
                        &state, second.generation, 1, false);
                assert(rejected.kind == HA_SWITCH_SYNC_ACTION_APPLY);
                assert(rejected.value == 1);
                return 0;
            }
            """
        )

    def test_late_success_reapplies_after_result_timeout(self):
        self.compile_and_run(
            r"""
            #include "ha_switch_sync.h"

            #include <assert.h>

            int main(void)
            {
                ha_switch_sync_t state;
                ha_switch_sync_init(&state, 0);

                ha_switch_sync_action_t publish = ha_switch_sync_user_changed(
                    &state, 1, HA_SWITCH_SYNC_IMMEDIATE);
                assert(publish.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);

                ha_switch_sync_action_t timeout =
                    ha_switch_sync_result_timeout(
                        &state, publish.generation);
                assert(timeout.kind == HA_SWITCH_SYNC_ACTION_APPLY);
                assert(timeout.value == 0);

                ha_switch_sync_action_t late_success =
                    ha_switch_sync_publish_result(
                        &state, publish.generation, 1, true);
                assert(late_success.kind == HA_SWITCH_SYNC_ACTION_APPLY);
                assert(late_success.value == 1);
                return 0;
            }
            """
        )

    def test_older_pending_success_reapplies_after_newer_local_rejection(self):
        self.compile_and_run(
            r"""
            #include "ha_switch_sync.h"

            #include <assert.h>

            int main(void)
            {
                ha_switch_sync_t state;
                ha_switch_sync_init(&state, 0);

                ha_switch_sync_action_t older = ha_switch_sync_user_changed(
                    &state, 1, HA_SWITCH_SYNC_IMMEDIATE);
                assert(older.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);

                ha_switch_sync_action_t newer = ha_switch_sync_user_changed(
                    &state, 0, HA_SWITCH_SYNC_IMMEDIATE);
                assert(newer.kind == HA_SWITCH_SYNC_ACTION_PUBLISH);

                ha_switch_sync_action_t local_rejection =
                    ha_switch_sync_local_rejected(
                        &state, newer.generation);
                assert(
                    local_rejection.kind ==
                    HA_SWITCH_SYNC_ACTION_APPLY);
                assert(local_rejection.value == 0);

                ha_switch_sync_action_t older_success =
                    ha_switch_sync_publish_result(
                        &state, older.generation, 1, true);
                assert(
                    older_success.kind ==
                    HA_SWITCH_SYNC_ACTION_APPLY);
                assert(older_success.value == 1);
                return 0;
            }
            """
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
