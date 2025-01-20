#!/usr/bin/env python3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test the test_case module."""

import pathlib
import tempfile
import unittest

import simulate_fpstudy
import test_case


class Test_TestCase(unittest.TestCase):
    """Test TestCase."""

    def setUp(self) -> None:
        self.temp_dir_context = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir_context.cleanup)
        self.temp_dir = pathlib.Path(self.temp_dir_context.name)
        self.simulation = simulate_fpstudy.SimulateEvalResults(
            num_users=10,
            num_fingers=2,
            num_verify_samples=3,
        )
        self.simulation.GenerateFARResults()
        self.simulation.GenerateFRRResults()

    def test_load(self):
        """Manually save a full test case to a dir and try to load it."""

        self.simulation.Experiment().far_decisions_to_csv(
            self.temp_dir / "FAR_decisions.csv"
        )
        self.simulation.Experiment().frr_decisions_to_csv(
            self.temp_dir / "FRR_decisions.csv"
        )
        self.simulation.TestCaseDescriptor().to_toml(
            self.temp_dir / "test_case.toml"
        )

        tc = test_case.test_case_from_dir(self.temp_dir)
        self.assertEqual(tc.name, self.simulation.TestCaseDescriptor().name)
        self.assertEqual(
            tc.description, self.simulation.TestCaseDescriptor().description
        )

        self.assertTrue(
            tc.experiment.far_decisions().equals(
                self.simulation.Experiment().far_decisions()
            )
        )
        self.assertTrue(
            tc.experiment.frr_decisions().equals(
                self.simulation.Experiment().frr_decisions()
            )
        )


if __name__ == "__main__":
    unittest.main()
