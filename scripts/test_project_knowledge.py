#!/usr/bin/env python3
"""Regression tests for event-reliability architecture guidance."""

from __future__ import annotations

import unittest
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class ProjectKnowledgeTests(unittest.TestCase):
    def test_event_seam_and_authority_are_consistent(self) -> None:
        paths = (
            ROOT / "AGENTS.md",
            ROOT / "main" / "AGENTS.md",
            ROOT / "main" / "ARCHITECTURE.md",
            ROOT / "docs" / "stability" / "hardware-smoke-checklist.md",
        )
        texts = {path: path.read_text(encoding="utf-8") for path in paths}
        combined = "\n".join(texts.values())

        for path, text in texts.items():
            with self.subTest(path=path):
                self.assertIn("view_event_init()", text)

        self.assertIn("view_event.h", combined)
        self.assertIn("view_data_types.h", combined)
        for legacy in (
            r"\bview_event_handle\b",
            r"\bVIEW_EVENT_BASE\b",
            r"\besp_event_post_to\b",
        ):
            self.assertIsNone(re.search(legacy, combined))

    def test_hardware_checklist_covers_event_reliability(self) -> None:
        text = (
            ROOT / "docs" / "stability" / "hardware-smoke-checklist.md"
        ).read_text(encoding="utf-8")
        for phrase in (
            "queue pressure",
            "exactly one shutdown",
            "rejected shutdown",
            "short-press wake",
            "Wi-Fi/MQTT",
            "sensor input",
        ):
            with self.subTest(phrase=phrase):
                self.assertIn(phrase, text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
