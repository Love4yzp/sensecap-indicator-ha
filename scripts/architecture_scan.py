#!/usr/bin/env python3
"""Scan for first-order architecture drift in the ESP32S3 screen firmware.

The scan is intentionally conservative for Stage 1: it blocks new unsafe
patterns while allowlisting current known debt that should be retired in later
stages.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE_EXTS = {".c", ".h", ".cpp", ".hpp", ".cc"}


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    rule: str
    message: str

    def format(self) -> str:
        rel = self.path.relative_to(ROOT)
        return f"{rel}:{self.line}: {self.rule}: {self.message}"


# Keep this small. Each entry is current debt that Stage 1 documents but does
# not fix. New code should not be added here without a written reason.
ALLOWLIST: set[tuple[str, str]] = set()


ALLOWLIST_SUMMARY_ENTRIES: set[tuple[str, str]] = {
    ("main/view_data.h", "shared-bsp-include"),
}


SYMBOL_ALLOWLIST_REASONS: dict[tuple[str, str, str], str] = {
    ("main/main.c", "lvgl-outside-view", "lv_port_init"): (
        "existing boot sequence intentionally initializes the LVGL port from app_main"
    ),
    ("main/ha/ha_switch.c", "lvgl-outside-view", "lv_port_sem_take"): (
        "HA switch controller must hold the LVGL port semaphore before updating its screen component"
    ),
    ("main/ha/ha_switch.c", "lvgl-outside-view", "lv_port_sem_give"): (
        "HA switch controller releases the LVGL port semaphore after updating its screen component"
    ),
}


OCCURRENCE_ALLOWLIST_REASONS: dict[tuple[str, str, str, str], str] = {
    (
        "main/btn/btn.c",
        "service-register-callback",
        "bsp_btn_register_callback",
        "bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_SINGLE_CLICK, _btn_click_callback, NULL);",
    ): "existing single-click button registration uses BSP callback API",
    (
        "main/btn/btn.c",
        "service-register-callback",
        "bsp_btn_register_callback",
        "bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_DOUBLE_CLICK, _btn_double_click_callback, NULL);",
    ): "existing double-click button registration uses BSP callback API",
    (
        "main/btn/btn.c",
        "service-register-callback",
        "bsp_btn_register_callback",
        "bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_LONG_PRESS_START, _btn_press_start_callback, NULL);",
    ): "existing long-press-start button registration uses BSP callback API",
    (
        "main/btn/btn.c",
        "service-register-callback",
        "bsp_btn_register_callback",
        "bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_LONG_PRESS_HOLD, _btn_long_press_hold_callback, NULL);",
    ): "existing long-press-hold button registration uses BSP callback API",
    (
        "main/btn/btn.c",
        "service-register-callback",
        "bsp_btn_register_callback",
        "bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_PRESS_UP, _btn_press_up_callback, NULL);",
    ): "existing press-up button registration uses BSP callback API",
    (
        "main/view_data.h",
        "shared-bsp-include",
        "bsp_board.h",
        "#include <bsp_board.h>",
    ): "existing shared view data header depends on board-level screen constants",
}


EVENT_COMMENT_ALLOWLIST_REASONS: dict[str, str] = {}


VIEW_FILE_PATTERNS = (
    re.compile(r"(^|/)indicator_.*_view\.c$"),
    re.compile(r"(^|/)indicator_view\.c$"),
    re.compile(r"(^|/)ha/ha_config\.c$"),
    re.compile(r"(^|/)ha/ha_.*_screen\.c$"),
    re.compile(r"(^|/)wifi/wifi_view\.c$"),
    re.compile(r"(^|/)wifi/wifi_.*_screen\.c$"),
    re.compile(r"(^|/)sensor/sensor_view\.c$"),
    re.compile(r"(^|/)display/display_view\.c$"),
    re.compile(r"(^|/)settings/settings_view\.c$"),
    re.compile(r"(^|/)lv_port\.[ch]$"),
    re.compile(r"(^|/)nav/nav\.[ch]$"),
    re.compile(r"(^|/)ui(/|$)"),
)


MODEL_FILE_PATTERNS = (
    re.compile(r"(^|/)indicator_model\.[ch]$"),
    re.compile(r"(^|/)indicator_.*_model\.[ch]$"),
    re.compile(r"(^|/)indicator_(?!view\.)(?!.*_view)[a-zA-Z0-9_]*\.[ch]$"),
    re.compile(r"(^|/)esp32_rp2040\.[ch]$"),
)


LVGL_CALL_RE = re.compile(r"\b(lv_[a-zA-Z0-9_]+)\s*\(")
EVENT_ENUM_RE = re.compile(r"^\s*(VIEW_EVENT_[A-Z0-9_]+)\s*(?:=.*?)?,?\s*(?://\s*(.*))?$")
MODEL_UI_INCLUDE_RE = re.compile(r"#\s*include\s*[<\"](?:ui/)?ui\.h[>\"]")
HA_SWITCH_SCREEN_UI_INCLUDE_RE = re.compile(r"#\s*include\s*[<\"](?:ui/)?(?:ui|ui_helpers)\.h[>\"]")
HA_SWITCH_SCREEN_GLOBAL_RE = re.compile(r"\bui_switch(?:[0-9]|_btn3)\w*\b")
BSP_INCLUDE_RE = re.compile(r"#\s*include\s+[<\"](bsp_[^>\"]+)[>\"]")
SERVICE_CALLBACK_RE = re.compile(r"\b([a-zA-Z0-9_]+_register_(?:cb|callback))\s*\(")
VIEW_EVENT_PRIVATE_INCLUDE_RE = re.compile(
    r"#\s*include\s*[<\"][^>\"]*view_event_adapter\.h[>\"]"
)
WIFI_SCAN_PRIVATE_INCLUDE_RE = re.compile(
    r"#\s*include\s*[<\"][^>\"]*wifi_scan_worker_adapter\.h[>\"]"
)
C_INCLUDE_DIRECTIVE_RE = re.compile(r"^\s*#\s*include\b")
VIEW_EVENT_LEGACY_TRANSPORT_RE = re.compile(
    r"\b(view_event_handle|VIEW_EVENT_BASE)\b"
)
VIEW_EVENT_INTERNAL_TRANSPORT_RE = re.compile(r"\bVIEW_EVENT_TRANSPORT_BASE\b")
ESP_EVENT_TRANSPORT_CALL_RE = re.compile(
    r"\b(esp_event_post_to|esp_event_handler_instance_register_with)\s*\("
)
VIEW_EVENT_COMMAND_CALL_RE = re.compile(r"\bview_event_post_command\s*\(")
RESULT_ASSIGNMENT_RE = re.compile(r"(?<![=!<>])=(?!=)[\s(]*$")


def mask_c_non_code(text: str) -> str:
    """Mask comments and literals while preserving offsets and line numbers."""
    result = list(text)
    state = "code"
    index = 0

    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""

        if state == "code":
            if char == "/" and next_char == "/":
                result[index] = result[index + 1] = " "
                state = "line_comment"
                index += 2
                continue
            if char == "/" and next_char == "*":
                result[index] = result[index + 1] = " "
                state = "block_comment"
                index += 2
                continue
            if char == '"':
                result[index] = " "
                state = "string"
            elif char == "'":
                result[index] = " "
                state = "char"
        elif state == "line_comment":
            if char == "\n":
                state = "code"
            else:
                result[index] = " "
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                result[index] = result[index + 1] = " "
                state = "code"
                index += 2
                continue
            if char != "\n":
                result[index] = " "
        elif state in {"string", "char"}:
            result[index] = " " if char != "\n" else "\n"
            if char == "\\" and next_char:
                result[index + 1] = " " if next_char != "\n" else "\n"
                index += 2
                continue
            if (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "code"

        index += 1

    return "".join(result)


def private_include_lines(
    text: str, code_text: str, include_re: re.Pattern[str]
) -> list[int]:
    """Find real include directives while preserving original source lines."""
    lines = []
    for line, (source_line, masked_line) in enumerate(
        zip(text.splitlines(), code_text.splitlines()), start=1
    ):
        if (
            C_INCLUDE_DIRECTIVE_RE.match(masked_line)
            and include_re.search(source_line)
        ):
            lines.append(line)
    return lines


def rel_key(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def is_allowlisted(path: Path, rule: str) -> bool:
    return (rel_key(path), rule) in ALLOWLIST


def is_symbol_allowlisted(path: Path, rule: str, symbol: str) -> bool:
    return (rel_key(path), rule, symbol) in SYMBOL_ALLOWLIST_REASONS


def is_occurrence_allowlisted(path: Path, line: int, rule: str, symbol: str, line_text: str) -> bool:
    del line
    return (rel_key(path), rule, symbol, line_text.strip()) in OCCURRENCE_ALLOWLIST_REASONS


def iter_source_files() -> list[Path]:
    roots = [ROOT / "main"]
    files: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_EXTS:
                files.append(path)
    return sorted(files)


def is_view_file(path: Path) -> bool:
    rel = rel_key(path)
    return any(pattern.search(rel) for pattern in VIEW_FILE_PATTERNS)


def is_model_file(path: Path) -> bool:
    rel = rel_key(path)
    return any(pattern.search(rel) for pattern in MODEL_FILE_PATTERNS)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def matching_paren(text: str, open_offset: int) -> int | None:
    depth = 0
    for offset in range(open_offset, len(text)):
        if text[offset] == "(":
            depth += 1
        elif text[offset] == ")":
            depth -= 1
            if depth == 0:
                return offset
    return None


def command_result_is_handled(
    code_text: str, match: re.Match[str]
) -> bool:
    statement_start = max(
        code_text.rfind(delimiter, 0, match.start())
        for delimiter in (";", "{", "}")
    ) + 1
    prefix = code_text[statement_start:match.start()]
    stripped_prefix = prefix.strip()
    if stripped_prefix == "return" or stripped_prefix.startswith("return "):
        return True
    if RESULT_ASSIGNMENT_RE.search(prefix):
        return True

    call_open = code_text.find("(", match.start(), match.end())
    call_close = matching_paren(code_text, call_open)
    if call_close is None:
        return False

    for if_match in re.finditer(r"\bif\s*\(", code_text[:match.start()]):
        condition_open = code_text.find(
            "(", if_match.start(), if_match.end()
        )
        condition_close = matching_paren(code_text, condition_open)
        if (
            condition_close is None
            or not (condition_open < match.start() < call_close < condition_close)
        ):
            continue
        before_call = code_text[condition_open + 1:match.start()]
        after_call = code_text[call_close + 1:condition_close]
        return bool(
            re.search(r"(?:==|!=)\s*$", before_call)
            or re.match(
                r"\s*(?:==|!=)\s*VIEW_EVENT_DELIVERY_[A-Z_]+",
                after_call,
            )
        )

    return False


def add_finding(findings: list[Finding], path: Path, line: int, rule: str, message: str) -> None:
    if not is_allowlisted(path, rule):
        findings.append(Finding(path, line, rule, message))


def add_symbol_finding(
    findings: list[Finding], path: Path, line: int, rule: str, symbol: str, message: str
) -> None:
    if not is_symbol_allowlisted(path, rule, symbol):
        add_finding(findings, path, line, rule, message)


def add_occurrence_finding(
    findings: list[Finding],
    path: Path,
    line: int,
    rule: str,
    symbol: str,
    line_text: str,
    message: str,
) -> None:
    if not is_occurrence_allowlisted(path, line, rule, symbol, line_text):
        add_symbol_finding(findings, path, line, rule, symbol, message)


def scan_model_ui_includes(path: Path, text: str, findings: list[Finding]) -> None:
    if not is_model_file(path):
        return
    for match in MODEL_UI_INCLUDE_RE.finditer(text):
        line = line_number(text, match.start())
        line_text = text.splitlines()[line - 1]
        include_name = match.group(0).split("include", maxsplit=1)[1].strip().strip("<>\"")
        add_occurrence_finding(
            findings,
            path,
            line,
            "model-ui-include",
            include_name,
            line_text,
            "model/controller files should not gain direct SquareLine UI ownership",
        )


def scan_ha_switch_squareline_bindings(path: Path, text: str, findings: list[Finding]) -> None:
    if rel_key(path) != "main/ha/ha_switch_screen.c":
        return
    for match in HA_SWITCH_SCREEN_UI_INCLUDE_RE.finditer(text):
        line = line_number(text, match.start())
        add_finding(
            findings,
            path,
            line,
            "ha-switch-squareline-binding",
            "ha_switch_screen must create its own widgets without SquareLine UI headers",
        )
    for match in HA_SWITCH_SCREEN_GLOBAL_RE.finditer(text):
        line = line_number(text, match.start())
        add_finding(
            findings,
            path,
            line,
            "ha-switch-squareline-binding",
            "ha_switch_screen must bind local widget handles, not SquareLine switch globals",
        )


def scan_lvgl_calls(path: Path, text: str, findings: list[Finding]) -> None:
    if is_view_file(path):
        return
    code_text = mask_c_non_code(text)
    for match in LVGL_CALL_RE.finditer(code_text):
        symbol = match.group(1)
        add_symbol_finding(
            findings,
            path,
            line_number(text, match.start()),
            "lvgl-outside-view",
            symbol,
            "LVGL calls belong in view/UI files or the LVGL port layer",
        )


def scan_shared_bsp_includes(path: Path, text: str, findings: list[Finding]) -> None:
    rel = rel_key(path)
    if rel not in {"main/view_data.h"} and not rel.endswith("app_events.h"):
        return
    for match in BSP_INCLUDE_RE.finditer(text):
        include_name = match.group(1)
        line = line_number(text, match.start())
        line_text = text.splitlines()[line - 1]
        add_occurrence_finding(
            findings,
            path,
            line,
            "shared-bsp-include",
            include_name,
            line_text,
            "shared event/data headers should not depend on BSP headers",
        )


def scan_event_payload_comments(path: Path, text: str, findings: list[Finding]) -> None:
    if rel_key(path) != "main/view_data_types.h":
        return
    for idx, line in enumerate(text.splitlines(), start=1):
        match = EVENT_ENUM_RE.match(line)
        if not match:
            continue
        name, comment = match.groups()
        if name == "VIEW_EVENT_ALL":
            continue
        if name in EVENT_COMMENT_ALLOWLIST_REASONS:
            continue
        if not comment or not comment.strip():
            add_finding(
                findings,
                path,
                idx,
                "event-payload-comment",
                f"{name} needs an inline payload/lifetime comment",
            )


def scan_service_callbacks(path: Path, text: str, findings: list[Finding]) -> None:
    for match in SERVICE_CALLBACK_RE.finditer(text):
        symbol = match.group(1)
        line = line_number(text, match.start())
        line_text = text.splitlines()[line - 1]
        add_occurrence_finding(
            findings,
            path,
            line,
            "service-register-callback",
            symbol,
            line_text,
            "prefer the shared event loop over service-local callback registration",
        )


def scan_view_event_seam(path: Path, text: str, findings: list[Finding]) -> None:
    """Keep view-event transport details behind the module's internal adapter."""
    rel = rel_key(path)
    code_text = mask_c_non_code(text)
    adapter_path = "main/view_event/view_event_esp_adapter.c"
    wifi_scan_adapter_paths = {
        "main/wifi/wifi_scan_worker.c",
        "main/wifi/wifi_scan_worker_esp_adapter.c",
    }

    if not rel.startswith("main/view_event/"):
        for line in private_include_lines(
            text, code_text, VIEW_EVENT_PRIVATE_INCLUDE_RE
        ):
            add_finding(
                findings,
                path,
                line,
                "view-event-private-adapter",
                "runtime code must use view_event.h, not the private adapter seam",
            )

    if rel not in wifi_scan_adapter_paths:
        for line in private_include_lines(
            text, code_text, WIFI_SCAN_PRIVATE_INCLUDE_RE
        ):
            add_finding(
                findings,
                path,
                line,
                "wifi-scan-private-adapter",
                "runtime code must use wifi_scan_worker.h, not its private adapter seam",
            )

    for match in VIEW_EVENT_LEGACY_TRANSPORT_RE.finditer(code_text):
        add_finding(
            findings,
            path,
            line_number(text, match.start()),
            "view-event-legacy-transport",
            f"{match.group(1)} bypasses the view_event module interface",
        )

    if rel != adapter_path:
        for match in VIEW_EVENT_INTERNAL_TRANSPORT_RE.finditer(code_text):
            add_finding(
                findings,
                path,
                line_number(text, match.start()),
                "view-event-internal-transport",
                "the ESP Event transport base belongs only to the production adapter",
            )

    if rel.startswith("main/view_event/") and rel != adapter_path:
        for match in ESP_EVENT_TRANSPORT_CALL_RE.finditer(code_text):
            add_finding(
                findings,
                path,
                line_number(text, match.start()),
                "view-event-adapter-boundary",
                f"{match.group(1)} belongs only in view_event_esp_adapter.c",
            )

    command_interface_paths = {
        "main/view_event/view_event.c",
        "main/view_event/view_event.h",
    }
    for match in (
        ()
        if rel in command_interface_paths
        else VIEW_EVENT_COMMAND_CALL_RE.finditer(code_text)
    ):
        if command_result_is_handled(code_text, match):
            continue
        add_finding(
            findings,
            path,
            line_number(text, match.start()),
            "view-event-command-result",
            "critical command delivery results must be handled explicitly",
        )


