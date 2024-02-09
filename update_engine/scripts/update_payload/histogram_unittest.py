#!/usr/bin/env python
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for histogram.py."""

# Disable check for function names to avoid errors based on old code
# pylint: disable-msg=invalid-name


import unittest

from update_payload import format_utils
from update_payload import histogram


class HistogramTest(unittest.TestCase):
    """Tests histogram"""

    @staticmethod
    def AddHumanReadableSize(size):
        fmt = format_utils.BytesToHumanReadable(size)
        return "%s (%s)" % (size, fmt) if fmt else str(size)

    def CompareToExpectedDefault(self, actual_str):
        expected_str = (
            "Yes |################    | 5 (83.3%)\n"
            "No  |###                 | 1 (16.6%)"
        )
        self.assertEqual(actual_str, expected_str)

    def testExampleHistogram(self):
        self.CompareToExpectedDefault(
            str(histogram.Histogram([("Yes", 5), ("No", 1)]))
        )

    def testFromCountDict(self):
        self.CompareToExpectedDefault(
            str(histogram.Histogram.FromCountDict({"Yes": 5, "No": 1}))
        )

    def testFromKeyList(self):
        self.CompareToExpectedDefault(
            str(
                histogram.Histogram.FromKeyList(
                    ["Yes", "Yes", "No", "Yes", "Yes", "Yes"]
                )
            )
        )

    def testCustomScale(self):
        expected_str = "Yes |#### | 5 (83.3%)\n" "No  |     | 1 (16.6%)"
        actual_str = str(histogram.Histogram([("Yes", 5), ("No", 1)], scale=5))
        self.assertEqual(actual_str, expected_str)

    def testCustomFormatter(self):
        expected_str = (
            "Yes |################    | 5000 (4.8 KiB) (83.3%)\n"
            "No  |###                 | 1000 (16.6%)"
        )
        actual_str = str(
            histogram.Histogram(
                [("Yes", 5000), ("No", 1000)],
                formatter=self.AddHumanReadableSize,
            )
        )
        self.assertEqual(actual_str, expected_str)


if __name__ == "__main__":
    unittest.main()
