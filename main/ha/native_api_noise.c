#include "native_api_noise.h"

#include "native_config.h"

#include "esp_random.h"

#include <string.h>

#define NOISE_API_PROLOGUE "NoiseAPIInit"
#define NOISE_API_PROLOGUE_SIZE 12u
#define NOISE_API_HANDSHAKE_STATUS_OK 0x00u
#define NOISE_API_HANDSHAKE_STATUS_REJECT 0x01u
#define NOISE_API_SELECTED_PROTOCOL 0x01u

void noise_rand_bytes(void *output, size_t length)
{
    esp_fill_random(output, length);
}

static native_api_noise_status_t fail(native_api_noise_session_t *session,
                                      native_api_noise_status_t status)
{
    session->state = NATIVE_API_NOISE_STATE_FAILED;
    session->status = status;
    return status;
}

static native_api_noise_status_t write_frame(uint8_t *output,
                                             size_t output_capacity,
                                             const uint8_t *payload,
                                             size_t payload_size,
                                             size_t *output_size)
{
    if (output_size == NULL || (payload_size > 0 && payload == NULL)) {
        return NATIVE_API_NOISE_INVALID_ARGUMENT;
    }
    *output_size = 0;
    if (payload_size > UINT16_MAX ||
        payload_size > SIZE_MAX - NATIVE_API_NOISE_FRAME_HEADER_SIZE) {
        return NATIVE_API_NOISE_FRAME_TOO_LARGE;
    }
    size_t total = payload_size + NATIVE_API_NOISE_FRAME_HEADER_SIZE;
    if (output == NULL || output_capacity < total) {
        return NATIVE_API_NOISE_OUTPUT_TOO_SMALL;
    }
    output[0] = NATIVE_API_NOISE_FRAME_INDICATOR;
    output[1] = (uint8_t)(payload_size >> 8);
    output[2] = (uint8_t)payload_size;
    if (payload_size > 0) {
        memcpy(output + NATIVE_API_NOISE_FRAME_HEADER_SIZE, payload, payload_size);
    }
    *output_size = total;
    return NATIVE_API_NOISE_OK;
}

void native_api_noise_session_init(native_api_noise_session_t *session,
                                   uint8_t *frame_buffer,
                                   size_t frame_capacity)
{
    memset(session, 0, sizeof(*session));
    session->state = NATIVE_API_NOISE_STATE_CLIENT_HELLO;
    session->frame_buffer = frame_buffer;
    session->frame_capacity = frame_capacity;
}

void native_api_noise_session_cleanup(native_api_noise_session_t *session)
{
    if (session == NULL) {
        return;
    }
    if (session->handshake != NULL) {
        noise_handshakestate_free(session->handshake);
    }
    if (session->send_cipher != NULL) {
        noise_cipherstate_free(session->send_cipher);
    }
    if (session->recv_cipher != NULL) {
        noise_cipherstate_free(session->recv_cipher);
    }
    memset(session, 0, sizeof(*session));
}

bool native_api_noise_is_ready(const native_api_noise_session_t *session)
{
    return session != NULL && session->state == NATIVE_API_NOISE_STATE_DATA &&
           session->send_cipher != NULL && session->recv_cipher != NULL;
}

