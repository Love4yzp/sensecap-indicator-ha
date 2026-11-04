#include "native_api_protocol.h"

#include <string.h>

#define PARSER_STATE_PREAMBLE 0u
#define PARSER_STATE_LENGTH 1u
#define PARSER_STATE_TYPE 2u
#define PARSER_STATE_PAYLOAD 3u

static size_t varuint32_size(uint32_t value)
{
    size_t size = 1;
    while (value >= 0x80u) {
        value >>= 7;
        size++;
    }
    return size;
}

static size_t encode_varuint32(uint32_t value, uint8_t *output)
{
    size_t size = 0;
    while (value >= 0x80u) {
        output[size++] = (uint8_t)(value & 0x7Fu) | 0x80u;
        value >>= 7;
    }
    output[size++] = (uint8_t)value;
    return size;
}

static native_api_status_t consume_varuint32(native_api_parser_t *parser,
                                              uint8_t byte,
                                              int *complete)
{
    uint8_t index = parser->varint_size;
    if (index >= NATIVE_API_VARUINT32_MAX_SIZE ||
        (index == NATIVE_API_VARUINT32_MAX_SIZE - 1u && (byte & 0xF0u) != 0)) {
        return NATIVE_API_STATUS_INVALID_VARINT;
    }

    parser->varint_value |= (uint32_t)(byte & 0x7Fu) << (index * 7u);
    parser->varint_size++;
    if ((byte & 0x80u) != 0 && parser->varint_size == NATIVE_API_VARUINT32_MAX_SIZE) {
        return NATIVE_API_STATUS_INVALID_VARINT;
    }
    *complete = (byte & 0x80u) == 0;
    return NATIVE_API_STATUS_OK;
}

static void start_varuint(native_api_parser_t *parser, uint8_t state)
{
    parser->varint_value = 0;
    parser->varint_size = 0;
    parser->state = state;
}

static native_api_status_t fail(native_api_parser_t *parser, native_api_status_t status)
{
    parser->status = status;
    return status;
}

static void emit_frame(native_api_parser_t *parser,
                       native_api_frame_callback_t callback,
                       void *context)
{
    if (callback != NULL) {
        native_api_frame_t frame = {
            .message_type = parser->message_type,
            .payload = parser->payload_buffer,
            .payload_length = parser->payload_size,
        };
        callback(&frame, context);
    }
    parser->state = PARSER_STATE_PREAMBLE;
}

native_api_status_t native_api_parser_init(native_api_parser_t *parser,
                                           uint8_t *payload_buffer,
                                           size_t payload_capacity,
                                           uint32_t max_payload_size)
{
    if (parser == NULL || (payload_capacity > 0 && payload_buffer == NULL) ||
        max_payload_size > payload_capacity) {
        return NATIVE_API_STATUS_INVALID_ARGUMENT;
    }
    *parser = (native_api_parser_t){
        .payload_buffer = payload_buffer,
        .payload_capacity = payload_capacity,
        .max_payload_size = max_payload_size,
        .state = PARSER_STATE_PREAMBLE,
        .status = NATIVE_API_STATUS_OK,
    };
    return NATIVE_API_STATUS_OK;
}

void native_api_parser_reset(native_api_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    uint8_t *payload_buffer = parser->payload_buffer;
    size_t payload_capacity = parser->payload_capacity;
    uint32_t max_payload_size = parser->max_payload_size;
    *parser = (native_api_parser_t){
        .payload_buffer = payload_buffer,
        .payload_capacity = payload_capacity,
        .max_payload_size = max_payload_size,
        .state = PARSER_STATE_PREAMBLE,
        .status = NATIVE_API_STATUS_OK,
    };
}

