# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Add an interface class for defining TestCase Enums."""

from __future__ import annotations

import dataclasses
import pathlib
import sys
import textwrap


# Python 3.11 introduced the following native TOML parsing library:
# https://docs.python.org/3.11/library/tomllib.html
if sys.version_info >= (3, 11):
    import tomllib
else:
    # The tomli library seems to be the library that was integrated into
    # Python 3.11, from the https://github.com/hukkin/tomli#intro note.
    # We will fallback to using this library.
    import tomli as tomllib


@dataclasses.dataclass(frozen=True)
class TestCase:
    """Describes the name and description for a TestCase."""

    name: str
    description: str

    def __str__(self) -> str:
        return self.name

    def to_toml(self, file_path: pathlib.Path):
        """Write the TestCaseDescriptor to a TOML file.

        Args:
            file_path: Path to the output TOML file.
        """
        toml = textwrap.dedent(
            f"""\
            name = "{self.name}"
            description = "{self.description}"
            """
        )
        file_path.write_text(toml)


def test_case_from_toml(file_path: pathlib.Path) -> TestCase:
    """Parse a TestCaseDescriptor from a TOML file.

    Args:
        file_path: Path to the TOML file.

    Returns:
        TestCaseDescriptor object.

    We force both `name` and `description` to exist in the file, to avoid
    unnoticeable errors. If the user really wants to temporarily bypass
    `name` and `description`, they can set them to `""` in the TOML file.
    """
    with open(file_path, "rb") as f:
        parsed_toml = tomllib.load(f)

    def parsed_str(key: str) -> str:
        """Get the `key` as exactly an `str`."""
        value = parsed_toml.get(key)
        if value is None:
            raise ValueError(f"Missing '{key}' key in {file_path}")
        if not isinstance(value, str):
            raise ValueError(
                f"The '{key}' key in {file_path} must be a "
                f"string, but found {type(value)}."
            )
        return value

    return TestCase(parsed_str("name"), parsed_str("description"))
