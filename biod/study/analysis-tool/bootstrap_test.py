#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the Bootstrap module."""

import math
import pickle
import unittest

from bootstrap import BootstrapFullFARHierarchy
from experiment import Experiment
import fpsutils
import simulate_fpstudy


class Test_Bootstrap(unittest.TestCase):
    def setUp(self):
        # VerifyUser VerifyFinger VerifySample EnrollUser EnrollFinger Decision
        # This can take about 2 seconds.
        self.far_decisions = simulate_fpstudy.GenerateFARResults(
            num_users=72,
            num_fingers=3,
            num_verify_samples=40,
            user_groups=None,
            verbose=True,
        )
        self.exp = Experiment(far_decisions=self.far_decisions)

    def test_benchmark(self):
        """Test how long it takes to run the sampling using different modes."""

        boot = BootstrapFullFARHierarchy(self.exp, verbose=True)
        boot.USE_GLOBAL_SHARING = True
        boot.run(num_samples=5000, num_proc=0)

    def test_benchmark_pickling(self):
        """Test how long it takes to pickle the Bootstrap object.

        Pickling can impact the performance of multiprocessing very heavily.
        Pickling could span between nanoseconds to seconds (or longer),
        depending on what data is contained in the object.

        Note that using `Bootstrap.USE_GLOBAL_SHARING` can avoid the high
        cost of pickling.

        Run this benchmark on your new Bootstrap sample to ensure that the
        bootstrap sample object is reasonably size.
        """

        HYPOTHETICAL_NUM_CORES = [4, 64, 128]
        HYPOTHETICAL_NUM_SAMPLES = [5000, 10000, 100000]

        boot = BootstrapFullFARHierarchy(self.exp)
        fpsutils.benchmark(
            "vals = pickle.loads(pickle.dumps(123456))",
            global_vars={**locals(), **globals()},
        )
        _, _, pkl_time = fpsutils.benchmark(
            "pickle.dumps(boot)", global_vars={**locals(), **globals()}
        )
        pkl_data = pickle.dumps(boot)
        fpsutils.fake_use(pkl_data)
        _, _, unpkl_time = fpsutils.benchmark(
            "pickle.loads(pkl_data)", global_vars={**locals(), **globals()}
        )

        print(
            "Total pickling time is "
            f"{fpsutils.elapsed_time_str(pkl_time + unpkl_time)}."
        )

        for c in HYPOTHETICAL_NUM_CORES:
            for s in HYPOTHETICAL_NUM_SAMPLES:
                eta_calc = pkl_time + (
                    unpkl_time * math.ceil(float(s) / float(c))
                )
                eta = fpsutils.elapsed_time_str(eta_calc)
                print(
                    f"Processing {s} samples running over {c},",
                    f"could take {eta} in pickling.",
                )

        print(
            "In reality, there is some other mechanism that seems "
            "to increase runtime with increasing number of processes, when "
            "the map interable objects are large."
        )


if __name__ == "__main__":
    unittest.main()
