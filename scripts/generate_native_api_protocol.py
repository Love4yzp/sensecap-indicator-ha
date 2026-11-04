#!/usr/bin/env python3
"""Generate the pinned ESPHome Native API subset and its descriptor-derived IDs."""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.metadata
import json
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NATIVE_API_DIR = ROOT / "main" / "ha" / "native_api"
PROTO_DIR = NATIVE_API_DIR / "proto"
LOCK_PATH = PROTO_DIR / "source.lock.json"
OPTIONS_PATH = PROTO_DIR / "api.options"
GENERATED_FILES = ("api.pb.h", "api.pb.c", "native_api_messages.h", "native_api_messages.c")

# None keeps every field; a tuple keeps only the named fields.
MESSAGE_SELECTION = {
    "HelloRequest": None,
    "HelloResponse": None,
    "DisconnectRequest": None,
    "DisconnectResponse": None,
    "PingRequest": None,
    "PingResponse": None,
    "DeviceInfoRequest": None,
    "DeviceInfoResponse": (
        "uses_password",
        "name",
        "mac_address",
        "esphome_version",
        "compilation_time",
        "model",
        "has_deep_sleep",
        "project_name",
        "project_version",
        "manufacturer",
        "friendly_name",
        "suggested_area",
        "api_encryption_supported",
    ),
    "DeviceCapabilitiesRequest": None,
    "DeviceCapabilitiesResponse": (),
    "ListEntitiesRequest": None,
    "ListEntitiesDoneResponse": None,
    "SubscribeStatesRequest": None,
    "ListEntitiesSensorResponse": None,
    "SensorStateResponse": None,
    "ListEntitiesSwitchResponse": None,
    "SwitchStateResponse": None,
    "SwitchCommandRequest": None,
    "ListEntitiesNumberResponse": None,
    "NumberStateResponse": None,
    "NumberCommandRequest": None,
}


def load_lock() -> dict:
    return json.loads(LOCK_PATH.read_text(encoding="utf-8"))


