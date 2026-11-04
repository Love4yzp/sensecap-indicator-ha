#ifndef NATIVE_API_NOISE_H
#define NATIVE_API_NOISE_H

#include "native_api_protocol.h"
#include "native_config.h"

#include <noise/protocol.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_API_NOISE_PROTOCOL_NAME \
    "Noise_" "NNpsk0_25519_ChaChaPoly_SHA256"
#define NATIVE_API_NOISE_FRAME_INDICATOR 0x01u
#define NATIVE_API_NOISE_FRAME_HEADER_SIZE 3u
#define NATIVE_API_NOISE_DATA_HEADER_SIZE 4u
#define NATIVE_API_NOISE_MAC_SIZE 16u
#define NATIVE_API_NOISE_MAX_HANDSHAKE_PAYLOAD 128u
#define NATIVE_API_NOISE_MAX_HANDSHAKE_FRAME_SIZE \
    (NATIVE_API_NOISE_FRAME_HEADER_SIZE + NATIVE_API_NOISE_MAX_HANDSHAKE_PAYLOAD)
#define NATIVE_API_NOISE_RX_BUFFER_SIZE \
    (NATIVE_API_DEFAULT_MAX_PAYLOAD_SIZE + NATIVE_API_NOISE_DATA_HEADER_SIZE + \
     NATIVE_API_NOISE_MAC_SIZE)

typedef enum {
    NATIVE_API_NOISE_STATE_UNINITIALIZED = 0,
    NATIVE_API_NOISE_STATE_CLIENT_HELLO,
    NATIVE_API_NOISE_STATE_HANDSHAKE,
    NATIVE_API_NOISE_STATE_DATA,
    NATIVE_API_NOISE_STATE_FAILED,
} native_api_noise_state_t;

typedef enum {
    NATIVE_API_NOISE_OK = 0,
    NATIVE_API_NOISE_INVALID_ARGUMENT,
    NATIVE_API_NOISE_OUTPUT_TOO_SMALL,
    NATIVE_API_NOISE_FRAME_TOO_LARGE,
    NATIVE_API_NOISE_INVALID_INDICATOR,
    NATIVE_API_NOISE_HANDSHAKE_ERROR,
    NATIVE_API_NOISE_AUTHENTICATION_FAILED,
    NATIVE_API_NOISE_NOT_READY,
} native_api_noise_status_t;

typedef struct {
    native_api_noise_state_t state;
    NoiseHandshakeState *handshake;
    NoiseCipherState *send_cipher;
    NoiseCipherState *recv_cipher;
    uint8_t *frame_buffer;
    size_t frame_capacity;
    size_t frame_offset;
    uint16_t frame_size;
    uint8_t header[3];
    uint8_t header_offset;
    native_api_noise_status_t status;
} native_api_noise_session_t;

typedef void (*native_api_noise_frame_callback_t)(
    uint8_t *frame, size_t frame_size, void *context);

void native_api_noise_session_init(native_api_noise_session_t *session,
                                   uint8_t *frame_buffer,
                                   size_t frame_capacity);
void native_api_noise_session_cleanup(native_api_noise_session_t *session);
bool native_api_noise_is_ready(const native_api_noise_session_t *session);

native_api_noise_status_t native_api_noise_feed(native_api_noise_session_t *session,
                                                const uint8_t *data,
                                                size_t length,
                                                native_api_noise_frame_callback_t callback,
                                                void *context);

native_api_noise_status_t native_api_noise_process_frame(
    native_api_noise_session_t *session, uint8_t *frame, size_t frame_size,
    const char *name, const char *mac_address,
    const uint8_t psk[NATIVE_CONFIG_PSK_SIZE],
    uint8_t *response, size_t response_capacity, size_t *response_size,
    native_api_frame_t *message, bool *message_available);

native_api_noise_status_t native_api_noise_reject(
    native_api_noise_session_t *session, const char *reason,
    uint8_t *response, size_t response_capacity, size_t *response_size);

native_api_noise_status_t native_api_noise_encode_message(
    native_api_noise_session_t *session, uint32_t message_type,
    const uint8_t *payload, size_t payload_size,
    uint8_t *output, size_t output_capacity, size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_API_NOISE_H */
