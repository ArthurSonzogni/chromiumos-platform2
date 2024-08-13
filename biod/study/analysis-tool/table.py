#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import enum
from typing import Optional

import fpsutils
import pandas as pd


class MatchType(enum.Enum):
    """Represents what type of natch attempt.

    This may be a `Genuine` match with a finger against its own template (FRR)
    or an `Imposter` match between a finger and a non-matching template (FAR).
    """

    Genuine = "Genuine"
    Imposter = "Imposter"


class Finger(enum.Enum):
    Thumb_Left = 0
    Thumb_Right = 1
    Index_Left = 2
    Index_Right = 3
    Middle_Left = 4
    Middle_Right = 5


class UserGroup(enum.Enum):
    A = 0
    B = 1
    C = 2
    D = 3
    E = 4
    F = 5


class Decision(enum.Enum):
    Accept = "ACCEPT"
    Reject = "REJECT"


class Col(enum.Enum):
    """All known table column names used across different table types."""

    Enroll_User = "EnrollUser"
    Enroll_Finger = "EnrollFinger"
    Enroll_Group = "EnrollGroup"
    Verify_User = "VerifyUser"
    Verify_Finger = "VerifyFinger"
    Verify_Sample = "VerifySample"
    Verify_Group = "VerifyGroup"
    Decision = "Decision"
    User = "User"
    Group = "Group"

    @classmethod
    def all(cls) -> list[enum.Enum]:
        return list(level for level in cls)

    @classmethod
    def all_values(cls) -> list[str]:
        return list(level.value for level in cls)


FALSE_TABLE_COLS = [
    Col.Enroll_User.value,
    Col.Enroll_Finger.value,
    Col.Verify_User.value,
    Col.Verify_Finger.value,
    Col.Verify_Sample.value,
]

USER_GROUP_TABLE_COLS = [
    Col.User.value,
    Col.Group.value,
]
"""Column names used in a user_group mapping table."""


class Table(pd.DataFrame):
    """The baseclass for all special tables."""

    def _check_duplicates(self):
        """Check that there are no duplicate rows."""

        dup_rows_bool: pd.Series[bool] = self.duplicated()
        dup_rows_count = sum(dup_rows_bool)
        if dup_rows_count > 0:
            print(f"Found {dup_rows_count} duplicate rows in the table.")
            print("Example:")
            # Pass array to iloc to ensure it prints out as table row,
            # instead of vertical single item series.
            print(self[dup_rows_bool].iloc[0 : min(5, dup_rows_count)])
            raise ValueError("table contains duplicate rows")


class Decisions(Table):
    """Holds a decisions table."""

    COLS: list[str] = [
        Col.Enroll_User.value,
        Col.Enroll_Finger.value,
        Col.Verify_User.value,
        Col.Verify_Finger.value,
        Col.Verify_Sample.value,
        Col.Decision.value,
    ]
    """The minimum required columns for a decision table."""

    GROUP_COLS: list[str] = [
        Col.Enroll_Group.value,
        Col.Verify_Group.value,
    ]
    """The optional group columns."""

    def filter_match_attempts(self, match_type: MatchType) -> Decisions:
        """Return match attempts from the table that are for FA or FR."""

        genuine_attempts_bools = (
            self[Col.Enroll_User.value] == self[Col.Verify_User.value]
        ) & (self[Col.Enroll_Finger.value] == self[Col.Verify_Finger.value])

        if match_type == MatchType.Genuine:
            return Decisions(self[genuine_attempts_bools])
        elif match_type == MatchType.Imposter:
            return Decisions(self[~genuine_attempts_bools])

    def _check_cols(self):
        """Check basic decisions table properties, like columns.

        1. Check that the minimum column name set exists.
        2. Check that either both group columns are correctly absent or
            that they are both are present.
        """
        if not fpsutils.has_columns(self, Decisions.COLS):
            raise TypeError("table is missing one or more required columns")

        # If one of the Enroll/Verify group columns is present, then the
        # other must be present.
        grps = set(self.columns) & set(Decisions.GROUP_COLS)
        if len(grps) == 1:
            raise TypeError("table is missing one group column")

    def check(
        self,
        duplicates: bool = False,
        expected_match_type: Optional[MatchType] = None,
    ):
        """Check for consistency within a single decisions table.

        The default arguments are intended to be very fast to allow checking
        in all contexts. If more time is available, enable the other checks
        to ensure that the data is what we expect.

        - A decision table must have the correct column labels.
        - A decisions table should either not have any group information or
          both Enroll and Verify group columns must exist.
        - A decisions table should not contain duplicate rows.
        - An imposter/FAR table should not include genuine/FRR matches.
        - An genuine/FRR table should not include imposter/FAR matches.

        Args:
            duplicates: If enabled, additionally check for duplicate rows
            expected_match_type: If specified, additionally check that all
                match attempts are of the expected match type only.

        Raises an exception if an inconsistency is found.
        """

        self._check_cols()

        if duplicates:
            self._check_duplicates()

        if expected_match_type is MatchType.Genuine:
            # FRR table should not contain any imposter matches, where the
            # Verify User+Finger doesn't equal Enroll USer+Finger.

            bad_fr_attempts = self.filter_match_attempts(MatchType.Imposter)
            if len(bad_fr_attempts) > 0:
                print(
                    f"Found {len(bad_fr_attempts)} FAR match attempts in FRR "
                    "decisions table."
                )
                print("Example:")
                print(bad_fr_attempts.iloc[[0]])
                raise ValueError("FRR table contains imposter match attempts.")
        elif expected_match_type is MatchType.Imposter:
            # FAR table should not contain any match attempts against the
            # finger's own template, where Enroll User+Finger equals
            # Verify User+Finger.

            bad_fa_attempts = self.filter_match_attempts(MatchType.Genuine)
            if len(bad_fa_attempts) > 0:
                print(
                    f"Found {len(bad_fa_attempts)} FRR match attempts in FAR "
                    "decisions table."
                )
                print("Example:")
                # Pass array to iloc to ensure it prints out as table row,
                # instead of vertical single item series.
                print(bad_fa_attempts.iloc[[0]])
                raise ValueError("FAR table contains genuine match attempts.")
