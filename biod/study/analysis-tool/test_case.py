# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Represent the results from one particular testing condition."""

from __future__ import annotations

import pathlib

import experiment
import test_case_descriptor


class TestCase:
    """A TestCase represents the results from one particular testing condition.

    It contains the `name`, `description`, and `Experiment` results from that
    testing condition.
    """

    _descriptor: test_case_descriptor.TestCaseDescriptor
    _exp: experiment.Experiment

    def __init__(
        self,
        descriptor: test_case_descriptor.TestCaseDescriptor,
        exp: experiment.Experiment,
    ) -> None:
        self._descriptor = descriptor
        self._exp = exp

    @property
    def name(self) -> str:
        return self._descriptor.name

    @property
    def description(self) -> str:
        return self._descriptor.description

    @property
    def descriptor(self) -> test_case_descriptor.TestCaseDescriptor:
        return self._descriptor

    @property
    def experiment(self) -> experiment.Experiment:
        return self._exp


def test_case_from_dir(dir_path: pathlib.Path) -> TestCase:
    """Parse a TestCase from a directory with all the prerequisite files.

    This function expects there to be a test_case.toml in addition to the files
    needed to parse an `Experiment`.
    """
    if not dir_path.is_dir():
        raise NotADirectoryError(f"{dir_path} is not a directory.")

    test_case_descriptor_toml = dir_path / "test_case.toml"
    if not test_case_descriptor_toml.is_file():
        raise FileNotFoundError(
            f"test case descriptor {test_case_descriptor_toml} does not exist."
        )

    tcd = test_case_descriptor.test_case_descriptor_from_toml(
        test_case_descriptor_toml
    )
    exp = experiment.experiment_from_decisions_dir(dir_path)
    return TestCase(tcd, exp)
