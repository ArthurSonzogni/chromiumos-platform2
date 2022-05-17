#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Add an interface class for defining TestCase Enums."""

from __future__ import annotations

from enum import Enum
from typing import List, Tuple


class TestCase(Enum):
    ...
    # The `value` should be a tuple with first value being the description.
    # Any additional info can be placed in extra tuple positions.
    #
    # Example:
    # One = ("One's description", ...)

    @classmethod
    def all(cls) -> List[TestCase]:
        return [level for level in cls]

    @classmethod
    def all_values(cls, s=slice(None)) -> List[str]:
        return [level.value[s] for level in cls]

    def __str__(self) -> str:
        return self.name

    def description(self) -> str:
        """Get the description for the given test case."""
        assert type(self.value) == tuple
        assert len(self.value) >= 1
        assert type(self.value[0]) == str
        return self.value[0]

    def extra(self) -> Tuple:
        """Get extra value tuple info."""
        assert type(self.value) == tuple
        assert len(self.value) >= 1
        assert type(self.value[0]) == str
        return self.value[1:]
