#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for the test_case_enum module."""

import unittest

import test_case_enum


class Test_TestCaseEnum(unittest.TestCase):
    """Test the TestCaseEnum class."""

    class TestTestCaseEnum(test_case_enum.TestCaseEnum):
        """A sample TestCaseEnum class to test against."""

        One = ("One's Description", "path/to/one")
        Two = ("Two's Description", "path/to/two", "meta info")
        Three = ("Three's Description",)

    def setUp(self) -> None:
        self.tc_one = self.TestTestCaseEnum.One
        self.tc_two = self.TestTestCaseEnum.Two
        self.tc_three = self.TestTestCaseEnum.Three
        return super().setUp()

    def test_description(self):
        self.assertEqual(self.tc_one.description(), "One's Description")
        self.assertEqual(self.tc_two.description(), "Two's Description")
        self.assertEqual(self.tc_three.description(), "Three's Description")

    def test_tuple_extra(self):
        self.assertEqual(self.tc_one.extra(), ("path/to/one",))
        self.assertEqual(self.tc_two.extra(), ("path/to/two", "meta info"))
        self.assertEqual(self.tc_three.extra(), ())

    def test_all(self):
        self.assertEqual(
            self.TestTestCaseEnum.all(),
            [
                self.TestTestCaseEnum.One,
                self.TestTestCaseEnum.Two,
                self.TestTestCaseEnum.Three,
            ],
        )

    def test_all_values(self):
        self.assertEqual(
            self.TestTestCaseEnum.all_values(),
            [
                ("One's Description", "path/to/one"),
                ("Two's Description", "path/to/two", "meta info"),
                ("Three's Description",),
            ],
        )
        self.assertEqual(
            self.TestTestCaseEnum.all_values(slice(1, None)),
            [("path/to/one",), ("path/to/two", "meta info"), ()],
        )

    def test_str(self):
        self.assertEqual(str(self.tc_one), "One")
        self.assertEqual(str(self.tc_two), "Two")
        self.assertEqual(str(self.tc_three), "Three")


if __name__ == "__main__":
    unittest.main()
