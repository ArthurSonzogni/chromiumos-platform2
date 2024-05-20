#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the experiment module."""

import unittest

from experiment import Experiment
import fpsutils
import simulate_fpstudy


class Test_fa_query(unittest.TestCase):
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
        self.exp = Experiment(
            num_users=72,
            num_fingers=3,
            num_verification=40,
            far_decisions=self.far_decisions,
        )

    def test_benchmark_fa_query1(self):
        print("Query 1 Timing")

        fpsutils.benchmark(
            "self.exp.fa_query(verify_user_id=10071)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query(verify_user_id=10071, verify_finger_id=0)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057, enroll_finger_id=4)",
            globals={**globals(), **locals()},
        )

    def test_benchmark_fa_query2(self):
        print("Query 2 Timing")

        fpsutils.benchmark(
            "self.exp.fa_query2(verify_user_id=10071)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query2(verify_user_id=10071, verify_finger_id=0)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057)",
            globals={**globals(), **locals()},
        )
        fpsutils.benchmark(
            "self.exp.fa_query2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057, enroll_finger_id=4)",
            globals={**globals(), **locals()},
        )


if __name__ == "__main__":
    unittest.main()
