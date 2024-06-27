#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Add an interface class for defining TestCase Enums."""

from __future__ import annotations

from enum import Enum

from test_case import TestCase


class TestCaseEnum(Enum):
    # The `value` should be a tuple with first value being the description.
    # Any additional info can be placed in extra tuple positions.
    #
    # Example:
    # One = ("One's description", ...)

    @classmethod
    def all(cls) -> list[TestCaseEnum]:
        return [level for level in cls]

    @classmethod
    def all_values(cls, s=slice(None)) -> list[str]:
        return [level.value[s] for level in cls]

    def __str__(self) -> str:
        # Without this method, the str(self) would contain the full type
        # prefix, like SubClassTestCaseEnum.One.
        return self.name

    def description(self) -> str:
        """Get the description for the given test case."""
        assert type(self.value) == tuple
        assert len(self.value) >= 1
        assert type(self.value[0]) == str
        return self.value[0]

    def extra(self) -> tuple:
        """Get extra value tuple info."""
        assert type(self.value) == tuple
        assert len(self.value) >= 1
        assert type(self.value[0]) == str
        return self.value[1:]

    def test_case(self) -> TestCase:
        """Return the general TestCase for this TestCaseEnum."""
        return TestCase(self.name, self.description())
