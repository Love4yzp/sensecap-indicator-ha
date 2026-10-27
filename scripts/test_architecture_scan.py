#!/usr/bin/env python3
"""Unit tests for architecture-scan source masking."""

from __future__ import annotations

import unittest

from architecture_scan import LVGL_CALL_RE, mask_c_non_code


class MaskCNonCodeTests(unittest.TestCase):
    def test_masks_comments_and_literals_but_keeps_real_calls(self) -> None:
        source = """\
/* lv_label_set_text(blocked, "comment"); */
const char *example = "lv_obj_create(fake)";
// lv_button_create(commented);
lv_obj_create(real);
"""

        masked = mask_c_non_code(source)
        calls = [match.group(1) for match in LVGL_CALL_RE.finditer(masked)]

        self.assertEqual(calls, ["lv_obj_create"])
        self.assertEqual(len(masked), len(source))
        self.assertEqual(masked.count("\n"), source.count("\n"))

    def test_handles_escaped_quotes(self) -> None:
        source = 'const char *value = "escaped \\" lv_obj_create(fake)";\nlv_label_create(real);\n'

        masked = mask_c_non_code(source)
        calls = [match.group(1) for match in LVGL_CALL_RE.finditer(masked)]

        self.assertEqual(calls, ["lv_label_create"])


if __name__ == "__main__":
    unittest.main()
