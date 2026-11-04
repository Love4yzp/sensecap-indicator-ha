#!/usr/bin/env python3
"""Host tests for ESPHome Native API framing and pinned nanopb messages."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HA_DIR = ROOT / "main" / "ha"
NATIVE_API_DIR = HA_DIR / "native_api"
PROTO_DIR = NATIVE_API_DIR / "proto"

FRAME_HARNESS = r"""
#include "native_api_protocol.h"
#include "native_api/native_api_messages.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define TEST_MAX_PAYLOAD 16u

typedef struct {
    uint32_t type[8];
    size_t length[8];
    uint8_t payload[8][TEST_MAX_PAYLOAD];
    size_t count;
} capture_t;

static void capture_frame(const native_api_frame_t *frame, void *context)
{
    capture_t *capture = context;
    assert(capture->count < 8);
    size_t index = capture->count++;
    capture->type[index] = frame->message_type;
    capture->length[index] = frame->payload_length;
    assert(frame->payload_length <= TEST_MAX_PAYLOAD);
    if (frame->payload_length > 0) {
        memcpy(capture->payload[index], frame->payload, frame->payload_length);
    }
}

static size_t encode(uint32_t type, const uint8_t *payload, size_t length,
                     uint8_t *output, size_t capacity)
{
    size_t written = 0;
    assert(native_api_encode_frame(type, payload, length, TEST_MAX_PAYLOAD,
                                   output, capacity, &written) == NATIVE_API_STATUS_OK);
    return written;
}

static void init_parser(native_api_parser_t *parser, uint8_t *storage, size_t capacity,
                        uint32_t maximum)
{
    assert(native_api_parser_init(parser, storage, capacity, maximum) ==
           NATIVE_API_STATUS_OK);
}

static void test_sticky_frames_and_unknown_messages(void)
{
    uint8_t stream[128];
    uint8_t payload[] = {0x08, 0x01, 0x15, 0x00, 0x00};
    size_t offset = 0;
    offset += encode(NATIVE_API_MESSAGE_PING_REQUEST, NULL, 0,
                     stream + offset, sizeof(stream) - offset);
    offset += encode(NATIVE_API_MESSAGE_NUMBER_COMMAND_REQUEST, payload, sizeof(payload),
                     stream + offset, sizeof(stream) - offset);
    offset += encode(UINT32_MAX, NULL, 0, stream + offset, sizeof(stream) - offset);

    uint8_t storage[TEST_MAX_PAYLOAD];
    native_api_parser_t parser;
    capture_t capture = {0};
    init_parser(&parser, storage, sizeof(storage), TEST_MAX_PAYLOAD);
    assert(native_api_parser_feed(&parser, stream, offset, capture_frame, &capture) ==
           NATIVE_API_STATUS_OK);
    assert(capture.count == 3);
    assert(capture.type[0] == NATIVE_API_MESSAGE_PING_REQUEST);
    assert(capture.length[0] == 0);
    assert(capture.type[1] == NATIVE_API_MESSAGE_NUMBER_COMMAND_REQUEST);
    assert(capture.length[1] == sizeof(payload));
    assert(memcmp(capture.payload[1], payload, sizeof(payload)) == 0);
    assert(capture.type[2] == UINT32_MAX);
    assert(native_api_message_name(capture.type[2]) == NULL);
    assert(strcmp(native_api_message_name(capture.type[0]), "PingRequest") == 0);
}

