#!/usr/bin/env python3
"""Host behavior tests for the entity registry and authoritative entity core."""

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class EntityCoreTests(unittest.TestCase):
    def compile_and_run(self, harness: str) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            harness_path = tmp / "entity_core_harness.c"
            binary_path = tmp / "entity_core_harness"
            harness_path.write_text(textwrap.dedent(harness), encoding="utf-8")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DENTITY_CORE_HOST_TEST",
                    "-I",
                    str(ROOT / "main"),
                    "-I",
                    str(ROOT / "main" / "ha"),
                    str(ROOT / "main" / "ha" / "entity_registry.c"),
                    str(ROOT / "main" / "ha" / "entity_core.c"),
                    str(harness_path),
                    "-pthread",
                    "-lm",
                    "-o",
                    str(binary_path),
                ],
                check=True,
            )
            subprocess.run([str(binary_path)], check=True, timeout=5)

    def test_registry_has_unique_stable_identities_and_control_types(self):
        self.compile_and_run(
            r"""
            #include "entity_registry.h"

            #include <assert.h>
            #include <string.h>

            int main(void)
            {
                assert(entity_registry_validate());
                assert(entity_registry_count() == 12);
                assert(ENTITY_SENSOR_COUNT == 4);
                assert(ENTITY_SWITCH_COUNT == 6);
                assert(ENTITY_NUMBER_COUNT == 2);

                size_t sensors = 0;
                size_t switches = 0;
                size_t numbers = 0;
                for (uint8_t i = 0; i < ENTITY_INDEX_COUNT; ++i) {
                    const entity_descriptor_t *entity = entity_registry_get(i);
                    assert(entity != NULL);
                    assert(entity->index == i);
                    assert(entity->native_key != 0);
                    assert(entity->object_id[0] != '\0');
                    assert(entity->name[0] != '\0');
                    sensors += entity->kind == ENTITY_KIND_SENSOR;
                    switches += entity->kind == ENTITY_KIND_SWITCH;
                    numbers += entity->kind == ENTITY_KIND_NUMBER;
                    for (uint8_t j = i + 1; j < ENTITY_INDEX_COUNT; ++j) {
                        const entity_descriptor_t *other = entity_registry_get(j);
                        assert(entity->native_key != other->native_key);
                        assert(strcmp(entity->object_id, other->object_id) != 0);
                        assert(strcmp(entity->name, other->name) != 0);
                    }
                }
                assert(sensors == 4);
                assert(switches == 6);
                assert(numbers == 2);
                assert(entity_registry_control_index(0) == ENTITY_INDEX_SWITCH_1);
                assert(entity_registry_control_index(4) == ENTITY_INDEX_NUMBER_1);
                assert(entity_registry_control_index(7) == ENTITY_INDEX_NUMBER_2);
                assert(entity_registry_control_index(8) == ENTITY_INDEX_INVALID);
                assert(entity_registry_get(ENTITY_INDEX_COUNT) == NULL);
                return 0;
            }
            """
        )

    def test_commands_snapshots_sources_and_observers(self):
        self.compile_and_run(
            r"""
            #include "entity_core.h"

            #include <assert.h>
            #include <math.h>
            #include <stddef.h>

            static unsigned observer_one_calls;
            static unsigned observer_two_calls;
            static entity_core_snapshot_t observer_one_latest;
            static entity_core_snapshot_t observer_two_latest;

            static void observer_one(
                const entity_core_snapshot_t *snapshot, void *context)
            {
                assert(context == &observer_one_calls);
                entity_core_snapshot_t locked_read;
                assert(entity_core_get_snapshot(snapshot->index, &locked_read));
                assert(locked_read.state_version == snapshot->state_version);
                observer_one_latest = *snapshot;
                observer_one_calls++;
            }

            static void observer_two(
                const entity_core_snapshot_t *snapshot, void *context)
            {
                assert(context == &observer_two_calls);
                entity_core_snapshot_t locked_read;
                assert(entity_core_get_snapshot(snapshot->index, &locked_read));
                assert(locked_read.value == snapshot->value);
                observer_two_latest = *snapshot;
                observer_two_calls++;
            }

            static void unused_observer(
                const entity_core_snapshot_t *snapshot, void *context)
            {
                (void)snapshot;
                (void)context;
                assert(0);
            }

            static entity_core_command_result_t submit(
                uint8_t index, float value, uint32_t generation,
                entity_core_source_t source)
            {
                entity_core_command_t command = {
                    .index = index,
                    .value = value,
                    .generation = generation,
                    .source = source,
                };
                entity_core_command_result_t result;
                entity_core_status_t status =
                    entity_core_submit_command(&command, &result);
                assert(status == result.status);
                assert(result.index == index);
                assert(result.generation == generation);
                return result;
            }

            int main(void)
            {
                assert(entity_core_init());
                assert(entity_core_init());

                for (uint8_t i = 0; i < ENTITY_INDEX_COUNT; ++i) {
                    entity_core_snapshot_t snapshot;
                    assert(entity_core_get_snapshot(i, &snapshot));
                    assert(snapshot.index == i);
                    assert(snapshot.state_version == 0);
                    assert(snapshot.source == ENTITY_CORE_SOURCE_BOOT);
                    assert(snapshot.value == 0.0f);
                    assert(snapshot.valid ==
                           (snapshot.kind != ENTITY_KIND_SENSOR));
                }

                assert(entity_core_add_observer(
                    observer_one, &observer_one_calls));
                assert(entity_core_add_observer(
                    observer_one, &observer_one_calls));
                assert(entity_core_add_observer(
                    observer_two, &observer_two_calls));
                assert(!entity_core_add_observer(unused_observer, NULL));

                entity_core_command_result_t local = submit(
                    ENTITY_INDEX_SWITCH_1, 1.0f, 41,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(local.accepted);
                assert(local.status == ENTITY_CORE_STATUS_OK);
                assert(local.value == 1.0f);
                assert(local.generation == 41);
                assert(local.state_version == 1);
                assert(observer_one_calls == 1);
                assert(observer_two_calls == 1);
                assert(observer_one_latest.source == ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(observer_two_latest.state_version == local.state_version);

                entity_core_snapshot_t snapshot;
                assert(entity_core_get_snapshot(ENTITY_INDEX_SWITCH_1, &snapshot));
                assert(snapshot.valid && snapshot.value == 1.0f);
                uint64_t committed_version = snapshot.state_version;

                entity_core_command_result_t rejected = submit(
                    200, 1.0f, 42, ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(!rejected.accepted);
                assert(rejected.status == ENTITY_CORE_STATUS_UNKNOWN_ENTITY);
                rejected = submit(
                    ENTITY_INDEX_SENSOR_TEMP, 1.0f, 43,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(rejected.status == ENTITY_CORE_STATUS_READ_ONLY);
                rejected = submit(
                    ENTITY_INDEX_SWITCH_1, 2.0f, 44,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(rejected.status == ENTITY_CORE_STATUS_OUT_OF_RANGE);
                assert(rejected.value == 1.0f);
                assert(rejected.state_version == committed_version);
                rejected = submit(
                    ENTITY_INDEX_NUMBER_1, NAN, 45,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(rejected.status == ENTITY_CORE_STATUS_NON_FINITE);
                rejected = submit(
                    ENTITY_INDEX_NUMBER_1, INFINITY, 46,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(rejected.status == ENTITY_CORE_STATUS_NON_FINITE);
                rejected = submit(
                    ENTITY_INDEX_NUMBER_1, 101.0f, 47,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(rejected.status == ENTITY_CORE_STATUS_OUT_OF_RANGE);
                rejected = submit(
                    ENTITY_INDEX_NUMBER_1, 0.5f, 48,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(rejected.status == ENTITY_CORE_STATUS_STEP_MISMATCH);
                assert(observer_one_calls == 1);
                assert(observer_two_calls == 1);
                assert(entity_core_get_snapshot(ENTITY_INDEX_NUMBER_1, &snapshot));
                assert(snapshot.value == 0.0f && snapshot.state_version == 0);

                int ui_value = 72;
                entity_core_command_result_t number = submit(
                    ENTITY_INDEX_NUMBER_1, (float)ui_value, 50,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                int ui_result = (int)number.value;
                assert(number.accepted && ui_result == ui_value);
                assert(number.generation == 50);
                assert(observer_one_calls == 2 && observer_two_calls == 2);

                entity_core_command_result_t remote = submit(
                    ENTITY_INDEX_SWITCH_2, 1.0f, 0,
                    ENTITY_CORE_SOURCE_MQTT);
                assert(remote.accepted);
                assert(remote.generation == 0);
                assert(remote.state_version > number.state_version);
                assert(observer_one_latest.source == ENTITY_CORE_SOURCE_MQTT);
                assert(observer_one_latest.index == ENTITY_INDEX_SWITCH_2);

                entity_core_command_result_t newer_generation = submit(
                    ENTITY_INDEX_NUMBER_1, 20.0f, 100,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                entity_core_command_result_t older_generation = submit(
                    ENTITY_INDEX_NUMBER_1, 30.0f, 10,
                    ENTITY_CORE_SOURCE_LOCAL_UI);
                assert(newer_generation.generation == 100);
                assert(older_generation.generation == 10);
                assert(older_generation.state_version >
                       newer_generation.state_version);
                assert(observer_one_latest.value == 30.0f);
                assert(observer_one_latest.state_version ==
                       older_generation.state_version);

                unsigned before_reconnect_read = observer_one_calls;
                assert(entity_core_get_snapshot(ENTITY_INDEX_NUMBER_1, &snapshot));
                assert(snapshot.valid && snapshot.value == 30.0f);
                assert(snapshot.state_version == older_generation.state_version);
                assert(observer_one_calls == before_reconnect_read);

                assert(entity_core_set_sensor(ENTITY_INDEX_SENSOR_TEMP, 23.5f));
                assert(observer_one_latest.index == ENTITY_INDEX_SENSOR_TEMP);
                assert(observer_one_latest.source == ENTITY_CORE_SOURCE_SENSOR);
                assert(observer_one_latest.valid);
                assert(observer_one_latest.value == 23.5f);
                assert(observer_one_latest.state_version > snapshot.state_version);
                uint64_t sensor_version = observer_one_latest.state_version;
                assert(!entity_core_set_sensor(ENTITY_INDEX_SENSOR_TEMP, NAN));
                assert(!entity_core_set_sensor(ENTITY_INDEX_SWITCH_1, 20.0f));
                assert(entity_core_get_snapshot(ENTITY_INDEX_SENSOR_TEMP, &snapshot));
                assert(snapshot.value == 23.5f);
                assert(snapshot.state_version == sensor_version);
                return 0;
            }
            """
        )

    def test_concurrent_updates_notify_observers_in_commit_order(self):
        self.compile_and_run(
            r"""
            #include "entity_core.h"

            #include <assert.h>
            #include <pthread.h>
            #include <sched.h>

            #define COMMAND_THREAD_COUNT 4
            #define SENSOR_THREAD_COUNT 2
            #define UPDATES_PER_THREAD 400

            typedef struct {
                uint64_t last_version[ENTITY_INDEX_COUNT];
                float last_value[ENTITY_INDEX_COUNT];
                unsigned calls[ENTITY_INDEX_COUNT];
                unsigned total_calls;
            } observer_log_t;

            typedef struct {
                uint8_t index;
                uint32_t generation;
            } worker_config_t;

            static observer_log_t observer_logs[2];

            static void record_snapshot(
                const entity_core_snapshot_t *snapshot, void *context)
            {
                observer_log_t *log = context;
                uint8_t index = snapshot->index;
                assert(index < ENTITY_INDEX_COUNT);
                assert(snapshot->state_version > log->last_version[index]);

                entity_core_snapshot_t core_snapshot;
                assert(entity_core_get_snapshot(index, &core_snapshot));
                assert(core_snapshot.state_version >= snapshot->state_version);
                sched_yield();

                log->last_version[index] = snapshot->state_version;
                log->last_value[index] = snapshot->value;
                log->calls[index]++;
                log->total_calls++;
            }

            static void *submit_commands(void *context)
            {
                const worker_config_t *config = context;
                for (int i = 0; i < UPDATES_PER_THREAD; ++i) {
                    entity_core_command_t command = {
                        .index = config->index,
                        .value = (float)((i + config->generation) % 101),
                        .generation = config->generation,
                        .source = ENTITY_CORE_SOURCE_NATIVE,
                    };
                    entity_core_command_result_t result;
                    assert(entity_core_submit_command(&command, &result) ==
                           ENTITY_CORE_STATUS_OK);
                    assert(result.accepted);
                    assert(result.generation == config->generation);
                }
                return NULL;
            }

            static void *update_sensor(void *context)
            {
                const worker_config_t *config = context;
                for (int i = 0; i < UPDATES_PER_THREAD; ++i) {
                    float value =
                        1.0f + (float)((i + config->generation) % 500);
                    assert(entity_core_set_sensor(config->index, value));
                }
                return NULL;
            }

            int main(void)
            {
                assert(entity_core_init());
                assert(entity_core_add_observer(
                    record_snapshot, &observer_logs[0]));
                assert(entity_core_add_observer(
                    record_snapshot, &observer_logs[1]));

                worker_config_t command_configs[COMMAND_THREAD_COUNT] = {
                    {ENTITY_INDEX_NUMBER_1, 1},
                    {ENTITY_INDEX_NUMBER_2, 2},
                    {ENTITY_INDEX_NUMBER_1, 3},
                    {ENTITY_INDEX_NUMBER_2, 4},
                };
                worker_config_t sensor_configs[SENSOR_THREAD_COUNT] = {
                    {ENTITY_INDEX_SENSOR_TEMP, 5},
                    {ENTITY_INDEX_SENSOR_CO2, 6},
                };
                pthread_t command_threads[COMMAND_THREAD_COUNT];
                pthread_t sensor_threads[SENSOR_THREAD_COUNT];
                for (size_t i = 0; i < COMMAND_THREAD_COUNT; ++i) {
                    assert(pthread_create(
                               &command_threads[i], NULL, submit_commands,
                               &command_configs[i]) == 0);
                }
                for (size_t i = 0; i < SENSOR_THREAD_COUNT; ++i) {
                    assert(pthread_create(
                               &sensor_threads[i], NULL, update_sensor,
                               &sensor_configs[i]) == 0);
                }
                for (size_t i = 0; i < COMMAND_THREAD_COUNT; ++i) {
                    assert(pthread_join(command_threads[i], NULL) == 0);
                }
                for (size_t i = 0; i < SENSOR_THREAD_COUNT; ++i) {
                    assert(pthread_join(sensor_threads[i], NULL) == 0);
                }

                const uint8_t indices[] = {
                    ENTITY_INDEX_NUMBER_1,
                    ENTITY_INDEX_NUMBER_2,
                    ENTITY_INDEX_SENSOR_TEMP,
                    ENTITY_INDEX_SENSOR_CO2,
                };
                const unsigned expected_calls[] = {
                    2 * UPDATES_PER_THREAD,
                    2 * UPDATES_PER_THREAD,
                    UPDATES_PER_THREAD,
                    UPDATES_PER_THREAD,
                };
                for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
                    entity_core_snapshot_t snapshot;
                    assert(entity_core_get_snapshot(indices[i], &snapshot));
                    assert(snapshot.valid);
                    for (size_t observer = 0; observer < 2; ++observer) {
                        const observer_log_t *log = &observer_logs[observer];
                        assert(log->calls[indices[i]] == expected_calls[i]);
                        assert(log->last_version[indices[i]] ==
                               snapshot.state_version);
                        assert(log->last_value[indices[i]] == snapshot.value);
                    }
                }
                for (size_t observer = 0; observer < 2; ++observer) {
                    assert(observer_logs[observer].total_calls ==
                           (COMMAND_THREAD_COUNT + SENSOR_THREAD_COUNT) *
                               UPDATES_PER_THREAD);
                }
                return 0;
            }
            """
        )

    def test_replay_serializes_with_concurrent_commit(self):
        self.compile_and_run(
            r"""
            #include "entity_core.h"

            #include <assert.h>
            #include <pthread.h>
            #include <sched.h>
            #include <stdatomic.h>

            typedef struct {
                bool seen;
                uint64_t last_version;
                float last_value;
                unsigned number_calls;
            } replay_log_t;

            static replay_log_t replay_log;
            static atomic_int replay_started;
            static atomic_int replay_finished;

            static void record_replay(
                const entity_core_snapshot_t *snapshot, void *context)
            {
                replay_log_t *log = context;
                atomic_store_explicit(
                    &replay_started, 1, memory_order_release);
                if (snapshot->index != ENTITY_INDEX_NUMBER_1) {
                    sched_yield();
                    return;
                }

                entity_core_snapshot_t core_snapshot;
                assert(entity_core_get_snapshot(
                    snapshot->index, &core_snapshot));
                assert(core_snapshot.state_version >= snapshot->state_version);
                if (log->seen) {
                    assert(snapshot->state_version >= log->last_version);
                }
                log->seen = true;
                log->last_version = snapshot->state_version;
                log->last_value = snapshot->value;
                log->number_calls++;
                sched_yield();
            }

            static void *replay_snapshots(void *context)
            {
                assert(entity_core_replay(record_replay, context));
                atomic_store_explicit(
                    &replay_finished, 1, memory_order_release);
                return NULL;
            }

            static void *commit_new_value(void *context)
            {
                (void)context;
                entity_core_command_t command = {
                    .index = ENTITY_INDEX_NUMBER_1,
                    .value = 73.0f,
                    .generation = 9,
                    .source = ENTITY_CORE_SOURCE_NATIVE,
                };
                entity_core_command_result_t result;
                assert(entity_core_submit_command(&command, &result) ==
                       ENTITY_CORE_STATUS_OK);
                assert(result.accepted && result.value == 73.0f);
                assert(atomic_load_explicit(
                    &replay_finished, memory_order_acquire));
                return NULL;
            }

            int main(void)
            {
                assert(entity_core_init());
                assert(!entity_core_replay(NULL, NULL));
                assert(entity_core_add_observer(record_replay, &replay_log));

                pthread_t replay_thread;
                pthread_t command_thread;
                assert(pthread_create(
                           &replay_thread, NULL, replay_snapshots,
                           &replay_log) == 0);
                while (!atomic_load_explicit(
                    &replay_started, memory_order_acquire)) {
                    sched_yield();
                }
                assert(pthread_create(
                           &command_thread, NULL, commit_new_value,
                           NULL) == 0);
                assert(pthread_join(replay_thread, NULL) == 0);
                assert(pthread_join(command_thread, NULL) == 0);

                entity_core_snapshot_t snapshot;
                assert(entity_core_get_snapshot(
                    ENTITY_INDEX_NUMBER_1, &snapshot));
                assert(snapshot.valid && snapshot.value == 73.0f);
                assert(replay_log.seen);
                assert(replay_log.number_calls == 2);
                assert(replay_log.last_version == snapshot.state_version);
                assert(replay_log.last_value == snapshot.value);
                return 0;
            }
            """
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
