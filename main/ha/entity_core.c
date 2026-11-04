#include "entity_core.h"

#include <math.h>
#include <string.h>

#ifdef ENTITY_CORE_HOST_TEST
#include <pthread.h>
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

typedef struct {
    entity_core_snapshot_t snapshot;
} entity_state_t;

typedef struct {
    entity_core_observer_t callback;
    void *context;
} observer_slot_t;

static entity_state_t s_states[ENTITY_INDEX_COUNT];
static observer_slot_t s_observers[ENTITY_CORE_OBSERVER_CAPACITY];
static size_t s_observer_count;
static uint64_t s_state_version;
static bool s_initialized;

#ifdef ENTITY_CORE_HOST_TEST
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_notification_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool platform_locks_init(void)
{
    return true;
}

static void platform_lock(void)
{
    pthread_mutex_lock(&s_mutex);
}

static void platform_unlock(void)
{
    pthread_mutex_unlock(&s_mutex);
}

static void platform_notification_lock(void)
{
    pthread_mutex_lock(&s_notification_mutex);
}

static void platform_notification_unlock(void)
{
    pthread_mutex_unlock(&s_notification_mutex);
}
#else
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_notification_mutex;

static bool platform_locks_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_notification_mutex == NULL) {
        s_notification_mutex = xSemaphoreCreateMutex();
    }
    return s_mutex != NULL && s_notification_mutex != NULL;
}

static void platform_lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void platform_unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static void platform_notification_lock(void)
{
    xSemaphoreTake(s_notification_mutex, portMAX_DELAY);
}

static void platform_notification_unlock(void)
{
    xSemaphoreGive(s_notification_mutex);
}
#endif

static void snapshot_from_state(
    uint8_t index, const entity_state_t *state, entity_core_snapshot_t *snapshot)
{
    *snapshot = state->snapshot;
    snapshot->index = index;
    snapshot->kind = entity_registry_get(index)->kind;
}

static entity_core_status_t validate_command(
    const entity_descriptor_t *entity, float value)
{
    if (entity->kind == ENTITY_KIND_SENSOR) {
        return ENTITY_CORE_STATUS_READ_ONLY;
    }
    if (!isfinite(value)) {
        return ENTITY_CORE_STATUS_NON_FINITE;
    }
    if (value < entity->min_value || value > entity->max_value) {
        return ENTITY_CORE_STATUS_OUT_OF_RANGE;
    }

    float remainder = fmodf(value - entity->min_value, entity->step);
    float tolerance = entity->step * 0.00001f;
    if (fabsf(remainder) > tolerance &&
        fabsf(fabsf(remainder) - entity->step) > tolerance) {
        return ENTITY_CORE_STATUS_STEP_MISMATCH;
    }
    return ENTITY_CORE_STATUS_OK;
}

static size_t copy_observers(observer_slot_t *observers)
{
    memcpy(observers, s_observers, sizeof(s_observers));
    return s_observer_count;
}

static void notify_observers(
    const entity_core_snapshot_t *snapshot,
    const observer_slot_t *observers,
    size_t observer_count)
{
    for (size_t i = 0; i < observer_count; ++i) {
        observers[i].callback(snapshot, observers[i].context);
    }
}

bool entity_core_init(void)
{
    if (!entity_registry_validate() || !platform_locks_init()) {
        return false;
    }

    platform_lock();
    if (!s_initialized) {
        for (size_t i = 0; i < ENTITY_INDEX_COUNT; ++i) {
            const entity_descriptor_t *entity = entity_registry_get((uint8_t)i);
            s_states[i].snapshot = (entity_core_snapshot_t){
                .index = entity->index,
                .kind = entity->kind,
                .value = entity->default_value,
                .source = ENTITY_CORE_SOURCE_BOOT,
                .valid = entity->kind != ENTITY_KIND_SENSOR,
            };
        }
        s_initialized = true;
    }
    platform_unlock();
    return true;
}