static void test_every_split_and_byte_at_a_time(void)
{
    uint8_t stream[96];
    uint8_t first_payload[] = {1, 2, 3, 4, 5};
    size_t first = encode(NATIVE_API_MESSAGE_HELLO_REQUEST, first_payload,
                          sizeof(first_payload), stream, sizeof(stream));
    size_t second = encode(NATIVE_API_MESSAGE_PING_RESPONSE, NULL, 0,
                           stream + first, sizeof(stream) - first);
    size_t total = first + second;

    for (size_t split = 0; split <= total; split++) {
        uint8_t storage[TEST_MAX_PAYLOAD];
        native_api_parser_t parser;
        capture_t capture = {0};
        init_parser(&parser, storage, sizeof(storage), TEST_MAX_PAYLOAD);
        assert(native_api_parser_feed(&parser, stream, split, capture_frame, &capture) ==
               NATIVE_API_STATUS_OK);
        assert(native_api_parser_feed(&parser, stream + split, total - split,
                                      capture_frame, &capture) == NATIVE_API_STATUS_OK);
        assert(capture.count == 2);
        assert(capture.type[0] == NATIVE_API_MESSAGE_HELLO_REQUEST);
        assert(capture.length[0] == sizeof(first_payload));
        assert(memcmp(capture.payload[0], first_payload, sizeof(first_payload)) == 0);
        assert(capture.type[1] == NATIVE_API_MESSAGE_PING_RESPONSE);
        assert(capture.length[1] == 0);
    }

    uint8_t storage[TEST_MAX_PAYLOAD];
    native_api_parser_t parser;
    capture_t capture = {0};
    init_parser(&parser, storage, sizeof(storage), TEST_MAX_PAYLOAD);
    for (size_t i = 0; i < total; i++) {
        assert(native_api_parser_feed(&parser, stream + i, 1, capture_frame, &capture) ==
               NATIVE_API_STATUS_OK);
    }
    assert(capture.count == 2);
}

static void expect_bad_input(const uint8_t *data, size_t length, native_api_status_t expected)
{
    uint8_t storage[TEST_MAX_PAYLOAD];
    native_api_parser_t parser;
    init_parser(&parser, storage, sizeof(storage), TEST_MAX_PAYLOAD);
    assert(native_api_parser_feed(&parser, data, length, NULL, NULL) == expected);
    assert(native_api_parser_feed(&parser, NULL, 0, NULL, NULL) == expected);

    native_api_parser_reset(&parser);
    uint8_t ping[16];
    size_t ping_size = encode(NATIVE_API_MESSAGE_PING_REQUEST, NULL, 0, ping, sizeof(ping));
    capture_t capture = {0};
    assert(native_api_parser_feed(&parser, ping, ping_size, capture_frame, &capture) ==
           NATIVE_API_STATUS_OK);
    assert(capture.count == 1);
}

static void test_invalid_inputs(void)
{
    const uint8_t noise_record[] = {0x01};
    const uint8_t unterminated_length[] = {0x00, 0x80, 0x80, 0x80, 0x80, 0x80};
    const uint8_t overflow_length[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x10};
    const uint8_t unterminated_type[] = {0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80};
    const uint8_t overflow_type[] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x10};
    const uint8_t oversized_frame[] = {0x00, TEST_MAX_PAYLOAD + 1u};

    expect_bad_input(noise_record, sizeof(noise_record), NATIVE_API_STATUS_INVALID_PREAMBLE);
    expect_bad_input(unterminated_length, sizeof(unterminated_length),
                     NATIVE_API_STATUS_INVALID_VARINT);
    expect_bad_input(overflow_length, sizeof(overflow_length), NATIVE_API_STATUS_INVALID_VARINT);
    expect_bad_input(unterminated_type, sizeof(unterminated_type),
                     NATIVE_API_STATUS_INVALID_VARINT);
    expect_bad_input(overflow_type, sizeof(overflow_type), NATIVE_API_STATUS_INVALID_VARINT);
    expect_bad_input(oversized_frame, sizeof(oversized_frame), NATIVE_API_STATUS_FRAME_TOO_LARGE);
}

