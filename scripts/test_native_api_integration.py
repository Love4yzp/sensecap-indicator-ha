#!/usr/bin/env python3
"""Exercise a real Indicator Native API server with aioesphomeapi 46.6.0."""

from __future__ import annotations

import argparse
import asyncio
import base64
import importlib.metadata
import json
import math
import re
import sys
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parent.parent
EXPECTED_CLIENT_VERSION = "46.6.0"

try:
    from aioesphomeapi import (
        APIClient,
        APIConnectionError,
        APIVersion,
        InvalidEncryptionKeyAPIError,
        RequiresEncryptionAPIError,
        NumberInfo,
        NumberState,
        SensorInfo,
        SensorState,
        SwitchInfo,
        SwitchState,
    )
except ImportError as exc:
    raise SystemExit(
        "Missing optional integration dependency. Create an isolated environment and "
        "install scripts/requirements-native-api-integration.txt; this script never "
        "installs packages automatically."
    ) from exc


def check_client_version() -> None:
    actual = importlib.metadata.version("aioesphomeapi")
    if actual != EXPECTED_CLIENT_VERSION:
        raise RuntimeError(
            f"aioesphomeapi {actual} is installed; {EXPECTED_CLIENT_VERSION} is required"
        )
    lock = json.loads(
        (ROOT / "main/ha/native_api/proto/source.lock.json").read_text(encoding="utf-8")
    )
    if lock["home_assistant"]["aioesphomeapi_version"] != actual:
        raise RuntimeError("The installed client does not match the pinned protocol lock")


def validate_device_info(info, psk) -> None:
    if info.manufacturer != "Seeed Studio" or info.model != "SenseCAP Indicator":
        raise AssertionError(f"Unexpected device identity: {info}")
    if info.esphome_version:
        raise AssertionError("The firmware must not claim an ESPHome version")
    if info.project_name != "Seeed Studio.sensecap-indicator-ha":
        raise AssertionError("project_name must use the HA author.project format")
    if (
        info.uses_password
        or info.api_encryption_supported != (psk is not None)
        or info.api_encryption_provisionable
    ):
        raise AssertionError("DeviceInfo does not match the active transport security")
    compact_mac = info.mac_address.replace(":", "").lower()
    if not re.fullmatch(r"[0-9a-f]{12}", compact_mac):
        raise AssertionError(f"Invalid real MAC address: {info.mac_address!r}")
    match = re.fullmatch(r"sensecap-indicator-([0-9a-f]{6})", info.name)
    if match is None or match.group(1) != compact_mac[-6:]:
        raise AssertionError(f"Device name does not carry its stable MAC suffix: {info.name!r}")


async def short_discovery_probe(host: str, port: int, timeout: float, psk: str | None) -> None:
    client = APIClient(host, port, "", client_info="indicator-native-integration-probe", noise_psk=psk)
    try:
        await asyncio.wait_for(client.connect(), timeout)
        info = await asyncio.wait_for(client.device_info(), timeout)
        validate_device_info(info, psk)
        capabilities = await asyncio.wait_for(client.device_capabilities(), timeout)
        if capabilities.bluetooth_proxy.feature_flags or capabilities.voice_assistant.feature_flags:
            raise AssertionError("DeviceCapabilities claims unimplemented features")
        print(f"Short connection: {info.friendly_name} ({info.mac_address}), API {client.api_version}")
    finally:
        await asyncio.wait_for(client.disconnect(force=True), timeout)


class StateCollector:
    def __init__(self) -> None:
        self.latest: dict[tuple[str, int], object] = {}
        self.history: list[object] = []

    def __call__(self, state) -> None:
        if isinstance(state, SensorState):
            kind = "sensor"
        elif isinstance(state, SwitchState):
            kind = "switch"
        elif isinstance(state, NumberState):
            kind = "number"
        else:
            return
        self.latest[(kind, state.key)] = state
        self.history.append(state)

    def get(self, kind: str, key: int):
        return self.latest.get((kind, key))

    async def wait_for(
        self,
        predicate: Callable[[], bool],
        timeout: float,
        description: str,
    ) -> None:
        async with asyncio.timeout(timeout):
            while not predicate():
                await asyncio.sleep(0.05)
        print(description)


def entities_by_type(entities, entity_type) -> list:
    return [entity for entity in entities if type(entity) is entity_type]