native_api_status_t native_api_parser_feed(native_api_parser_t *parser,
                                           const uint8_t *data,
                                           size_t length,
                                           native_api_frame_callback_t callback,
                                           void *context)
{
    if (parser == NULL || (length > 0 && data == NULL)) {
        return NATIVE_API_STATUS_INVALID_ARGUMENT;
    }
    if (parser->status != NATIVE_API_STATUS_OK) {
        return parser->status;
    }

    while (length > 0) {
        if (parser->state == PARSER_STATE_PAYLOAD) {
            size_t remaining = parser->payload_size - parser->payload_offset;
            size_t available = remaining < length ? remaining : length;
            memcpy(parser->payload_buffer + parser->payload_offset, data, available);
            parser->payload_offset += available;
            data += available;
            length -= available;
            if (parser->payload_offset == parser->payload_size) {
                emit_frame(parser, callback, context);
            }
            continue;
        }

        uint8_t byte = *data++;
        length--;
        if (parser->state == PARSER_STATE_PREAMBLE) {
            if (byte != NATIVE_API_PLAINTEXT_PREAMBLE) {
                return fail(parser, NATIVE_API_STATUS_INVALID_PREAMBLE);
            }
            start_varuint(parser, PARSER_STATE_LENGTH);
            continue;
        }

        int complete = 0;
        native_api_status_t status = consume_varuint32(parser, byte, &complete);
        if (status != NATIVE_API_STATUS_OK) {
            return fail(parser, status);
        }
        if (!complete) {
            continue;
        }

        if (parser->state == PARSER_STATE_LENGTH) {
            parser->payload_size = parser->varint_value;
            if (parser->payload_size > parser->max_payload_size ||
                parser->payload_size > parser->payload_capacity) {
                return fail(parser, NATIVE_API_STATUS_FRAME_TOO_LARGE);
            }
            start_varuint(parser, PARSER_STATE_TYPE);
        } else if (parser->state == PARSER_STATE_TYPE) {
            parser->message_type = parser->varint_value;
            parser->payload_offset = 0;
            if (parser->payload_size == 0) {
                emit_frame(parser, callback, context);
            } else {
                parser->state = PARSER_STATE_PAYLOAD;
            }
        } else {
            return fail(parser, NATIVE_API_STATUS_INVALID_ARGUMENT);
        }
    }
    return NATIVE_API_STATUS_OK;
}

native_api_status_t native_api_encoded_size(uint32_t message_type,
                                            size_t payload_length,
                                            uint32_t max_payload_size,
                                            size_t *frame_size)
{
    if (frame_size == NULL) {
        return NATIVE_API_STATUS_INVALID_ARGUMENT;
    }
    *frame_size = 0;
    if (payload_length > UINT32_MAX || payload_length > max_payload_size) {
        return NATIVE_API_STATUS_FRAME_TOO_LARGE;
    }
    size_t header_size = 1 + varuint32_size((uint32_t)payload_length) +
                         varuint32_size(message_type);
    if (payload_length > SIZE_MAX - header_size) {
        return NATIVE_API_STATUS_FRAME_TOO_LARGE;
    }
    *frame_size = header_size + payload_length;
    return NATIVE_API_STATUS_OK;
}

native_api_status_t native_api_encode_frame(uint32_t message_type,
                                            const uint8_t *payload,
                                            size_t payload_length,
                                            uint32_t max_payload_size,
                                            uint8_t *output,
                                            size_t output_capacity,
                                            size_t *output_size)
{
    if (output_size == NULL || (payload_length > 0 && payload == NULL)) {
        return NATIVE_API_STATUS_INVALID_ARGUMENT;
    }
    native_api_status_t status = native_api_encoded_size(
        message_type, payload_length, max_payload_size, output_size);
    if (status != NATIVE_API_STATUS_OK) {
        return status;
    }
    if (output_capacity < *output_size) {
        return NATIVE_API_STATUS_OUTPUT_TOO_SMALL;
    }
    if (output == NULL) {
        return NATIVE_API_STATUS_INVALID_ARGUMENT;
    }

    size_t offset = 0;
    output[offset++] = NATIVE_API_PLAINTEXT_PREAMBLE;
    offset += encode_varuint32((uint32_t)payload_length, output + offset);
    offset += encode_varuint32(message_type, output + offset);
    if (payload_length > 0) {
        memcpy(output + offset, payload, payload_length);
    }
    return NATIVE_API_STATUS_OK;
}