static void test_bounds_and_encoding_errors(void)
{
    struct {
        uint8_t before;
        uint8_t payload[4];
        uint8_t after;
    } guarded = {0xA5, {0}, 0x5A};
    uint8_t payload[] = {9, 8, 7, 6};
    uint8_t frame[32];
    size_t frame_size = encode(NATIVE_API_MESSAGE_SWITCH_COMMAND_REQUEST,
                               payload, sizeof(payload), frame, sizeof(frame));
    native_api_parser_t parser;
    capture_t capture = {0};
    init_parser(&parser, guarded.payload, sizeof(guarded.payload), sizeof(guarded.payload));
    assert(native_api_parser_feed(&parser, frame, frame_size, capture_frame, &capture) ==
           NATIVE_API_STATUS_OK);
    assert(capture.count == 1);
    assert(guarded.before == 0xA5 && guarded.after == 0x5A);

    size_t required = 0;
    assert(native_api_encoded_size(NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_RESPONSE,
                                   sizeof(payload), sizeof(payload), &required) ==
           NATIVE_API_STATUS_OK);
    assert(native_api_encode_frame(NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_RESPONSE,
                                   payload, sizeof(payload), sizeof(payload), frame,
                                   required - 1, &required) ==
           NATIVE_API_STATUS_OUTPUT_TOO_SMALL);
    assert(required > frame_size);
    assert(native_api_encode_frame(NATIVE_API_MESSAGE_PING_REQUEST, payload,
                                   sizeof(payload) + 1, sizeof(payload), frame,
                                   sizeof(frame), &required) == NATIVE_API_STATUS_FRAME_TOO_LARGE);
    assert(native_api_encoded_size(NATIVE_API_MESSAGE_PING_REQUEST, SIZE_MAX,
                                   TEST_MAX_PAYLOAD, &required) ==
           NATIVE_API_STATUS_FRAME_TOO_LARGE);
    assert(native_api_parser_init(NULL, guarded.payload, 4, 4) ==
           NATIVE_API_STATUS_INVALID_ARGUMENT);
    assert(native_api_parser_init(&parser, NULL, 4, 4) ==
           NATIVE_API_STATUS_INVALID_ARGUMENT);
    assert(native_api_parser_init(&parser, guarded.payload, 4, 5) ==
           NATIVE_API_STATUS_INVALID_ARGUMENT);
}

static void test_empty_payload_without_storage(void)
{
    native_api_parser_t parser;
    capture_t capture = {0};
    init_parser(&parser, NULL, 0, 0);
    uint8_t frame[16];
    size_t written = 0;
    assert(native_api_encode_frame(NATIVE_API_MESSAGE_DISCONNECT_REQUEST, NULL, 0, 0,
                                   frame, sizeof(frame), &written) == NATIVE_API_STATUS_OK);
    assert(native_api_parser_feed(&parser, frame, written, capture_frame, &capture) ==
           NATIVE_API_STATUS_OK);
    assert(capture.count == 1);
    assert(capture.type[0] == NATIVE_API_MESSAGE_DISCONNECT_REQUEST);
    assert(capture.length[0] == 0);
}

static void check_large_frame(const native_api_frame_t *frame, void *context)
{
    size_t *count = context;
    assert(frame->message_type == NATIVE_API_MESSAGE_SENSOR_STATE_RESPONSE);
    assert(frame->payload_length == 300);
    for (size_t i = 0; i < frame->payload_length; i++) {
        assert(frame->payload[i] == (uint8_t)(i % 251));
    }
    (*count)++;
}

static void test_wire_layout_and_multibyte_length(void)
{
    uint8_t empty_frame[8];
    size_t empty_size = encode(NATIVE_API_MESSAGE_PING_REQUEST, NULL, 0,
                               empty_frame, sizeof(empty_frame));
    assert(empty_size == 3);
    assert(empty_frame[0] == 0x00 && empty_frame[1] == 0x00);
    assert(empty_frame[2] == NATIVE_API_MESSAGE_PING_REQUEST);

    size_t capability_size = encode(NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_RESPONSE,
                                    NULL, 0, empty_frame, sizeof(empty_frame));
    assert(capability_size == 4);
    assert(empty_frame[0] == 0x00 && empty_frame[1] == 0x00);
    assert(empty_frame[2] ==
           (uint8_t)((NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_RESPONSE & 0x7F) | 0x80));
    assert(empty_frame[3] == (NATIVE_API_MESSAGE_DEVICE_CAPABILITIES_RESPONSE >> 7));

    uint8_t payload[300];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i % 251);
    }
    uint8_t frame[320];
    size_t frame_size = 0;
    assert(native_api_encode_frame(NATIVE_API_MESSAGE_SENSOR_STATE_RESPONSE,
                                   payload, sizeof(payload), sizeof(payload), frame,
                                   sizeof(frame), &frame_size) == NATIVE_API_STATUS_OK);
    assert(frame[0] == 0x00);
    assert(frame[1] == (uint8_t)((sizeof(payload) & 0x7F) | 0x80));
    assert(frame[2] == (sizeof(payload) >> 7));

    uint8_t storage[300];
    native_api_parser_t parser;
    size_t count = 0;
    init_parser(&parser, storage, sizeof(storage), sizeof(storage));
    for (size_t offset = 0; offset < frame_size; offset += 7) {
        size_t remaining = frame_size - offset;
        size_t chunk = remaining < 7 ? remaining : 7;
        assert(native_api_parser_feed(&parser, frame + offset, chunk,
                                      check_large_frame, &count) == NATIVE_API_STATUS_OK);
    }
    assert(count == 1);
}