def verify_sources(lock: dict) -> None:
    for name, metadata in lock["esphome"]["files"].items():
        data = (PROTO_DIR / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != metadata["size"] or digest != metadata["sha256"]:
            raise RuntimeError(
                f"{name} does not match ESPHome {lock['esphome']['version']} "
                f"at {lock['esphome']['commit']}"
            )


def fetch_sources(lock: dict) -> None:
    esphome = lock["esphome"]
    downloaded = {}
    for name, metadata in esphome["files"].items():
        url = f"{esphome['repository']}/raw/{esphome['commit']}/{metadata['path']}"
        with urllib.request.urlopen(url) as response:
            data = response.read()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != metadata["size"] or digest != metadata["sha256"]:
            raise RuntimeError(f"downloaded {name} failed its pinned size/hash check")
        downloaded[name] = data
    for name, data in downloaded.items():
        (PROTO_DIR / name).write_bytes(data)


def check_tool_versions(lock: dict) -> None:
    for package in ("nanopb", "protobuf", "grpcio-tools"):
        actual = importlib.metadata.version(package)
        expected = lock["generator"][package]
        if actual != expected:
            raise RuntimeError(f"{package} {actual} found; pinned version is {expected}")


def run(command: list[str], cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def enum_name(message_name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", message_name).upper()


def render_id_files(messages: list, api_options_pb2) -> dict[str, bytes]:
    id_extension = api_options_pb2.id
    entries = []
    by_name = {message.name: message for message in messages}
    for name in MESSAGE_SELECTION:
        message = by_name[name]
        if not message.options.HasExtension(id_extension):
            raise RuntimeError(f"{name} has no descriptor (id) extension")
        message_id = message.options.Extensions[id_extension]
        if not 0 < message_id <= 0xFFFFFFFF:
            raise RuntimeError(f"{name} has invalid message ID {message_id}")
        entries.append((name, message_id))
    ids = [message_id for _, message_id in entries]
    if len(ids) != len(set(ids)):
        raise RuntimeError("duplicate selected message IDs")

    guard = "MAIN_HA_NATIVE_API_NATIVE_API_MESSAGES_H"
    header = [
        "/* Generated from the pinned api.proto descriptor (id) extensions. */",
        "/* Do not edit; use scripts/generate_native_api_protocol.py. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "typedef enum {",
    ]
    header.extend(
        f"    NATIVE_API_MESSAGE_{enum_name(name)} = {message_id}," for name, message_id in entries
    )
    header.extend(
        [
            "} native_api_message_id_t;",
            "",
            "/* Returns NULL for unsupported and unknown IDs; callers can safely ignore them. */",
            "const char *native_api_message_name(uint32_t message_id);",
            "",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
            f"#endif /* {guard} */",
            "",
        ]
    )
    source = [
        "/* Generated from the pinned api.proto descriptor (id) extensions. */",
        "/* Do not edit; use scripts/generate_native_api_protocol.py. */",
        '#include "native_api_messages.h"',
        "",
        "#include <stddef.h>",
        "",
        "const char *native_api_message_name(uint32_t message_id)",
        "{",
        "    switch (message_id) {",
    ]
    source.extend(
        f"    case NATIVE_API_MESSAGE_{enum_name(name)}:\n        return \"{name}\";"
        for name, _ in entries
    )
    source.extend(
        [
            "    default:",
            "        return NULL;",
            "    }",
            "}",
            "",
        ]
    )
    return {
        "native_api_messages.h": "\n".join(header).encode("utf-8"),
        "native_api_messages.c": "\n".join(source).encode("utf-8"),
    }


def select_api_file(api_file, api_options_pb2):
    selected = copy.deepcopy(api_file)
    selected.ClearField("service")
    selected.ClearField("source_code_info")
    original_messages = {message.name: message for message in api_file.message_type}
    original_enums = {enum.name: enum for enum in api_file.enum_type}
    kept_messages = []
    required_types = set()

    for name, allowed_fields in MESSAGE_SELECTION.items():
        if name not in original_messages:
            raise RuntimeError(f"selected message {name} is absent from pinned api.proto")
        message = copy.deepcopy(original_messages[name])
        if allowed_fields is not None:
            available = {field.name for field in message.field}
            missing = set(allowed_fields) - available
            if missing:
                raise RuntimeError(f"{name} fields missing upstream: {sorted(missing)}")
            retained = [field for field in message.field if field.name in allowed_fields]
            message.ClearField("field")
            message.field.extend(retained)
        for field in message.field:
            if field.type_name:
                required_types.add(field.type_name)
        kept_messages.append(message)

    # Resolve transitive message/enum dependencies without retaining unrelated API entities.
    resolved_messages = {}
    pending = list(required_types)
    while pending:
        type_name = pending.pop()
        short_name = type_name.rsplit(".", 1)[-1]
        if short_name in original_messages and short_name not in resolved_messages:
            dependency = copy.deepcopy(original_messages[short_name])
            resolved_messages[short_name] = dependency
            for field in dependency.field:
                if field.type_name:
                    required_types.add(field.type_name)
                    pending.append(field.type_name)

    selected.ClearField("message_type")
    selected.message_type.extend(kept_messages)
    selected.message_type.extend(resolved_messages.values())
    selected.ClearField("enum_type")
    selected.enum_type.extend(
        copy.deepcopy(enum)
        for name, enum in original_enums.items()
        if f".{name}" in required_types
    )
    return selected


def generate(lock: dict, output_dir: Path) -> None:
    try:
        import grpc_tools
        from google.protobuf import descriptor_pb2
    except ImportError as exc:
        raise RuntimeError(
            "generation requires an isolated environment with "
            "scripts/requirements-native-api.txt installed"
        ) from exc

    check_tool_versions(lock)
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        descriptor_path = tmp / "api_full.pb"
        python_dir = tmp / "python"
        python_dir.mkdir()
        grpc_include = Path(grpc_tools.__file__).resolve().parent / "_proto"
        common = [
            sys.executable,
            "-m",
            "grpc_tools.protoc",
            f"-I{PROTO_DIR}",
            f"-I{grpc_include}",
        ]
        run(
            common
            + [
                "--include_imports",
                f"--descriptor_set_out={descriptor_path}",
                "api.proto",
            ],
            cwd=PROTO_DIR,
        )
        run(common + [f"--python_out={python_dir}", "api_options.proto"], cwd=PROTO_DIR)
        sys.path.insert(0, str(python_dir))
        try:
            import api_options_pb2
        finally:
            sys.path.pop(0)

        descriptor_bytes = descriptor_path.read_bytes()
        file_set = descriptor_pb2.FileDescriptorSet.FromString(descriptor_bytes)
        files = {file.name: file for file in file_set.file}
        api_file = files["api.proto"]
        generated = render_id_files(api_file.message_type, api_options_pb2)
        selected_api = select_api_file(api_file, api_options_pb2)

        filtered_set = descriptor_pb2.FileDescriptorSet()
        for file in file_set.file:
            filtered_set.file.add().CopyFrom(selected_api if file.name == "api.proto" else file)
        filtered_path = tmp / "filtered.pb"
        filtered_path.write_bytes(filtered_set.SerializeToString())

        nanopb_dir = tmp / "nanopb"
        nanopb_dir.mkdir()
        generator = Path(sys.executable).with_name("nanopb_generator")
        if not generator.exists():
            raise RuntimeError(f"nanopb_generator not found beside {sys.executable}")
        run(
            [
                str(generator),
                f"--options-file={OPTIONS_PATH}",
                f"--options-path={PROTO_DIR}",
                f"--output-dir={nanopb_dir}",
                "--strip-path",
                "--error-on-unmatched",
                "-xapi_options.proto",
                str(filtered_path),
            ]
        )
        for name in ("api.pb.h", "api.pb.c"):
            generated[name] = (nanopb_dir / name).read_bytes()

    for name in GENERATED_FILES:
        (output_dir / name).write_bytes(generated[name].rstrip(b"\n") + b"\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="refresh vendored protos from their pinned URLs")
    parser.add_argument("--check", action="store_true", help="verify generated files without modifying them")
    args = parser.parse_args()
    lock = load_lock()
    if args.fetch:
        if args.check:
            parser.error("--fetch and --check are mutually exclusive")
        fetch_sources(lock)
    verify_sources(lock)
    if args.check:
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            generate(lock, tmp)
            mismatches = [
                name
                for name in GENERATED_FILES
                if not (NATIVE_API_DIR / name).exists()
                or (NATIVE_API_DIR / name).read_bytes() != (tmp / name).read_bytes()
            ]
        if mismatches:
            raise RuntimeError(f"generated files are stale: {', '.join(mismatches)}")
        print("Native API sources, descriptor IDs and generated files: OK")
    else:
        generate(lock, NATIVE_API_DIR)
        for name in GENERATED_FILES:
            path = NATIVE_API_DIR / name
            print(f"{path.relative_to(ROOT)}: {path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"native API generation failed: {exc}")
