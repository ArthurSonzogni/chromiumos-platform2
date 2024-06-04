#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the experiment module."""

import pathlib
import tempfile
import textwrap
import unittest

from experiment import *
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

    CSV_USER_GROUP = textwrap.dedent(
        """\
        User,Group
        10001,A
        10002,B
        """
    )
    CSV_USER_GROUP_DATAFRAME = pd.DataFrame(
        {
            Experiment.TableCol.User.value: [10001, 10002],
            Experiment.TableCol.Group.value: ["A", "B"],
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

    def test_read_user_group(self):
        exp = Experiment(num_verification=0, num_fingers=0, num_users=0)

        self.temp_csv.write_text(self.CSV_USER_GROUP)
        exp.add_groups_from_csv(self.temp_csv)
        df = exp.user_groups_table()
        self.assertTrue(df.equals(self.CSV_USER_GROUP_DATAFRAME))

    def test_write_user_group(self):
        exp = Experiment(num_verification=0, num_fingers=0, num_users=0)

        exp.add_groups(self.CSV_USER_GROUP_DATAFRAME)
        self.temp_csv.unlink(missing_ok=True)
        exp.user_groups_table_to_csv(self.temp_csv)
        self.assertEqual(self.temp_csv.read_text(), self.CSV_USER_GROUP)


class Test_Experiment_User_Groups(unittest.TestCase):
    """Test the User Group capabilities of `Experiment`."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.temp_csv = pathlib.Path(self.temp_dir.name) / "test.csv"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()
        return super().tearDown()

    FRR_DATAFRAME_GROUPS = pd.DataFrame(
        {
            Experiment.TableCol.Enroll_User.value: [10001, 10002],
            Experiment.TableCol.Enroll_Finger.value: [1, 2],
            Experiment.TableCol.Verify_User.value: [10001, 10002],
            Experiment.TableCol.Verify_Finger.value: [1, 2],
            Experiment.TableCol.Verify_Sample.value: [25, 25],
            Experiment.TableCol.Decision.value: [
                Experiment.Decision.Accept.value,
                Experiment.Decision.Accept.value,
            ],
            # With Enroll and Verify groups.
            Experiment.TableCol.Enroll_Group.value: ["A", "B"],
            Experiment.TableCol.Verify_Group.value: ["A", "B"],
        }
    )

    FAR_DATAFRAME_GROUPS = pd.DataFrame(
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
            # With Enroll and Verify groups.
            Experiment.TableCol.Enroll_Group.value: ["A", "A"],
            Experiment.TableCol.Verify_Group.value: ["B", "B"],
        }
    )
    """This example requires the groups to be scanned from both enroll and verify."""

    USER_GROUPS_TABLE = pd.DataFrame(
        {
            Experiment.TableCol.User.value: [10001, 10002],
            Experiment.TableCol.Group.value: ["A", "B"],
        }
    )

    def test_infer_groups_from_frr(self):
        """Test whether the user_groups can be scanned from the FRR table."""
        exp = Experiment(num_verification=0, num_fingers=0, num_users=0)

        exp.add_frr_decisions(self.FRR_DATAFRAME_GROUPS)
        user_groups = exp.user_groups_table()
        self.assertIsNotNone(user_groups)
        self.assertTrue(user_groups.equals(self.USER_GROUPS_TABLE))

    def test_infer_groups_from_far(self):
        """Test whether the user_groups can be scanned from the FAR table.

        Additionally, this checks whether groups are detected from both enroll
        and verify columns.
        """
        exp = Experiment(num_verification=0, num_fingers=0, num_users=0)

        exp.add_far_decisions(self.FAR_DATAFRAME_GROUPS)
        user_groups = exp.user_groups_table()
        self.assertIsNotNone(user_groups)
        self.assertTrue(user_groups.equals(self.USER_GROUPS_TABLE))


if __name__ == "__main__":
    unittest.main()