int main(void)
{
    test_sticky_frames_and_unknown_messages();
    test_every_split_and_byte_at_a_time();
    test_invalid_inputs();
    test_bounds_and_encoding_errors();
    test_empty_payload_without_storage();
    test_wire_layout_and_multibyte_length();
    puts("native_api_protocol C framing: OK");
    return 0;
}
"""

NANOPB_HARNESS = r"""
#include "api.pb.h"
#include "native_api/native_api_messages.h"
#include "native_api_protocol.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static native_api_frame_t decoded_frame;

static void save_frame(const native_api_frame_t *frame, void *context)
{
    (void)context;
    decoded_frame = *frame;
}

static void test_number_command_roundtrip(void)
{
    NumberCommandRequest input = NumberCommandRequest_init_zero;
    input.key = 0xDEADBEEFu;
    input.state = 42.5f;
    uint8_t payload[64];
    pb_ostream_t payload_stream = pb_ostream_from_buffer(payload, sizeof(payload));
    assert(pb_encode(&payload_stream, NumberCommandRequest_fields, &input));

    uint8_t encoded[80];
    size_t encoded_size = 0;
    assert(native_api_encode_frame(NATIVE_API_MESSAGE_NUMBER_COMMAND_REQUEST,
                                   payload, payload_stream.bytes_written, sizeof(payload),
                                   encoded, sizeof(encoded), &encoded_size) == NATIVE_API_STATUS_OK);
    uint8_t storage[64];
    native_api_parser_t parser;
    assert(native_api_parser_init(&parser, storage, sizeof(storage), sizeof(storage)) ==
           NATIVE_API_STATUS_OK);
    for (size_t i = 0; i < encoded_size; i++) {
        assert(native_api_parser_feed(&parser, encoded + i, 1, save_frame, NULL) ==
               NATIVE_API_STATUS_OK);
    }
    assert(decoded_frame.message_type == NATIVE_API_MESSAGE_NUMBER_COMMAND_REQUEST);

    NumberCommandRequest output = NumberCommandRequest_init_zero;
    pb_istream_t input_stream = pb_istream_from_buffer(decoded_frame.payload,
                                                        decoded_frame.payload_length);
    assert(pb_decode(&input_stream, NumberCommandRequest_fields, &output));
    assert(output.key == input.key);
    assert(fabsf(output.state - input.state) < 0.0001f);
}

static void test_hello_and_empty_capabilities(void)
{
    HelloResponse hello = HelloResponse_init_zero;
    hello.api_version_major = 1;
    hello.api_version_minor = 15;
    strcpy(hello.server_info, "indicator-native-poc");
    strcpy(hello.name, "indicator");
    uint8_t payload[96];
    pb_ostream_t stream = pb_ostream_from_buffer(payload, sizeof(payload));
    assert(pb_encode(&stream, HelloResponse_fields, &hello));

    HelloResponse decoded = HelloResponse_init_zero;
    pb_istream_t input = pb_istream_from_buffer(payload, stream.bytes_written);
    assert(pb_decode(&input, HelloResponse_fields, &decoded));
    assert(decoded.api_version_major == 1 && decoded.api_version_minor == 15);
    assert(strcmp(decoded.server_info, hello.server_info) == 0);
    assert(strcmp(decoded.name, hello.name) == 0);

    DeviceCapabilitiesResponse capabilities = DeviceCapabilitiesResponse_init_zero;
    stream = pb_ostream_from_buffer(payload, sizeof(payload));
    assert(pb_encode(&stream, DeviceCapabilitiesResponse_fields, &capabilities));
    assert(stream.bytes_written == 0);
}

