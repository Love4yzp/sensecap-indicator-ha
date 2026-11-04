#!/usr/bin/env python3
"""Host wire-format tests for the Native Noise transport without external packages."""

from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NOISE_HEADER_STUB = r"""
#ifndef NOISE_PROTOCOL_H
#define NOISE_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
typedef struct NoiseHandshakeState_s { int unused; } NoiseHandshakeState;
typedef struct NoiseCipherState_s { int unused; } NoiseCipherState;
typedef struct { uint8_t *data; size_t size; size_t max_size; } NoiseBuffer;
typedef struct {
    int prefix_id;
    int pattern_id;
    int modifier_ids[1];
    int dh_id;
    int cipher_id;
    int hash_id;
    int hybrid_id;
    int reserved[1];
} NoiseProtocolId;
#define NOISE_PREFIX_STANDARD 1
#define NOISE_PATTERN_NN 2
#define NOISE_MODIFIER_PSK0 3
#define NOISE_DH_CURVE25519 4
#define NOISE_CIPHER_CHACHAPOLY 5
#define NOISE_HASH_SHA256 6
#define NOISE_DH_NONE 0
#define NOISE_ROLE_RESPONDER 7
#define NOISE_ACTION_READ_MESSAGE 10
#define NOISE_ACTION_WRITE_MESSAGE 11
#define NOISE_ACTION_SPLIT 12
#define NOISE_ERROR_MAC_FAILURE 13
#define noise_buffer_init(buffer) ((buffer).data = 0, (buffer).size = 0, (buffer).max_size = 0)
#define noise_buffer_set_output(buffer, ptr, len) ((buffer).data = (ptr), (buffer).size = 0, (buffer).max_size = (len))
#define noise_buffer_set_input(buffer, ptr, len) ((buffer).data = (ptr), (buffer).size = (buffer).max_size = (len))
#define noise_buffer_set_inout(buffer, ptr, len, max) ((buffer).data = (ptr), (buffer).size = (len), (buffer).max_size = (max))
int noise_handshakestate_new_by_id(NoiseHandshakeState **, const NoiseProtocolId *, int);
int noise_handshakestate_free(NoiseHandshakeState *);
int noise_handshakestate_set_pre_shared_key(NoiseHandshakeState *, const uint8_t *, size_t);
int noise_handshakestate_set_prologue(NoiseHandshakeState *, const void *, size_t);
int noise_handshakestate_start(NoiseHandshakeState *);
int noise_handshakestate_get_action(const NoiseHandshakeState *);
int noise_handshakestate_write_message(NoiseHandshakeState *, NoiseBuffer *, const NoiseBuffer *);
int noise_handshakestate_read_message(NoiseHandshakeState *, NoiseBuffer *, NoiseBuffer *);
int noise_handshakestate_split(NoiseHandshakeState *, NoiseCipherState **, NoiseCipherState **);
int noise_cipherstate_free(NoiseCipherState *);
int noise_cipherstate_decrypt(NoiseCipherState *, NoiseBuffer *);
int noise_cipherstate_encrypt(NoiseCipherState *, NoiseBuffer *);
#endif
"""

