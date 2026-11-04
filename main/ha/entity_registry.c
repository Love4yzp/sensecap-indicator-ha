#include "entity_registry.h"

#include "home_assistant_config.h"

#include <string.h>

static const entity_descriptor_t s_entities[ENTITY_INDEX_COUNT] = {
    [ENTITY_INDEX_SENSOR_TEMP] = {
        .index = ENTITY_INDEX_SENSOR_TEMP,
        .kind = ENTITY_KIND_SENSOR,
        .object_id = "temp",
        .native_key = 0x10000001u,
        .name = CONFIG_SENSOR1_UI_NAME,
        .unit = CONFIG_SENSOR1_UI_UNIT,
    },
    [ENTITY_INDEX_SENSOR_HUMIDITY] = {
        .index = ENTITY_INDEX_SENSOR_HUMIDITY,
        .kind = ENTITY_KIND_SENSOR,
        .object_id = "humidity",
        .native_key = 0x10000002u,
        .name = CONFIG_SENSOR2_UI_NAME,
        .unit = CONFIG_SENSOR2_UI_UNIT,
    },
    [ENTITY_INDEX_SENSOR_TVOC] = {
        .index = ENTITY_INDEX_SENSOR_TVOC,
        .kind = ENTITY_KIND_SENSOR,
        .object_id = "tvoc",
        .native_key = 0x10000003u,
        .name = CONFIG_SENSOR3_UI_NAME,
        .unit = "index",
    },
    [ENTITY_INDEX_SENSOR_CO2] = {
        .index = ENTITY_INDEX_SENSOR_CO2,
        .kind = ENTITY_KIND_SENSOR,
        .object_id = "co2",
        .native_key = 0x10000004u,
        .name = CONFIG_SENSOR4_UI_NAME,
        .unit = CONFIG_SENSOR4_UI_UNIT,
    },
    [ENTITY_INDEX_SWITCH_1] = {
        .index = ENTITY_INDEX_SWITCH_1,
        .kind = ENTITY_KIND_SWITCH,
        .object_id = "switch1",
        .native_key = 0x10000005u,
        .name = CONFIG_SWITCH1_UI_NAME,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
    [ENTITY_INDEX_SWITCH_2] = {
        .index = ENTITY_INDEX_SWITCH_2,
        .kind = ENTITY_KIND_SWITCH,
        .object_id = "switch2",
        .native_key = 0x10000006u,
        .name = CONFIG_SWITCH2_UI_NAME,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
    [ENTITY_INDEX_SWITCH_3] = {
        .index = ENTITY_INDEX_SWITCH_3,
        .kind = ENTITY_KIND_SWITCH,
        .object_id = "switch3",
        .native_key = 0x10000007u,
        .name = CONFIG_SWITCH3_UI_NAME,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
    [ENTITY_INDEX_SWITCH_4] = {
        .index = ENTITY_INDEX_SWITCH_4,
        .kind = ENTITY_KIND_SWITCH,
        .object_id = "switch4",
        .native_key = 0x10000008u,
        .name = CONFIG_SWITCH4_UI_NAME,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
    [ENTITY_INDEX_NUMBER_1] = {
        .index = ENTITY_INDEX_NUMBER_1,
        .kind = ENTITY_KIND_NUMBER,
        .object_id = "number1",
        .native_key = 0x10000009u,
        .name = CONFIG_SWITCH5_UI_NAME,
        .unit = "°C",
        .min_value = 0.0f,
        .max_value = 100.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
    [ENTITY_INDEX_SWITCH_6] = {
        .index = ENTITY_INDEX_SWITCH_6,
        .kind = ENTITY_KIND_SWITCH,
        .object_id = "switch6",
        .native_key = 0x1000000au,
        .name = CONFIG_SWITCH6_UI_NAME,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
    [ENTITY_INDEX_SWITCH_7] = {
        .index = ENTITY_INDEX_SWITCH_7,
        .kind = ENTITY_KIND_SWITCH,
        .object_id = "switch7",
        .native_key = 0x1000000bu,
        .name = CONFIG_SWITCH7_UI_NAME,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
    [ENTITY_INDEX_NUMBER_2] = {
        .index = ENTITY_INDEX_NUMBER_2,
        .kind = ENTITY_KIND_NUMBER,
        .object_id = "number2",
        .native_key = 0x1000000cu,
        .name = CONFIG_SWITCH8_UI_NAME,
        .min_value = 0.0f,
        .max_value = 100.0f,
        .step = 1.0f,
        .default_value = 0.0f,
    },
};

size_t entity_registry_count(void)
{
    return ENTITY_INDEX_COUNT;
}

const entity_descriptor_t *entity_registry_get(uint8_t index)
{
    if (index >= ENTITY_INDEX_COUNT) {
        return NULL;
    }
    return &s_entities[index];
}

uint8_t entity_registry_control_index(uint8_t control_slot)
{
    if (control_slot >= ENTITY_CONTROL_COUNT) {
        return ENTITY_INDEX_INVALID;
    }
    return (uint8_t)(ENTITY_INDEX_CONTROL_FIRST + control_slot);
}

bool entity_registry_validate(void)
{
    for (size_t i = 0; i < ENTITY_INDEX_COUNT; ++i) {
        const entity_descriptor_t *entity = &s_entities[i];
        if (entity->index != i || entity->object_id == NULL ||
            entity->object_id[0] == '\0' || entity->name == NULL ||
            entity->name[0] == '\0' || entity->native_key == 0) {
            return false;
        }
        for (size_t j = i + 1; j < ENTITY_INDEX_COUNT; ++j) {
            const entity_descriptor_t *other = &s_entities[j];
            if (entity->native_key == other->native_key ||
                strcmp(entity->object_id, other->object_id) == 0 ||
                strcmp(entity->name, other->name) == 0) {
                return false;
            }
        }
    }
    return true;
}
