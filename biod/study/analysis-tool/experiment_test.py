#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the experiment module."""

import pathlib
import tempfile
import textwrap
import unittest

from experiment import Experiment
import fpsutils
import pandas as pd
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


class Test_Experiment_CSV(unittest.TestCase):
    """Test the CSV reading and writing capabilities of `Experiment`."""

    CSV_FAR = textwrap.dedent(
        """\
        EnrollUser,EnrollFinger,VerifyUser,VerifyFinger,VerifySample,Decision
        10001,1,10002,3,12,REJECT
        10001,1,10002,3,13,REJECT
        """
    )
    CSV_FAR_DATAFRAME = pd.DataFrame(
        {
            Experiment.TableCol.Enroll_User.value: [10001, 10001],
            Experiment.TableCol.Enroll_Finger.value: [1, 1],
            Experiment.TableCol.Verify_User.value: [10002, 10002],
            Experiment.TableCol.Verify_Finger.value: [3, 3],
            Experiment.TableCol.Verify_Sample.value: [12, 13],
            Experiment.TableCol.Decision.value: [
                Experiment.Decision.Reject.value,
                Experiment.Decision.Reject.value,
            ],
        }
    )

    CSV_FRR = textwrap.dedent(
        """\
        EnrollUser,EnrollFinger,VerifyUser,VerifyFinger,VerifySample,Decision
        10001,1,10001,1,25,ACCEPT
        10001,2,10001,2,25,ACCEPT
        """
    )
    CSV_FRR_DATAFRAME = pd.DataFrame(
        {
            Experiment.TableCol.Enroll_User.value: [10001, 10001],
            Experiment.TableCol.Enroll_Finger.value: [1, 2],
            Experiment.TableCol.Verify_User.value: [10001, 10001],
            Experiment.TableCol.Verify_Finger.value: [1, 2],
            Experiment.TableCol.Verify_Sample.value: [25, 25],
            Experiment.TableCol.Decision.value: [
                Experiment.Decision.Accept.value,
                Experiment.Decision.Accept.value,
            ],
        }
    )

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.temp_csv = pathlib.Path(self.temp_dir.name) / "test.csv"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()
        return super().tearDown()

    def test_read_far(self):
        exp = Experiment(num_verification=0, num_fingers=0, num_users=0)

        self.temp_csv.write_text(self.CSV_FAR)
        exp.add_far_decisions_from_csv(self.temp_csv)
        df = exp.far_decisions()
        self.assertTrue(df.equals(self.CSV_FAR_DATAFRAME))

    def test_read_frr(self):
        exp = Experiment(num_verification=0, num_fingers=0, num_users=0)

        self.temp_csv.write_text(self.CSV_FRR)
        exp.add_frr_decisions_from_csv(self.temp_csv)
        df = exp.frr_decisions()
        self.assertTrue(df.equals(self.CSV_FRR_DATAFRAME))

    def test_write_far(self):
        exp = Experiment(
            num_verification=0,
            num_fingers=0,
            num_users=0,
            far_decisions=self.CSV_FAR_DATAFRAME,
        )
        self.temp_csv.unlink(missing_ok=True)
        exp.far_decisions_to_csv(self.temp_csv)
        self.assertEqual(self.temp_csv.read_text(), self.CSV_FAR)

    def test_write_frr(self):
        exp = Experiment(
            num_verification=0,
            num_fingers=0,
            num_users=0,
            frr_decisions=self.CSV_FRR_DATAFRAME,
        )
        self.temp_csv.unlink(missing_ok=True)
        exp.frr_decisions_to_csv(self.temp_csv)
        self.assertEqual(self.temp_csv.read_text(), self.CSV_FRR)


if __name__ == "__main__":
    unittest.main()