entity_core_status_t entity_core_submit_command(
    const entity_core_command_t *command,
    entity_core_command_result_t *result)
{
    if (result == NULL) {
        return ENTITY_CORE_STATUS_INVALID_ARGUMENT;
    }
    *result = (entity_core_command_result_t){
        .status = ENTITY_CORE_STATUS_INVALID_ARGUMENT,
    };
    if (command == NULL) {
        return result->status;
    }

    result->index = command->index;
    result->generation = command->generation;
    if (command->index >= ENTITY_INDEX_COUNT) {
        result->status = ENTITY_CORE_STATUS_UNKNOWN_ENTITY;
        return result->status;
    }

    platform_notification_lock();
    platform_lock();
    if (!s_initialized) {
        platform_unlock();
        platform_notification_unlock();
        result->status = ENTITY_CORE_STATUS_NOT_INITIALIZED;
        return result->status;
    }

    const entity_descriptor_t *entity = entity_registry_get(command->index);
    entity_state_t *state = &s_states[command->index];
    result->value = state->snapshot.value;
    result->state_version = state->snapshot.state_version;
    result->status = validate_command(entity, command->value);
    if (result->status != ENTITY_CORE_STATUS_OK) {
        platform_unlock();
        platform_notification_unlock();
        return result->status;
    }

    state->snapshot.value = command->value;
    state->snapshot.valid = true;
    state->snapshot.source = command->source;
    state->snapshot.state_version = ++s_state_version;
    result->value = state->snapshot.value;
    result->state_version = state->snapshot.state_version;
    result->accepted = true;

    entity_core_snapshot_t snapshot;
    observer_slot_t observers[ENTITY_CORE_OBSERVER_CAPACITY];
    snapshot_from_state(command->index, state, &snapshot);
    size_t observer_count = copy_observers(observers);
    platform_unlock();

    notify_observers(&snapshot, observers, observer_count);
    platform_notification_unlock();
    return ENTITY_CORE_STATUS_OK;
}

bool entity_core_set_sensor(uint8_t index, float value)
{
    if (index >= ENTITY_INDEX_COUNT) {
        return false;
    }
    const entity_descriptor_t *entity = entity_registry_get(index);
    if (entity->kind != ENTITY_KIND_SENSOR || !isfinite(value)) {
        return false;
    }

    platform_notification_lock();
    platform_lock();
    if (!s_initialized) {
        platform_unlock();
        platform_notification_unlock();
        return false;
    }

    entity_state_t *state = &s_states[index];
    state->snapshot.value = value;
    state->snapshot.valid = true;
    state->snapshot.source = ENTITY_CORE_SOURCE_SENSOR;
    state->snapshot.state_version = ++s_state_version;

    entity_core_snapshot_t snapshot;
    observer_slot_t observers[ENTITY_CORE_OBSERVER_CAPACITY];
    snapshot_from_state(index, state, &snapshot);
    size_t observer_count = copy_observers(observers);
    platform_unlock();

    notify_observers(&snapshot, observers, observer_count);
    platform_notification_unlock();
    return true;
}

bool entity_core_get_snapshot(uint8_t index, entity_core_snapshot_t *snapshot)
{
    if (index >= ENTITY_INDEX_COUNT || snapshot == NULL) {
        return false;
    }

    platform_lock();
    if (!s_initialized) {
        platform_unlock();
        return false;
    }
    snapshot_from_state(index, &s_states[index], snapshot);
    platform_unlock();
    return true;
}

bool entity_core_replay(entity_core_observer_t observer, void *context)
{
    if (observer == NULL) {
        return false;
    }

    platform_notification_lock();
    for (size_t i = 0; i < ENTITY_INDEX_COUNT; ++i) {
        entity_core_snapshot_t snapshot;
        platform_lock();
        if (!s_initialized) {
            platform_unlock();
            platform_notification_unlock();
            return false;
        }
        snapshot_from_state((uint8_t)i, &s_states[i], &snapshot);
        platform_unlock();
        observer(&snapshot, context);
    }
    platform_notification_unlock();
    return true;
}

bool entity_core_add_observer(entity_core_observer_t observer, void *context)
{
    if (observer == NULL) {
        return false;
    }

    platform_lock();
    if (!s_initialized) {
        platform_unlock();
        return false;
    }
    for (size_t i = 0; i < s_observer_count; ++i) {
        if (s_observers[i].callback == observer &&
            s_observers[i].context == context) {
            platform_unlock();
            return true;
        }
    }
    if (s_observer_count >= ENTITY_CORE_OBSERVER_CAPACITY) {
        platform_unlock();
        return false;
    }

    s_observers[s_observer_count++] = (observer_slot_t){
        .callback = observer,
        .context = context,
    };
    platform_unlock();
    return true;
}