def scan() -> list[Finding]:
    findings: list[Finding] = []
    for path in iter_source_files():
        text = path.read_text(encoding="utf-8", errors="ignore")
        scan_model_ui_includes(path, text, findings)
        scan_ha_switch_squareline_bindings(path, text, findings)
        scan_lvgl_calls(path, text, findings)
        scan_shared_bsp_includes(path, text, findings)
        scan_event_payload_comments(path, text, findings)
        scan_service_callbacks(path, text, findings)
        scan_view_event_seam(path, text, findings)
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list-allowlist", action="store_true", help="print current known-debt allowlist")
    args = parser.parse_args()

    if args.list_allowlist:
        for path, rule in sorted(ALLOWLIST | ALLOWLIST_SUMMARY_ENTRIES):
            print(f"{path}: {rule}")
        for (path, rule, symbol), reason in sorted(SYMBOL_ALLOWLIST_REASONS.items()):
            print(f"{path}: {rule}: {symbol}: {reason}")
        for (path, rule, symbol, line_text), reason in sorted(
            OCCURRENCE_ALLOWLIST_REASONS.items()
        ):
            print(f"{path}: {rule}: {symbol}: {line_text}: {reason}")
        for event, reason in sorted(EVENT_COMMENT_ALLOWLIST_REASONS.items()):
            print(f"{event}: event-payload-comment: {reason}")
        return 0

    findings = scan()
    if findings:
        print("architecture_scan: failed", file=sys.stderr)
        for finding in findings:
            print(f"  {finding.format()}", file=sys.stderr)
        return 1

    print("architecture_scan: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
