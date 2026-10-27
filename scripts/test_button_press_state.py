#!/usr/bin/env python3
"""Host tests for idempotent long-press sleep/shutdown transitions."""

from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>

#include "button_press_state.h"

static button_press_action_t hold_until(unsigned target_ms, bool sleeping)
{
    button_press_action_t action = BUTTON_PRESS_ACTION_NONE;
    while (button_press_elapsed_ms() < target_ms) {
        button_press_action_t next = button_press_hold(100, sleeping);
        if (next != BUTTON_PRESS_ACTION_NONE) {
            action = next;
        }
    }
    return action;
}

int main(void)
{
    button_press_begin();
    assert(hold_until(3100, false) == BUTTON_PRESS_ACTION_PREPARE_SLEEP);
    assert(button_press_release() == BUTTON_PRESS_ACTION_SHUTDOWN);
    assert(button_press_release() == BUTTON_PRESS_ACTION_NONE);

    button_press_begin();
    assert(hold_until(3100, false) == BUTTON_PRESS_ACTION_PREPARE_SLEEP);
    assert(button_press_release() == BUTTON_PRESS_ACTION_SHUTDOWN);

    button_press_begin();
    assert(hold_until(10100, false) == BUTTON_PRESS_ACTION_FACTORY_RESET);
    assert(button_press_release() == BUTTON_PRESS_ACTION_NONE);
    assert(button_press_release() == BUTTON_PRESS_ACTION_NONE);

    button_press_begin();
    assert(hold_until(3100, true) == BUTTON_PRESS_ACTION_RESTART);
    assert(button_press_hold(100, true) == BUTTON_PRESS_ACTION_NONE);

    return 0;
}
"""


class ButtonPressStateTests(unittest.TestCase):
    def test_shutdown_is_emitted_once_per_press(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "button_press_state_test"
            harness = Path(tmp) / "button_press_state_test.c"
            harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")

            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "main" / "btn"),
                    str(harness),
                    str(ROOT / "main" / "btn" / "button_press_state.c"),
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