native_api_noise_status_t native_api_noise_feed(native_api_noise_session_t *session,
                                                const uint8_t *data,
                                                size_t length,
                                                native_api_noise_frame_callback_t callback,
                                                void *context)
{
    if (session == NULL || (length > 0 && data == NULL)) {
        return NATIVE_API_NOISE_INVALID_ARGUMENT;
    }
    if (session->status != NATIVE_API_NOISE_OK) {
        return session->status;
    }
    while (length > 0) {
        if (session->header_offset < NATIVE_API_NOISE_FRAME_HEADER_SIZE) {
            uint8_t byte = *data++;
            length--;
            session->header[session->header_offset++] = byte;
            if (session->header_offset == 1 &&
                byte != NATIVE_API_NOISE_FRAME_INDICATOR) {
                return fail(session, NATIVE_API_NOISE_INVALID_INDICATOR);
            }
            if (session->header_offset == NATIVE_API_NOISE_FRAME_HEADER_SIZE) {
                session->frame_size = ((uint16_t)session->header[1] << 8) |
                                      session->header[2];
                size_t limit = session->state == NATIVE_API_NOISE_STATE_DATA
                                   ? NATIVE_API_DEFAULT_MAX_PAYLOAD_SIZE +
                                         NATIVE_API_NOISE_DATA_HEADER_SIZE +
                                         NATIVE_API_NOISE_MAC_SIZE
                                   : NATIVE_API_NOISE_MAX_HANDSHAKE_PAYLOAD;
                if (session->frame_size > limit ||
                    session->frame_size > session->frame_capacity) {
                    return fail(session, NATIVE_API_NOISE_FRAME_TOO_LARGE);
                }
                session->frame_offset = 0;
                if (session->frame_size == 0) {
                    if (callback != NULL) {
                        callback(session->frame_buffer, 0, context);
                    }
                    session->header_offset = 0;
                    if (session->status != NATIVE_API_NOISE_OK) {
                        return session->status;
                    }
                }
            }
            continue;
        }

        size_t remaining = session->frame_size - session->frame_offset;
        size_t available = remaining < length ? remaining : length;
        if (available > 0) {
            memcpy(session->frame_buffer + session->frame_offset, data, available);
            session->frame_offset += available;
            data += available;
            length -= available;
        }
        if (session->frame_offset == session->frame_size) {
            if (callback != NULL) {
                callback(session->frame_buffer, session->frame_size, context);
            }
            session->header_offset = 0;
            session->frame_offset = 0;
            session->frame_size = 0;
            if (session->status != NATIVE_API_NOISE_OK) {
                return session->status;
            }
        }
    }
    return NATIVE_API_NOISE_OK;
}

static native_api_noise_status_t format_reject(const char *reason,
                                               uint8_t *response,
                                               size_t response_capacity,
                                               size_t *response_size)
{
    uint8_t payload[32];
    size_t reason_size = strlen(reason);
    if (reason_size > sizeof(payload) - 1) {
        reason_size = sizeof(payload) - 1;
    }
    payload[0] = NOISE_API_HANDSHAKE_STATUS_REJECT;
    memcpy(payload + 1, reason, reason_size);
    return write_frame(response, response_capacity, payload, reason_size + 1,
                       response_size);
}

native_api_noise_status_t native_api_noise_reject(
    native_api_noise_session_t *session, const char *reason,
    uint8_t *response, size_t response_capacity, size_t *response_size)
{
    if (session == NULL || reason == NULL) {
        return NATIVE_API_NOISE_INVALID_ARGUMENT;
    }
    native_api_noise_status_t status = format_reject(
        reason, response, response_capacity, response_size);
    if (status == NATIVE_API_NOISE_OK) {
        session->state = NATIVE_API_NOISE_STATE_FAILED;
    }
    return status;
}

