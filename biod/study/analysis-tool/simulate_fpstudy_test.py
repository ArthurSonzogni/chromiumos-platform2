#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test simulate_fpstudy module."""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

from experiment import Experiment
import simulate_fpstudy as sim


class Test_simulate_fpstudy(unittest.TestCase):
    """Test functionality in simulate_fpstudy module."""

    def setUp(self) -> None:
        """Run before each test."""
        self.verbose = ("-v" in sys.argv) or ("--verbose" in sys.argv)
        self.simulation = sim.SimulateEvalResults(
            num_users=72,
            num_fingers=6,
            num_verify_samples=80,
        )
        return super().setUp()

    def test_GenerateFARResults(self):
        self.simulation.GenerateFARResults(verbose=self.verbose)
        far_tbl = self.simulation.Experiment().far_decisions()
        # There are two equal ways to count FA attempts:
        # - For all verification samples (users * fingers * samples) we can
        #   attempt to match them with all other finger templates
        #   (users*fingers), other than the current user-finger being verified
        #   in the sample (users*fingers-1). So, we have
        #   (users * fingers * samples) * (users * fingers - 1) attempts.
        self.assertEqual(len(far_tbl), (72 * 6 * 80) * (72 * 6 - 1))
        # - For all user-finger templates (users*fingers), we can attempt to
        #   match against all verification samples, other than the current
        #   user-finger template being used (samples * (users*fingers-1)).
        #   So, we have (users*fingers) * (samples * (users*fingers-1))
        #   attempts.
        self.assertEqual(len(far_tbl), (72 * 6) * (80 * (72 * 6 - 1)))
        # These two expressions are trivially equal by way of the associativity
        # property of multiplication.

    def test_GenerateFRRResults(self):
        self.simulation.GenerateFRRResults(verbose=self.verbose)
        frr_tbl = self.simulation.Experiment().frr_decisions()
        # For all user-finger enrollment templates (users*fingers), we attempt
        # to match them with their verification samples (samples). So, we have
        # users*fingers*samples attempts.
        self.assertEqual(len(frr_tbl), 72 * 6 * 80)

    def test_GenerateUserGroup(self):
        self.simulation.GenerateUserGroup(verbose=self.verbose)
        user_groups_tbl = self.simulation.Experiment().user_groups_table()
        self.assertIsNotNone(user_groups_tbl)
        assert user_groups_tbl is not None  # For static type checkers.
        self.assertEqual(len(user_groups_tbl), 72)


class Test_simulate_fpstudy_main(unittest.TestCase):
    """Test the main / command-line interface of simulate_fpstudy."""

    FAST = ["--fingers=2", "--samples=2"]
    """These options don't impact test coverage, but will dramatically speed
    up unit tests."""

    def setUp(self) -> None:
        """Run before each test."""
        self.verbose = ("-v" in sys.argv) or ("--verbose" in sys.argv)
        self.temp_dir = tempfile.TemporaryDirectory()
        self.temp_dir_path = pathlib.Path(self.temp_dir.name)
        # Optionally set the verbose flag.
        self.v: list[str] = ["--verbose"] if self.verbose else []

    def tearDown(self) -> None:
        """Run after each test."""
        self.temp_dir.cleanup()
        return super().tearDown()

    def test_with_groups(self):
        """Run the utility with groups enabled."""
        args = [str(self.temp_dir_path)] + self.FAST + self.v
        sim.main(args)
        self.assertTrue((self.temp_dir_path / "FAR_decisions.csv").is_file())
        self.assertTrue((self.temp_dir_path / "FRR_decisions.csv").is_file())
        self.assertTrue((self.temp_dir_path / "User_groups.csv").is_file())

    def test_without_groups(self):
        """Run the utility with groups disabled."""
        args = [str(self.temp_dir_path), "--groups=0"] + self.FAST + self.v
        sim.main(args)
        self.assertTrue((self.temp_dir_path / "FAR_decisions.csv").is_file())
        self.assertTrue((self.temp_dir_path / "FRR_decisions.csv").is_file())
        self.assertFalse((self.temp_dir_path / "User_groups.csv").is_file())

    def test_experiment_can_import(self):
        """Tests whether Experiment can import the results."""
        args = [str(self.temp_dir_path), "--fingers=2", "--samples=2"] + self.v
        sim.main(args)
        exp = Experiment(
            num_verification=2,
            num_users=72,
            num_fingers=2,
        )
        exp.add_far_decisions_from_csv(self.temp_dir_path / "FAR_decisions.csv")
        exp.add_frr_decisions_from_csv(self.temp_dir_path / "FRR_decisions.csv")
        exp.add_groups_from_csv(self.temp_dir_path / "User_groups.csv")
        exp.far_decisions()
        exp.frr_decisions()
        exp.user_groups_table()

    def test_with_invalid_output_dir(self):
        """Provide a file as the output path."""
        file_path = self.temp_dir_path / "file.csv"
        file_path.touch()

        with self.assertRaises(SystemExit):
            sim.main([str(self.temp_dir_path / "file.csv")])

    def test_with_invalid_frr_prob(self):
        """Provide FRR probability of more than 100%."""
        with self.assertRaises(SystemExit):
            sim.main([str(self.temp_dir_path), "--frr_prob_percent=101"])

    def test_with_invalid_frr_prob2(self):
        """Provide FRR probability of less than 0%."""
        with self.assertRaises(SystemExit):
            sim.main([str(self.temp_dir_path), "--frr_prob_percent=-1"])

    def test_with_invalid_far_prob(self):
        """Provide FAR probability divisor of less than 0."""
        with self.assertRaises(SystemExit):
            sim.main([str(self.temp_dir_path), "--far_prob_divisor=-1"])


if __name__ == "__main__":
    unittest.main()
