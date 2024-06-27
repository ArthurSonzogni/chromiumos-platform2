#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the test_case module."""

import pathlib
import tempfile
import unittest

import test_case


class Test_TestCase(unittest.TestCase):
    """Test TestCase."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.temp_toml = pathlib.Path(self.temp_dir.name) / "test.csv"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()
        return super().tearDown()

    def test_load_toml(self):
        self.temp_toml.write_text(
            'name = "TUDisabled"\n'
            'description = "All template updating disabled."\n'
        )
        tc = test_case.test_case_from_toml(self.temp_toml)
        self.assertEqual(tc.name, "TUDisabled")
        self.assertEqual(tc.description, "All template updating disabled.")

    def test_write_toml(self):
        tc = test_case.TestCase(
            name="TUDisabled",
            description="All template updating disabled.",
        )
        tc.to_toml(self.temp_toml)
        self.assertEqual(
            self.temp_toml.read_text(),
            'name = "TUDisabled"\n'
            'description = "All template updating disabled."\n',
        )


if __name__ == "__main__":
    unittest.main()