static native_api_noise_status_t start_handshake(
    native_api_noise_session_t *session, const uint8_t *client_hello,
    size_t client_hello_size, const uint8_t *psk)
{
    uint8_t prologue[NOISE_API_PROLOGUE_SIZE + 2 +
                     NATIVE_API_NOISE_MAX_HANDSHAKE_PAYLOAD];
    memcpy(prologue, NOISE_API_PROLOGUE, NOISE_API_PROLOGUE_SIZE);
    prologue[NOISE_API_PROLOGUE_SIZE] = (uint8_t)(client_hello_size >> 8);
    prologue[NOISE_API_PROLOGUE_SIZE + 1] = (uint8_t)client_hello_size;
    size_t prologue_size = NOISE_API_PROLOGUE_SIZE + 2 + client_hello_size;
    if (client_hello_size > 0) {
        memcpy(prologue + NOISE_API_PROLOGUE_SIZE + 2, client_hello,
               client_hello_size);
    }

    const NoiseProtocolId protocol = {
        .prefix_id = NOISE_PREFIX_STANDARD,
        .pattern_id = NOISE_PATTERN_NN,
        .modifier_ids = {NOISE_MODIFIER_PSK0},
        .dh_id = NOISE_DH_CURVE25519,
        .cipher_id = NOISE_CIPHER_CHACHAPOLY,
        .hash_id = NOISE_HASH_SHA256,
        .hybrid_id = NOISE_DH_NONE,
        .reserved = {0},
    };
    int err = noise_handshakestate_new_by_id(&session->handshake, &protocol,
                                             NOISE_ROLE_RESPONDER);
    if (err == 0) {
        err = noise_handshakestate_set_pre_shared_key(session->handshake, psk,
                                                      NATIVE_CONFIG_PSK_SIZE);
    }
    if (err == 0) {
        err = noise_handshakestate_set_prologue(session->handshake, prologue,
                                                prologue_size);
    }
    if (err == 0) {
        err = noise_handshakestate_start(session->handshake);
    }
    memset(prologue, 0, sizeof(prologue));
    if (err != 0) {
        if (session->handshake != NULL) {
            noise_handshakestate_free(session->handshake);
            session->handshake = NULL;
        }
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    session->state = NATIVE_API_NOISE_STATE_HANDSHAKE;
    return NATIVE_API_NOISE_OK;
}

static native_api_noise_status_t finish_handshake(native_api_noise_session_t *session)
{
    int action = noise_handshakestate_get_action(session->handshake);
    if (action == NOISE_ACTION_READ_MESSAGE || action == NOISE_ACTION_WRITE_MESSAGE) {
        return NATIVE_API_NOISE_OK;
    }
    if (action != NOISE_ACTION_SPLIT) {
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    int err = noise_handshakestate_split(session->handshake, &session->send_cipher,
                                         &session->recv_cipher);
    noise_handshakestate_free(session->handshake);
    session->handshake = NULL;
    if (err != 0) {
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    session->state = NATIVE_API_NOISE_STATE_DATA;
    return NATIVE_API_NOISE_OK;
}

static native_api_noise_status_t process_handshake(
    native_api_noise_session_t *session, uint8_t *frame, size_t frame_size,
    uint8_t *response, size_t response_capacity, size_t *response_size)
{
    if (frame_size == 0 || frame[0] != NOISE_API_HANDSHAKE_STATUS_OK) {
        native_api_noise_reject(session, "Bad handshake error byte", response,
                                response_capacity, response_size);
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    NoiseBuffer input;
    noise_buffer_init(input);
    noise_buffer_set_input(input, frame + 1, frame_size - 1);
    int err = noise_handshakestate_read_message(session->handshake, &input, NULL);
    if (err != 0) {
        const char *reason = err == NOISE_ERROR_MAC_FAILURE
                                 ? "Handshake MAC failure"
                                 : "Handshake error";
        native_api_noise_reject(session, reason, response, response_capacity,
                                response_size);
        return fail(session, err == NOISE_ERROR_MAC_FAILURE
                                 ? NATIVE_API_NOISE_AUTHENTICATION_FAILED
                                 : NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }

    int action = noise_handshakestate_get_action(session->handshake);
    if (action == NOISE_ACTION_WRITE_MESSAGE) {
        if (response_capacity < NATIVE_API_NOISE_FRAME_HEADER_SIZE + 1) {
            return fail(session, NATIVE_API_NOISE_OUTPUT_TOO_SMALL);
        }
        NoiseBuffer output;
        noise_buffer_init(output);
        noise_buffer_set_output(output,
                                response + NATIVE_API_NOISE_FRAME_HEADER_SIZE + 1,
                                response_capacity - NATIVE_API_NOISE_FRAME_HEADER_SIZE - 1);
        err = noise_handshakestate_write_message(session->handshake, &output, NULL);
        if (err != 0) {
            return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
        }
        response[NATIVE_API_NOISE_FRAME_HEADER_SIZE] = NOISE_API_HANDSHAKE_STATUS_OK;
        size_t payload_size = output.size + 1;
        response[0] = NATIVE_API_NOISE_FRAME_INDICATOR;
        response[1] = (uint8_t)(payload_size >> 8);
        response[2] = (uint8_t)payload_size;
        *response_size = payload_size + NATIVE_API_NOISE_FRAME_HEADER_SIZE;
    } else if (action != NOISE_ACTION_READ_MESSAGE && action != NOISE_ACTION_SPLIT) {
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    return finish_handshake(session);
}

static native_api_noise_status_t process_data(native_api_noise_session_t *session,
                                              uint8_t *frame, size_t frame_size,
                                              native_api_frame_t *message,
                                              bool *message_available)
{
    NoiseBuffer buffer;
    noise_buffer_init(buffer);
    noise_buffer_set_inout(buffer, frame, frame_size, frame_size);
    int err = noise_cipherstate_decrypt(session->recv_cipher, &buffer);
    if (err != 0) {
        return fail(session, err == NOISE_ERROR_MAC_FAILURE
                                 ? NATIVE_API_NOISE_AUTHENTICATION_FAILED
                                 : NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    if (buffer.size < NATIVE_API_NOISE_DATA_HEADER_SIZE) {
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    uint32_t message_type = ((uint32_t)frame[0] << 8) | frame[1];
    size_t payload_size = ((size_t)frame[2] << 8) | frame[3];
    if (payload_size > buffer.size - NATIVE_API_NOISE_DATA_HEADER_SIZE ||
        payload_size > NATIVE_API_DEFAULT_MAX_PAYLOAD_SIZE) {
        return fail(session, NATIVE_API_NOISE_FRAME_TOO_LARGE);
    }
    message->message_type = message_type;
    message->payload = frame + NATIVE_API_NOISE_DATA_HEADER_SIZE;
    message->payload_length = payload_size;
    *message_available = true;
    return NATIVE_API_NOISE_OK;
}

native_api_noise_status_t native_api_noise_process_frame(
    native_api_noise_session_t *session, uint8_t *frame, size_t frame_size,
    const char *name, const char *mac_address,
    const uint8_t psk[NATIVE_CONFIG_PSK_SIZE],
    uint8_t *response, size_t response_capacity, size_t *response_size,
    native_api_frame_t *message, bool *message_available)
{
    if (session == NULL || (frame_size > 0 && frame == NULL) ||
        response_size == NULL || message == NULL || message_available == NULL) {
        return NATIVE_API_NOISE_INVALID_ARGUMENT;
    }
    *response_size = 0;
    *message_available = false;
    if (session->state == NATIVE_API_NOISE_STATE_DATA) {
        return process_data(session, frame, frame_size, message, message_available);
    }
    if (session->state == NATIVE_API_NOISE_STATE_CLIENT_HELLO) {
        if (frame_size > NATIVE_API_NOISE_MAX_HANDSHAKE_PAYLOAD) {
            return fail(session, NATIVE_API_NOISE_FRAME_TOO_LARGE);
        }
        if (name == NULL || mac_address == NULL || psk == NULL) {
            return fail(session, NATIVE_API_NOISE_INVALID_ARGUMENT);
        }
        size_t name_size = strlen(name) + 1;
        size_t mac_size = strlen(mac_address) + 1;
        size_t hello_size = 1 + name_size + mac_size;
        if (hello_size > NATIVE_API_NOISE_MAX_HANDSHAKE_PAYLOAD) {
            return fail(session, NATIVE_API_NOISE_OUTPUT_TOO_SMALL);
        }
        uint8_t hello[NATIVE_API_NOISE_MAX_HANDSHAKE_PAYLOAD];
        hello[0] = NOISE_API_SELECTED_PROTOCOL;
        memcpy(hello + 1, name, name_size);
        memcpy(hello + 1 + name_size, mac_address, mac_size);
        native_api_noise_status_t status = write_frame(
            response, response_capacity, hello, hello_size, response_size);
        if (status != NATIVE_API_NOISE_OK) {
            return fail(session, status);
        }
        return start_handshake(session, frame, frame_size, psk);
    }
    if (session->state == NATIVE_API_NOISE_STATE_HANDSHAKE) {
        return process_handshake(session, frame, frame_size, response,
                                 response_capacity, response_size);
    }
    return NATIVE_API_NOISE_NOT_READY;
}

native_api_noise_status_t native_api_noise_encode_message(
    native_api_noise_session_t *session, uint32_t message_type,
    const uint8_t *payload, size_t payload_size,
    uint8_t *output, size_t output_capacity, size_t *output_size)
{
    if (session == NULL || output_size == NULL ||
        (payload_size > 0 && payload == NULL)) {
        return NATIVE_API_NOISE_INVALID_ARGUMENT;
    }
    *output_size = 0;
    if (!native_api_noise_is_ready(session) || message_type > UINT16_MAX) {
        return NATIVE_API_NOISE_NOT_READY;
    }
    if (payload_size > NATIVE_API_DEFAULT_MAX_PAYLOAD_SIZE ||
        payload_size > UINT16_MAX) {
        return NATIVE_API_NOISE_FRAME_TOO_LARGE;
    }
    size_t plaintext_size = NATIVE_API_NOISE_DATA_HEADER_SIZE + payload_size;
    size_t encrypted_size = plaintext_size + NATIVE_API_NOISE_MAC_SIZE;
    if (output == NULL || encrypted_size > SIZE_MAX - NATIVE_API_NOISE_FRAME_HEADER_SIZE ||
        output_capacity < encrypted_size + NATIVE_API_NOISE_FRAME_HEADER_SIZE) {
        return NATIVE_API_NOISE_OUTPUT_TOO_SMALL;
    }
    output[NATIVE_API_NOISE_FRAME_HEADER_SIZE] = (uint8_t)(message_type >> 8);
    output[NATIVE_API_NOISE_FRAME_HEADER_SIZE + 1] = (uint8_t)message_type;
    output[NATIVE_API_NOISE_FRAME_HEADER_SIZE + 2] = (uint8_t)(payload_size >> 8);
    output[NATIVE_API_NOISE_FRAME_HEADER_SIZE + 3] = (uint8_t)payload_size;
    if (payload_size > 0) {
        memcpy(output + NATIVE_API_NOISE_FRAME_HEADER_SIZE +
                   NATIVE_API_NOISE_DATA_HEADER_SIZE,
               payload, payload_size);
    }
    NoiseBuffer buffer;
    noise_buffer_init(buffer);
    noise_buffer_set_inout(buffer, output + NATIVE_API_NOISE_FRAME_HEADER_SIZE,
                           plaintext_size, output_capacity - NATIVE_API_NOISE_FRAME_HEADER_SIZE);
    int err = noise_cipherstate_encrypt(session->send_cipher, &buffer);
    if (err != 0) {
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    if (buffer.size != encrypted_size) {
        return fail(session, NATIVE_API_NOISE_HANDSHAKE_ERROR);
    }
    output[0] = NATIVE_API_NOISE_FRAME_INDICATOR;
    output[1] = (uint8_t)(buffer.size >> 8);
    output[2] = (uint8_t)buffer.size;
    *output_size = buffer.size + NATIVE_API_NOISE_FRAME_HEADER_SIZE;
    return NATIVE_API_NOISE_OK;
}
