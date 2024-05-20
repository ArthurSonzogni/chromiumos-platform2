#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the test_case module."""

import unittest

import test_case


class Test_test_case(unittest.TestCase):
    class TestTestCase(test_case.TestCase):
        One = ("One's Description", "path/to/one")
        Two = ("Two's Description", "path/to/two", "meta info")
        Three = ("Three's Description",)

    def setUp(self) -> None:
        self.tc_one = self.TestTestCase.One
        self.tc_two = self.TestTestCase.Two
        self.tc_three = self.TestTestCase.Three
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
            self.TestTestCase.all(),
            [
                self.TestTestCase.One,
                self.TestTestCase.Two,
                self.TestTestCase.Three,
            ],
        )

    def test_all_values(self):
        self.assertEqual(
            self.TestTestCase.all_values(),
            [
                ("One's Description", "path/to/one"),
                ("Two's Description", "path/to/two", "meta info"),
                ("Three's Description",),
            ],
        )
        self.assertEqual(
            self.TestTestCase.all_values(slice(1, None)),
            [("path/to/one",), ("path/to/two", "meta info"), ()],
        )

    def test_str(self):
        self.assertEqual(str(self.tc_one), "One")
        self.assertEqual(str(self.tc_two), "Two")
        self.assertEqual(str(self.tc_three), "Three")


if __name__ == "__main__":
    unittest.main()
