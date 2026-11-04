/* Generated from the pinned api.proto descriptor (id) extensions. */
/* Do not edit; use scripts/generate_native_api_protocol.py. */
#include "native_api_messages.h"

#include <stddef.h>

const char *native_api_message_name(uint32_t message_id)
{
    switch (message_id) {
    case NATIVE_API_MESSAGE_HELLO_REQUEST:
        return "HelloRequest";
    case NATIVE_API_MESSAGE_HELLO_RESPONSE:
        return "HelloResponse";
    case NATIVE_API_MESSAGE_DISCONNECT_REQUEST:
        return "DisconnectRequest";
    case NATIVE_API_MESSAGE_DISCONNECT_RESPONSE:
        return "DisconnectResponse";
    case NATIVE_API_MESSAGE_PING_REQUEST:
        return "PingRequest";
    case NATIVE_API_MESSAGE_PING_RESPONSE:
        return "PingResponse";
    case NATIVE_API_MESSAGE_DEVICE_INFO_REQUEST:
        return "DeviceInfoRequest";
    case NATIVE_API_MESSAGE_DEVICE_INFO_RESPONSE:
        return "DeviceInfoResponse";
    case NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_REQUEST:
        return "DeviceCapabilitiesRequest";
    case NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_RESPONSE:
        return "DeviceCapabilitiesResponse";
    case NATIVE_API_MESSAGE_LIST_ENTITIES_REQUEST:
        return "ListEntitiesRequest";
    case NATIVE_API_MESSAGE_LIST_ENTITIES_DONE_RESPONSE:
        return "ListEntitiesDoneResponse";
    case NATIVE_API_MESSAGE_SUBSCRIBE_STATES_REQUEST:
        return "SubscribeStatesRequest";
    case NATIVE_API_MESSAGE_LIST_ENTITIES_SENSOR_RESPONSE:
        return "ListEntitiesSensorResponse";
    case NATIVE_API_MESSAGE_SENSOR_STATE_RESPONSE:
        return "SensorStateResponse";
    case NATIVE_API_MESSAGE_LIST_ENTITIES_SWITCH_RESPONSE:
        return "ListEntitiesSwitchResponse";
    case NATIVE_API_MESSAGE_SWITCH_STATE_RESPONSE:
        return "SwitchStateResponse";
    case NATIVE_API_MESSAGE_SWITCH_COMMAND_REQUEST:
        return "SwitchCommandRequest";
    case NATIVE_API_MESSAGE_LIST_ENTITIES_NUMBER_RESPONSE:
        return "ListEntitiesNumberResponse";
    case NATIVE_API_MESSAGE_NUMBER_STATE_RESPONSE:
        return "NumberStateResponse";
    case NATIVE_API_MESSAGE_NUMBER_COMMAND_REQUEST:
        return "NumberCommandRequest";
    default:
        return NULL;
    }
}
