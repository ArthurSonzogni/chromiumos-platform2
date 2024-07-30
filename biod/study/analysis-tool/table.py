#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import enum
from typing import Literal

import pandas as pd


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
DECISION_TABLE_COLS = [
    Col.Enroll_User.value,
    Col.Enroll_Finger.value,
    Col.Verify_User.value,
    Col.Verify_Finger.value,
    Col.Verify_Sample.value,
    Col.Decision.value,
]
DECISION_TABLE_GROUP_COLS = [
    Col.Enroll_Group.value,
    Col.Verify_Group.value,
]
USER_GROUP_TABLE_COLS = [
    Col.User.value,
    Col.Group.value,
]
"""Column names used in a user_group mapping table."""