HARNESS = r"""
#include "native_api_noise.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t seen_psk[NATIVE_CONFIG_PSK_SIZE];
static uint8_t seen_prologue[160];
static size_t seen_prologue_size;
static int handshake_allocations;
static int handshake_frees;

void esp_fill_random(void *output, size_t length) { memset(output, 0xa5, length); }
int noise_handshakestate_new_by_id(NoiseHandshakeState **state, const NoiseProtocolId *protocol, int role)
{
    assert(protocol->pattern_id == NOISE_PATTERN_NN);
    assert(protocol->modifier_ids[0] == NOISE_MODIFIER_PSK0);
    assert(protocol->dh_id == NOISE_DH_CURVE25519);
    assert(protocol->cipher_id == NOISE_CIPHER_CHACHAPOLY);
    assert(protocol->hash_id == NOISE_HASH_SHA256);
    assert(role == NOISE_ROLE_RESPONDER);
    *state = calloc(1, sizeof(**state));
    assert(*state != NULL);
    handshake_allocations++;
    return 0;
}
int noise_handshakestate_free(NoiseHandshakeState *state)
{
    free(state);
    handshake_frees++;
    return 0;
}
int noise_handshakestate_set_pre_shared_key(NoiseHandshakeState *state, const uint8_t *key, size_t len)
{
    (void)state;
    assert(len == NATIVE_CONFIG_PSK_SIZE);
    memcpy(seen_psk, key, len);
    return 0;
}
int noise_handshakestate_set_prologue(NoiseHandshakeState *state, const void *prologue, size_t len)
{
    (void)state;
    assert(len <= sizeof(seen_prologue));
    memcpy(seen_prologue, prologue, len);
    seen_prologue_size = len;
    return 0;
}
int noise_handshakestate_start(NoiseHandshakeState *state) { (void)state; return 0; }
int noise_handshakestate_get_action(const NoiseHandshakeState *state) { (void)state; return NOISE_ACTION_READ_MESSAGE; }
int noise_handshakestate_write_message(NoiseHandshakeState *state, NoiseBuffer *message, const NoiseBuffer *payload)
{ (void)state; (void)message; (void)payload; return -1; }
int noise_handshakestate_read_message(NoiseHandshakeState *state, NoiseBuffer *message, NoiseBuffer *payload)
{ (void)state; (void)message; (void)payload; return -1; }
int noise_handshakestate_split(NoiseHandshakeState *state, NoiseCipherState **send, NoiseCipherState **receive)
{ (void)state; (void)send; (void)receive; return -1; }
int noise_cipherstate_free(NoiseCipherState *state) { free(state); return 0; }
int noise_cipherstate_decrypt(NoiseCipherState *state, NoiseBuffer *buffer) { (void)state; (void)buffer; return -1; }
int noise_cipherstate_encrypt(NoiseCipherState *state, NoiseBuffer *buffer) { (void)state; (void)buffer; return -1; }

typedef struct { size_t calls; size_t sizes[4]; uint8_t payload[4][8]; } capture_t;
static void capture(uint8_t *frame, size_t size, void *context)
{
    capture_t *capture = context;
    assert(capture->calls < 4);
    capture->sizes[capture->calls] = size;
    assert(size <= sizeof(capture->payload[0]));
    memcpy(capture->payload[capture->calls], frame, size);
    capture->calls++;
}

static void init_session(native_api_noise_session_t *session, uint8_t *storage)
{
    native_api_noise_session_init(session, storage, NATIVE_API_NOISE_RX_BUFFER_SIZE);
}

static void test_outer_frame_every_split_and_sticky(void)
{
    const uint8_t client_hello[] = {0x01, 0x00, 0x03, 0xaa, 0xbb, 0xcc};
    for (size_t split = 0; split <= sizeof(client_hello); split++) {
        uint8_t storage[NATIVE_API_NOISE_RX_BUFFER_SIZE];
        native_api_noise_session_t session;
        init_session(&session, storage);
        capture_t captured = {0};
        assert(native_api_noise_feed(&session, client_hello, split, capture, &captured) == NATIVE_API_NOISE_OK);
        assert(native_api_noise_feed(&session, client_hello + split, sizeof(client_hello) - split, capture, &captured) == NATIVE_API_NOISE_OK);
        assert(captured.calls == 1 && captured.sizes[0] == 3);
        assert(memcmp(captured.payload[0], "\xaa\xbb\xcc", 3) == 0);
        native_api_noise_session_cleanup(&session);
    }
    uint8_t storage[NATIVE_API_NOISE_RX_BUFFER_SIZE];
    native_api_noise_session_t session;
    init_session(&session, storage);
    capture_t captured = {0};
    const uint8_t sticky[] = {0x01, 0x00, 0x00, 0x01, 0x00, 0x00};
    assert(native_api_noise_feed(&session, sticky, sizeof(sticky), capture, &captured) == NATIVE_API_NOISE_OK);
    assert(captured.calls == 2 && captured.sizes[0] == 0 && captured.sizes[1] == 0);
    native_api_noise_session_cleanup(&session);
}

static void test_invalid_outer_frames(void)
{
    uint8_t storage[NATIVE_API_NOISE_RX_BUFFER_SIZE];
    native_api_noise_session_t session;
    init_session(&session, storage);
    const uint8_t plaintext[] = {0x00};
    assert(native_api_noise_feed(&session, plaintext, 1, NULL, NULL) == NATIVE_API_NOISE_INVALID_INDICATOR);
    assert(native_api_noise_feed(&session, NULL, 0, NULL, NULL) == NATIVE_API_NOISE_INVALID_INDICATOR);
    native_api_noise_session_cleanup(&session);
    init_session(&session, storage);
    const uint8_t oversized[] = {0x01, 0x00, 0x81};
    assert(native_api_noise_feed(&session, oversized, sizeof(oversized), NULL, NULL) == NATIVE_API_NOISE_FRAME_TOO_LARGE);
    native_api_noise_session_cleanup(&session);
}

static void test_server_hello_and_prologue(void)
{
    assert(strcmp(NATIVE_API_NOISE_PROTOCOL_NAME,
                  "Noise_" "NNpsk0_25519_ChaChaPoly_SHA256") == 0);
    uint8_t storage[NATIVE_API_NOISE_RX_BUFFER_SIZE];
    native_api_noise_session_t session;
    init_session(&session, storage);
    uint8_t psk[NATIVE_CONFIG_PSK_SIZE];
    for (size_t i = 0; i < sizeof(psk); i++) psk[i] = (uint8_t)i;
    const char *name = "sensecap-indicator-334455";
    const char *mac = "021122334455";
    uint8_t response[NATIVE_API_NOISE_MAX_HANDSHAKE_FRAME_SIZE];
    size_t response_size = 0;
    native_api_frame_t message;
    bool message_available = true;
    assert(native_api_noise_process_frame(&session, NULL, 0, name, mac, psk,
                                          response, sizeof(response), &response_size,
                                          &message, &message_available) == NATIVE_API_NOISE_OK);
    assert(!message_available);
    size_t expected_size = 1 + strlen(name) + 1 + strlen(mac) + 1;
    assert(response_size == expected_size + 3);
    assert(response[0] == 0x01 && response[1] == 0 && response[2] == expected_size);
    assert(response[3] == 0x01);
    assert(strcmp((char *)response + 4, name) == 0);
    assert(strcmp((char *)response + 4 + strlen(name) + 1, mac) == 0);
    const uint8_t expected_prologue[] = "NoiseAPIInit\0\0";
    assert(seen_prologue_size == 14);
    assert(memcmp(seen_prologue, expected_prologue, 14) == 0);
    assert(memcmp(seen_psk, psk, sizeof(psk)) == 0);
    assert(session.state == NATIVE_API_NOISE_STATE_HANDSHAKE);
    native_api_noise_session_cleanup(&session);
    assert(handshake_allocations == 1 && handshake_frees == 1);
}

static void test_reject_wire_contract(void)
{
    uint8_t storage[NATIVE_API_NOISE_RX_BUFFER_SIZE];
    native_api_noise_session_t session;
    init_session(&session, storage);
    uint8_t response[32];
    size_t response_size = 0;
    assert(native_api_noise_reject(&session, "Handshake MAC failure", response,
                                   sizeof(response), &response_size) == NATIVE_API_NOISE_OK);
    const char *reason = "Handshake MAC failure";
    size_t reason_size = strlen(reason);
    assert(response_size == reason_size + 4);
    assert(response[0] == 0x01 && response[1] == 0 && response[2] == reason_size + 1);
    assert(response[3] == 0x01);
    assert(memcmp(response + 4, reason, reason_size) == 0);
    assert(session.state == NATIVE_API_NOISE_STATE_FAILED);
    native_api_noise_session_cleanup(&session);
}

int main(void)
{
    test_outer_frame_every_split_and_sticky();
    test_invalid_outer_frames();
    test_server_hello_and_prologue();
    test_reject_wire_contract();
    puts("native_api_noise C wire format: OK");
    return 0;
}
"""


class NativeNoiseTests(unittest.TestCase):
    def test_wire_format_and_handshake_setup(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / "noise").mkdir()
            (tmp / "noise" / "protocol.h").write_text(NOISE_HEADER_STUB, encoding="utf-8")
            (tmp / "esp_err.h").write_text("typedef int esp_err_t;\n", encoding="utf-8")
            (tmp / "esp_random.h").write_text(
                "#include <stddef.h>\nvoid esp_fill_random(void *, size_t);\n", encoding="utf-8"
            )
            harness = tmp / "native_noise_test.c"
            binary = tmp / "native_noise_test"
            harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-I", str(tmp), "-I", str(ROOT / "main" / "ha"),
                    str(ROOT / "main" / "ha" / "native_api_noise.c"), str(harness),
                    "-o", str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