int main(void)
{
    test_number_command_roundtrip();
    test_hello_and_empty_capabilities();
    puts("native_api_protocol nanopb roundtrip: OK");
    return 0;
}
"""

EXPECTED_MESSAGES = {
    "HelloRequest",
    "HelloResponse",
    "DeviceInfoRequest",
    "DeviceInfoResponse",
    "DeviceCapabilitiesRequest",
    "DeviceCapabilitiesResponse",
    "ListEntitiesRequest",
    "ListEntitiesSensorResponse",
    "ListEntitiesSwitchResponse",
    "ListEntitiesNumberResponse",
    "ListEntitiesDoneResponse",
    "SubscribeStatesRequest",
    "SensorStateResponse",
    "SwitchStateResponse",
    "SwitchCommandRequest",
    "NumberStateResponse",
    "NumberCommandRequest",
    "PingRequest",
    "PingResponse",
    "DisconnectRequest",
    "DisconnectResponse",
}


def compile_and_run(sources: list[Path], harness: str, includes: list[Path], name: str) -> None:
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        harness_path = tmp / f"{name}.c"
        binary_path = tmp / name
        harness_path.write_text(textwrap.dedent(harness), encoding="utf-8")
        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        for include in includes:
            command.extend(["-I", str(include)])
        command.extend(str(source) for source in sources)
        command.extend([str(harness_path), "-o", str(binary_path)])
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary_path)], cwd=ROOT, check=True)


class NativeApiProtocolTests(unittest.TestCase):
    def test_stream_framing(self) -> None:
        compile_and_run(
            [
                HA_DIR / "native_api_protocol.c",
                NATIVE_API_DIR / "native_api_messages.c",
            ],
            FRAME_HARNESS,
            [HA_DIR],
            "native_api_framing_test",
        )

    def test_pinned_sources_and_requirements(self) -> None:
        lock = json.loads((PROTO_DIR / "source.lock.json").read_text(encoding="utf-8"))
        for name, metadata in lock["esphome"]["files"].items():
            data = (PROTO_DIR / name).read_bytes()
            self.assertEqual(len(data), metadata["size"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), metadata["sha256"])
        requirements = {}
        for line in (ROOT / "scripts" / "requirements-native-api.txt").read_text().splitlines():
            package, version = line.strip().split("==")
            requirements[package] = version
        for package in ("nanopb", "protobuf", "grpcio-tools"):
            self.assertEqual(requirements[package], lock["generator"][package])
        self.assertEqual(lock["home_assistant"]["aioesphomeapi_version"], "46.6.0")

    def test_generated_message_surface(self) -> None:
        generated_header = (NATIVE_API_DIR / "api.pb.h").read_text(encoding="utf-8")
        generated_ids = (NATIVE_API_DIR / "native_api_messages.h").read_text(encoding="utf-8")
        for message in EXPECTED_MESSAGES:
            self.assertIn(f"}} {message};", generated_header)
        self.assertNotIn("pb_callback_t", generated_header)
        self.assertNotIn("NoiseEncryption", generated_header)
        self.assertNotIn("NoiseEncryption", generated_ids)
        self.assertIn("api_encryption_supported", generated_header)
        self.assertNotIn("api_encryption_provisionable", generated_header)

    def test_descriptor_generation_is_reproducible(self) -> None:
        if importlib.util.find_spec("nanopb") is None or importlib.util.find_spec("grpc_tools") is None:
            self.skipTest("install scripts/requirements-native-api.txt to run descriptor checks")
        subprocess.run(
            [os.sys.executable, "scripts/generate_native_api_protocol.py", "--check"],
            cwd=ROOT,
            check=True,
        )

    def test_nanopb_message_roundtrip(self) -> None:
        runtime_value = os.environ.get("NANOPB_RUNTIME_DIR")
        if not runtime_value:
            self.skipTest("set NANOPB_RUNTIME_DIR to the nanopb 0.4.9.1 source directory")
        runtime = Path(runtime_value)
        compile_and_run(
            [
                HA_DIR / "native_api_protocol.c",
                NATIVE_API_DIR / "api.pb.c",
                runtime / "pb_common.c",
                runtime / "pb_encode.c",
                runtime / "pb_decode.c",
            ],
            NANOPB_HARNESS,
            [HA_DIR, NATIVE_API_DIR, runtime],
            "native_api_nanopb_test",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
