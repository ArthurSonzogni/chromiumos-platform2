# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test the main function of crosid."""

import subprocess

import pytest


def test_help(executable_path):
    result = subprocess.run(
        [executable_path, "--help"],
        check=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.stderr.startswith("Usage: ")
    assert result.stdout == ""


@pytest.mark.parametrize(
    "argv",
    (
        ["badarg"],
        ["-u"],
        ["--help", "badarg"],
        ["--verbose", "--help", "-u"],
    ),
)
def test_bad_args(executable_path, argv):
    # pylint: disable=subprocess-run-check
    result = subprocess.run(
        [executable_path, *argv],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.returncode != 0
    assert result.stderr.startswith("Unknown argument: ")
    assert result.stdout == ""