async def exercise_long_connection(
    host: str, port: int, timeout: float, sensor_timeout: float, psk: str | None
) -> None:
    client = APIClient(host, port, "", client_info="indicator-native-integration", noise_psk=psk)
    collector = StateCollector()
    switch_target = None
    number_target = None
    try:
        await asyncio.wait_for(client.connect(), timeout)
        if client.api_version != APIVersion(1, 15):
            raise AssertionError(f"Unexpected advertised API version: {client.api_version}")
        info, entities, services = await asyncio.wait_for(
            client.device_info_and_list_entities(), timeout
        )
        validate_device_info(info, psk)
        if services:
            raise AssertionError("Unexpected user services")
        sensors = entities_by_type(entities, SensorInfo)
        switches = entities_by_type(entities, SwitchInfo)
        numbers = entities_by_type(entities, NumberInfo)
        if (len(sensors), len(switches), len(numbers)) != (4, 6, 2):
            raise AssertionError(
                f"Expected 4 sensors/6 switches/2 numbers, got "
                f"{len(sensors)}/{len(switches)}/{len(numbers)}"
            )
        keys = [entity.key for entity in entities]
        object_ids = [entity.object_id for entity in entities]
        if len(set(keys)) != 12 or len(set(object_ids)) != 12:
            raise AssertionError("Native keys or object_ids are not unique")
        print("ListEntities: 4 sensors, 6 switches, 2 numbers with unique Registry identities")

        client.subscribe_states(collector)
        expected = [(kind, entity.key) for kind, group in (
            ("sensor", sensors), ("switch", switches), ("number", numbers)
        ) for entity in group]
        await collector.wait_for(
            lambda: all(key in collector.latest for key in expected),
            timeout,
            "SubscribeStates: received all 12 initial Core snapshots",
        )
        sensor_events_after_snapshot = sum(isinstance(state, SensorState) for state in collector.history)

        switch = switches[0]
        initial_switch = collector.get("switch", switch.key)
        if initial_switch is None or initial_switch.missing_state:
            raise AssertionError("Switch initial state is missing")
        switch_target = not initial_switch.state
        client.switch_command(switch.key, switch_target)
        await collector.wait_for(
            lambda: (state := collector.get("switch", switch.key)) is not None
            and state.state == switch_target,
            timeout,
            "Switch command: Core observer returned the authoritative new state",
        )

        number = numbers[0]
        initial_number = collector.get("number", number.key)
        if initial_number is None or initial_number.missing_state:
            raise AssertionError("Number initial state is missing")
        if not math.isfinite(initial_number.state) or number.step <= 0:
            raise AssertionError("Invalid Number metadata or initial state")
        number_target = number.min_value
        if math.isclose(number_target, initial_number.state):
            number_target += number.step
        if number_target > number.max_value:
            number_target = number.max_value
        if math.isclose(number_target, initial_number.state):
            raise AssertionError("Number has no alternate value available for command testing")
        client.number_command(number.key, number_target)
        await collector.wait_for(
            lambda: (state := collector.get("number", number.key)) is not None
            and math.isclose(state.state, number_target, rel_tol=1e-5, abs_tol=1e-5),
            timeout,
            "Number command: Core observer returned the authoritative new state",
        )

        client.switch_command(0xFFFFFFFF, True)
        client.number_command(number.key, math.nan)
        client.number_command(number.key, number.max_value + number.step)
        await asyncio.sleep(0.2)
        await asyncio.wait_for(client.device_info(), timeout)
        print("Unknown key, non-finite and out-of-range commands ignored; connection remains usable")

        client.switch_command(switch.key, initial_switch.state)
        await collector.wait_for(
            lambda: (state := collector.get("switch", switch.key)) is not None
            and state.state == initial_switch.state,
            timeout,
            "Switch restored",
        )
        switch_target = None
        client.number_command(number.key, initial_number.state)
        await collector.wait_for(
            lambda: (state := collector.get("number", number.key)) is not None
            and math.isclose(state.state, initial_number.state, rel_tol=1e-5, abs_tol=1e-5),
            timeout,
            "Number restored",
        )
        number_target = None

        await collector.wait_for(
            lambda: sum(isinstance(state, SensorState) for state in collector.history)
            > sensor_events_after_snapshot,
            sensor_timeout,
            "Sensor: received a post-snapshot RP2040/Core state push",
        )
    finally:
        try:
            if switch_target is not None:
                client.switch_command(switch.key, initial_switch.state)
            if number_target is not None:
                client.number_command(number.key, initial_number.state)
        finally:
            await asyncio.wait_for(client.disconnect(force=True), timeout)


async def negative_noise_tests(host: str, port: int, psk: str, timeout: float) -> None:
    raw_psk = bytearray(base64.b64decode(psk, validate=True))
    if len(raw_psk) != 32:
        raise AssertionError("The integration PSK must decode to 32 bytes")
    raw_psk[0] ^= 1
    wrong_psk = base64.b64encode(raw_psk).decode("ascii")
    wrong_client = APIClient(host, port, "", noise_psk=wrong_psk)
    try:
        try:
            await asyncio.wait_for(wrong_client.connect(), timeout)
        except InvalidEncryptionKeyAPIError:
            print("Wrong Noise PSK rejected with authenticated handshake failure")
        else:
            raise AssertionError("Wrong Noise PSK was accepted")
    finally:
        await asyncio.wait_for(wrong_client.disconnect(force=True), timeout)

    plaintext_client = APIClient(host, port, "")
    try:
        try:
            await asyncio.wait_for(plaintext_client.connect(), timeout)
        except RequiresEncryptionAPIError:
            print("Plaintext downgrade rejected while PSK is configured")
        else:
            raise AssertionError("Plaintext connected to a PSK-only server")
    finally:
        await asyncio.wait_for(plaintext_client.disconnect(force=True), timeout)


async def run(args: argparse.Namespace) -> None:
    check_client_version()
    await short_discovery_probe(args.host, args.port, args.timeout, args.psk)
    await exercise_long_connection(
        args.host, args.port, args.timeout, args.sensor_timeout, args.psk
    )
    if args.negative_tests:
        await negative_noise_tests(args.host, args.port, args.psk, args.timeout)
    print(f"Native API integration: PASS ({'Noise PSK' if args.psk else 'plaintext'}, reconnected)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="device IP address or resolvable hostname")
    parser.add_argument("--port", type=int, default=6053)
    parser.add_argument("--timeout", type=float, default=15.0, help="per-operation timeout in seconds")
    parser.add_argument("--sensor-timeout", type=float, default=30.0)
    parser.add_argument("--psk", help="base64-encoded 32-byte Noise PSK; omit for plaintext")
    parser.add_argument(
        "--negative-tests", action="store_true", help="with --psk, test wrong-key and plaintext rejection"
    )
    args = parser.parse_args()
    if args.negative_tests and not args.psk:
        parser.error("--negative-tests requires --psk")
    try:
        asyncio.run(run(args))
    except (APIConnectionError, AssertionError, RuntimeError, TimeoutError, OSError) as exc:
        print(f"Native API integration: FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
