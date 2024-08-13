#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test the table module."""

import unittest

import numpy as np
import pandas as pd
import table


class Test_Decisions_check(unittest.TestCase):
    """Test the consistency `check` function of `Decisions`."""

    FAR_DATAFRAME = pd.DataFrame(
        {
            table.Col.Enroll_User.value: [10001] * 10,
            table.Col.Enroll_Finger.value: [1] * 10,
            table.Col.Verify_User.value: [10002] * 10,
            table.Col.Verify_Finger.value: [1] * 10,
            table.Col.Verify_Sample.value: np.arange(0, 10),
            table.Col.Decision.value: [table.Decision.Reject.value] * 10,
            table.Col.Enroll_Group.value: ["A"] * 10,
            table.Col.Verify_Group.value: ["B"] * 10,
        }
    )

    FRR_DATAFRAME = pd.DataFrame(
        {
            table.Col.Enroll_User.value: [10001, 10002],
            table.Col.Enroll_Finger.value: [1, 2],
            table.Col.Verify_User.value: [10001, 10002],
            table.Col.Verify_Finger.value: [1, 2],
            table.Col.Verify_Sample.value: [25, 25],
            table.Col.Decision.value: [
                table.Decision.Accept.value,
                table.Decision.Accept.value,
            ],
            table.Col.Enroll_Group.value: ["A", "B"],
            table.Col.Verify_Group.value: ["A", "B"],
        }
    )

    def test_correct_with_groups(self):
        """Test the check method when both FAR and FRR tables are correct."""
        far_decisions = table.Decisions(self.FAR_DATAFRAME)
        frr_decisions = table.Decisions(self.FRR_DATAFRAME)
        far_decisions.check()
        frr_decisions.check()

    def test_correct_without_groups(self):
        """Test the check method when both FAR and FRR tables are correct."""
        far_decisions = table.Decisions(
            self.FAR_DATAFRAME.drop(columns=table.Decisions.GROUP_COLS)
        )
        frr_decisions = table.Decisions(
            self.FRR_DATAFRAME.drop(columns=table.Decisions.GROUP_COLS)
        )
        far_decisions.check()
        frr_decisions.check()

    def test_partial_groups(self):
        """Test that only one group column existing is detected."""
        far_decisions = table.Decisions(
            self.FAR_DATAFRAME.drop(columns=[table.Col.Enroll_Group.value])
        )
        with self.assertRaises(TypeError):
            far_decisions.check()

    def test_far_table_contains_frr(self):
        """Test that FRR attempts are detected in FAR table."""
        far_decisions = table.Decisions(
            pd.concat(
                [self.FAR_DATAFRAME, self.FRR_DATAFRAME],
                ignore_index=True,
            )
        )
        with self.assertRaises(ValueError):
            far_decisions.check(expected_match_type=table.MatchType.Imposter)

    def test_frr_table_contains_far(self):
        """Test that FAR attempts are detected in FRR table."""

        frr_decisions = table.Decisions(
            pd.concat(
                [self.FRR_DATAFRAME, self.FAR_DATAFRAME],
                ignore_index=True,
            )
        )
        with self.assertRaises(ValueError):
            frr_decisions.check(expected_match_type=table.MatchType.Genuine)

    def test_table_contains_duplicates(self):
        far_decisions = table.Decisions(
            pd.concat(
                [
                    self.FAR_DATAFRAME,
                    # Append duplicates of the first 4 rows, with the first row
                    # appearing as a duplicate twice. This allows for manual
                    # verification that only unique duplicates are shown as
                    # examples to the user.
                    self.FAR_DATAFRAME.iloc[0:1],
                    self.FAR_DATAFRAME.iloc[0:4],
                ],
                ignore_index=True,
            )
        )

        with self.assertRaises(ValueError):
            far_decisions.check(duplicates=True)


if __name__ == "__main__":
    unittest.main()
