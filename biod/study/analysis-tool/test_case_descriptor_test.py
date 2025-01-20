#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the test_case_descriptor module."""

import pathlib
import tempfile
import unittest

import test_case_descriptor


class Test_TestCaseDescriptor(unittest.TestCase):
    """Test TestCaseDescriptor."""

    def setUp(self) -> None:
        # The point of opening the temp dir here is to limit boilerplate code
        # in each of the following tests, thus we can't use "with" to scope.
        # pylint: disable=consider-using-with
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.temp_toml = pathlib.Path(self.temp_dir.name) / "test_case.toml"

    def test_descriptor_reliable_hash(self):
        """Ensure that a TestCaseDescriptor can be reliably hashed for keys."""
        tcd1 = test_case_descriptor.TestCaseDescriptor("TC1", "TC1 desc")
        tcd1_prime = test_case_descriptor.TestCaseDescriptor("TC1", "TC1 desc")
        self.assertEqual(tcd1, tcd1_prime)
        self.assertEqual(hash(tcd1), hash(tcd1_prime))
        self.assertNotEqual(id(tcd1), id(tcd1_prime))

    def test_descriptor_reliable_dict_key(self):
        """Ensure that a TestCaseDescriptor can be used as a stable dict key."""
        tcd1 = test_case_descriptor.TestCaseDescriptor("TC1", "TC1 desc")

        d = {tcd1: 1}
        d[tcd1] += 1
        d[test_case_descriptor.TestCaseDescriptor("TC1", "TC1 desc")] += 1
        self.assertEqual(d[tcd1], 3)

    def test_load_toml(self):
        self.temp_toml.write_text(
            'name = "TUDisabled"\n'
            'description = "All template updating disabled."\n'
        )
        tc = test_case_descriptor.test_case_descriptor_from_toml(self.temp_toml)
        self.assertEqual(tc.name, "TUDisabled")
        self.assertEqual(tc.description, "All template updating disabled.")

    def test_write_toml(self):
        tc = test_case_descriptor.TestCaseDescriptor(
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
