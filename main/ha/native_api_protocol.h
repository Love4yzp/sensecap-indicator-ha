#ifndef NATIVE_API_PROTOCOL_H
#define NATIVE_API_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Plaintext ESPHome API framing only; Noise records are not supported. */
#define NATIVE_API_PLAINTEXT_PREAMBLE 0x00u
#define NATIVE_API_DEFAULT_MAX_PAYLOAD_SIZE 2048u
#define NATIVE_API_VARUINT32_MAX_SIZE 5u

typedef enum {
    NATIVE_API_STATUS_OK = 0,
    NATIVE_API_STATUS_INVALID_ARGUMENT,
    NATIVE_API_STATUS_OUTPUT_TOO_SMALL,
    NATIVE_API_STATUS_FRAME_TOO_LARGE,
    NATIVE_API_STATUS_INVALID_PREAMBLE,
    NATIVE_API_STATUS_INVALID_VARINT,
} native_api_status_t;

typedef struct {
    uint32_t message_type;
    const uint8_t *payload;
    size_t payload_length;
} native_api_frame_t;

typedef void (*native_api_frame_callback_t)(const native_api_frame_t *frame, void *context);

typedef struct {
    uint8_t *payload_buffer;
    size_t payload_capacity;
    uint32_t max_payload_size;
    uint32_t payload_size;
    uint32_t message_type;
    size_t payload_offset;
    uint32_t varint_value;
    uint8_t varint_size;
    uint8_t state;
    native_api_status_t status;
} native_api_parser_t;

native_api_status_t native_api_parser_init(native_api_parser_t *parser,
                                           uint8_t *payload_buffer,
                                           size_t payload_capacity,
                                           uint32_t max_payload_size);
void native_api_parser_reset(native_api_parser_t *parser);
native_api_status_t native_api_parser_feed(native_api_parser_t *parser,
                                           const uint8_t *data,
                                           size_t length,
                                           native_api_frame_callback_t callback,
                                           void *context);

native_api_status_t native_api_encoded_size(uint32_t message_type,
                                            size_t payload_length,
                                            uint32_t max_payload_size,
                                            size_t *frame_size);
native_api_status_t native_api_encode_frame(uint32_t message_type,
                                            const uint8_t *payload,
                                            size_t payload_length,
                                            uint32_t max_payload_size,
                                            uint8_t *output,
                                            size_t output_capacity,
                                            size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
