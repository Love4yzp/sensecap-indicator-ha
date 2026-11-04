#ifndef ENTITY_REGISTRY_H
#define ENTITY_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENTITY_KIND_SENSOR = 0,
    ENTITY_KIND_SWITCH,
    ENTITY_KIND_NUMBER,
} entity_kind_t;

typedef enum {
    ENTITY_INDEX_SENSOR_TEMP = 0,
    ENTITY_INDEX_SENSOR_HUMIDITY,
    ENTITY_INDEX_SENSOR_TVOC,
    ENTITY_INDEX_SENSOR_CO2,
    ENTITY_INDEX_SWITCH_1,
    ENTITY_INDEX_SWITCH_2,
    ENTITY_INDEX_SWITCH_3,
    ENTITY_INDEX_SWITCH_4,
    ENTITY_INDEX_NUMBER_1,
    ENTITY_INDEX_SWITCH_6,
    ENTITY_INDEX_SWITCH_7,
    ENTITY_INDEX_NUMBER_2,
    ENTITY_INDEX_COUNT,
    ENTITY_INDEX_INVALID = 0xff,
} entity_index_t;

#define ENTITY_SENSOR_COUNT 4
#define ENTITY_SWITCH_COUNT 6
#define ENTITY_NUMBER_COUNT 2
#define ENTITY_CONTROL_COUNT (ENTITY_SWITCH_COUNT + ENTITY_NUMBER_COUNT)
#define ENTITY_INDEX_CONTROL_FIRST ENTITY_INDEX_SWITCH_1

typedef struct {
    uint8_t index;
    entity_kind_t kind;
    const char *object_id;
    uint32_t native_key;
    const char *name;
    const char *unit;
    float min_value;
    float max_value;
    float step;
    float default_value;
} entity_descriptor_t;

size_t entity_registry_count(void);
const entity_descriptor_t *entity_registry_get(uint8_t index);
uint8_t entity_registry_control_index(uint8_t control_slot);
bool entity_registry_validate(void);

#ifdef __cplusplus
}
#endif

#endif /* ENTITY_REGISTRY_H */
