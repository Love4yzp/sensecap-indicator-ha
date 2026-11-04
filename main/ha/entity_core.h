#ifndef ENTITY_CORE_H
#define ENTITY_CORE_H

#include "entity_registry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENTITY_CORE_OBSERVER_CAPACITY 2

typedef enum {
    ENTITY_CORE_SOURCE_BOOT = 0,
    ENTITY_CORE_SOURCE_LOCAL_UI,
    ENTITY_CORE_SOURCE_MQTT,
    ENTITY_CORE_SOURCE_NATIVE,
    ENTITY_CORE_SOURCE_SENSOR,
} entity_core_source_t;

typedef enum {
    ENTITY_CORE_STATUS_OK = 0,
    ENTITY_CORE_STATUS_INVALID_ARGUMENT,
    ENTITY_CORE_STATUS_UNKNOWN_ENTITY,
    ENTITY_CORE_STATUS_READ_ONLY,
    ENTITY_CORE_STATUS_NOT_INITIALIZED,
    ENTITY_CORE_STATUS_NON_FINITE,
    ENTITY_CORE_STATUS_OUT_OF_RANGE,
    ENTITY_CORE_STATUS_STEP_MISMATCH,
} entity_core_status_t;

typedef struct {
    uint8_t index;
    float value;
    uint32_t generation;
    entity_core_source_t source;
} entity_core_command_t;

typedef struct {
    uint8_t index;
    float value;
    uint32_t generation;
    uint64_t state_version;
    bool accepted;
    entity_core_status_t status;
} entity_core_command_result_t;

typedef struct {
    uint8_t index;
    entity_kind_t kind;
    float value;
    uint64_t state_version;
    entity_core_source_t source;
    bool valid;
} entity_core_snapshot_t;

/* Observers run outside the state lock and must return without blocking.
 * They may read snapshots, but must not recursively submit commands, sensor
 * updates, or snapshot replays while the notification is being delivered. */
typedef void (*entity_core_observer_t)(
    const entity_core_snapshot_t *snapshot, void *context);

bool entity_core_init(void);
entity_core_status_t entity_core_submit_command(
    const entity_core_command_t *command,
    entity_core_command_result_t *result);
bool entity_core_set_sensor(uint8_t index, float value);
bool entity_core_get_snapshot(uint8_t index, entity_core_snapshot_t *snapshot);
bool entity_core_replay(entity_core_observer_t observer, void *context);
bool entity_core_add_observer(entity_core_observer_t observer, void *context);

#ifdef __cplusplus
}
#endif

#endif /* ENTITY_CORE_H */
