#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to generate simulated performance evaluation tool results."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time
from typing import Optional

from experiment import Experiment
import numpy as np
import pandas as pd


def GenerateUserGroup(
    num_users: int = 72,
    user_groups: list[str] = ["A", "B", "C", "D", "E", "F"],
) -> pd.DataFrame:
    """Generate simulated user group mapping table."""

    users = np.arange(10001, 10001 + num_users)
    groups = np.repeat(user_groups, repeats=num_users // len(user_groups))

    num_user_remainder = num_users % len(user_groups)
    if num_user_remainder != 0:
        print(
            "WARNING - The number users cannot be evenly split into"
            f" {len(user_groups)} groups. We will add additional unbalanced"
            " groups.",
            file=sys.stderr,
        )
        groups = np.append(groups, user_groups[0:num_user_remainder])

    user_groups_tbl = pd.DataFrame(
        {
            Experiment.TableCol.User.value: users,
            Experiment.TableCol.Group.value: groups,
        }
    )
    return user_groups_tbl


def GenerateFARResults(
    num_users: int = 72,
    num_fingers: int = 6,
    num_verify_samples: int = 80,
    user_groups: Optional[list[str]] = ["A", "B", "C", "D", "E", "F"],
    prob: float = 1 / 100000.0,
    verbose: bool = False,
) -> pd.DataFrame:
    """Generate simulated FAR results."""
    time_start = time.perf_counter()

    users = np.arange(10001, 10001 + num_users)
    fingers = np.arange(num_fingers)
    samples = np.arange(num_verify_samples)

    # This includes match attempts between the same finger for enrollment and
    # verification. We need this for initial dataframe sizing, before we prune
    # the invalid matches.
    num_rows_naive = (num_users * num_fingers * num_verify_samples) * (
        num_users * num_fingers
    )

    # This is the correct number of total FAR matches.
    #
    # For all user-finger templates (users*fingers), we can attempt to
    # match against all verification samples, other than the current
    # user-finger template being used (samples * (users*fingers-1)).
    # So, we have (users*fingers) * (samples * (users*fingers-1)) attempts.
    num_rows = (num_users * num_fingers) * (
        num_verify_samples * (num_users * num_fingers - 1)
    )

    # Generate all cross combinations.
    if verbose:
        print("Generating User x Finger x Samples Matrix")
    cross_mat = np.array(
        np.meshgrid(
            users,
            fingers,
            users,
            fingers,
            samples,  # This will not generate the final user assignments.
        )
    ).T.reshape(-1, 5)

    # The resultant cross_mat contains a row entry for all combinations of
    # user x finger x user x finger x sample (5 columns).
    assert cross_mat.shape == (num_rows_naive, 5)

    df = pd.DataFrame(
        columns=[
            Experiment.TableCol.Enroll_User.value,
            Experiment.TableCol.Enroll_Finger.value,
            Experiment.TableCol.Verify_User.value,
            Experiment.TableCol.Verify_Finger.value,
            Experiment.TableCol.Verify_Sample.value,
            Experiment.TableCol.Decision.value,
        ]
    )

    if verbose:
        print("Initializing Data Frame")
    df[Experiment.TableCol.Verify_User.value] = np.zeros(
        num_rows_naive, dtype=int
    )

    if verbose:
        print("Loading Matrix Into Data Frame")
    df[
        [
            Experiment.TableCol.Enroll_User.value,
            Experiment.TableCol.Enroll_Finger.value,
            Experiment.TableCol.Verify_User.value,
            Experiment.TableCol.Verify_Finger.value,
            Experiment.TableCol.Verify_Sample.value,
        ]
    ] = cross_mat

    # Since we don't try to match a verification sample with its own template,
    # drop these attempts.
    if verbose:
        print("Dropping Invalid Matching Combinations")
    df.drop(
        df.loc[
            (df.VerifyUser == df.EnrollUser)
            & (df.VerifyFinger == df.EnrollFinger)
        ].index,
        inplace=True,
    )

    # Check that we now have the correct number of FAR entries.
    assert df.shape[0] == num_rows

    if verbose:
        print("Generating Decisions")
    rng = np.random.default_rng()
    df[Experiment.TableCol.Decision.value] = rng.choice(
        [Experiment.Decision.Reject.value, Experiment.Decision.Accept.value],
        size=num_rows,
        p=[1 - prob, prob],
    )
    if not user_groups is None:
        if verbose:
            print("Adding Group Associations")
        user_groups_tbl = GenerateUserGroup(num_users, user_groups)
        e = Experiment(far_decisions=df, fa_list=None)
        e.add_groups(user_groups_tbl)
        df = e.far_decisions()

    time_end = time.perf_counter()
    if verbose:
        print(f"Generation took {time_end-time_start:.5}s.")

    # TODO: Add options to allow forcing specific anomalous users.
    # One way to do this might be to choose a few user and redraw the
    # decisions from a new more frequent distribution (like 1/20k).

    return df


# TODO: Add a function for generating fake collection directories to test
# collection directory group discovery.


def GenerateFRRResults(
    num_users: int = 72,
    num_fingers: int = 6,
    num_verify_samples: int = 80,
    user_groups: Optional[list[str]] = ["A", "B", "C", "D", "E", "F"],
    prob: float = 2 / 100.0,
    verbose: bool = False,
) -> pd.DataFrame:
    """Generate simulated FRR results."""
    time_start = time.perf_counter()

    users = np.arange(10001, 10001 + num_users)
    fingers = np.arange(num_fingers)
    samples = np.arange(num_verify_samples)

    # This is the number of total FRR matches.
    num_rows = num_users * num_fingers * num_verify_samples

    # Generate all cross combinations.
    if verbose:
        print("Generating User x Finger x Samples Matrix")
    mesh = np.meshgrid(
        users,
        fingers,
        samples,
    )
    # The user and finger for enrollment will always match that of the
    # verification sample, so we duplicate these columns.
    cross_mat = np.array(
        [mesh[0], mesh[1], mesh[0], mesh[1], mesh[2]]
    ).T.reshape(-1, 5)

    # The resultant cross_mat contains a row entry for every verification
    # sample, for all user x finger templates, but still contains the 5
    # core columns.
    assert cross_mat.shape == (num_rows, 5)

    df = pd.DataFrame(
        columns=[
            Experiment.TableCol.Enroll_User.value,
            Experiment.TableCol.Enroll_Finger.value,
            Experiment.TableCol.Verify_User.value,
            Experiment.TableCol.Verify_Finger.value,
            Experiment.TableCol.Verify_Sample.value,
            Experiment.TableCol.Decision.value,
        ]
    )

    if verbose:
        print("Initializing Data Frame")
    df[Experiment.TableCol.Enroll_User.value] = np.zeros(num_rows, dtype=int)

    if verbose:
        print("Loading Matrix Into Data Frame")
    df[
        [
            Experiment.TableCol.Enroll_User.value,
            Experiment.TableCol.Enroll_Finger.value,
            Experiment.TableCol.Verify_User.value,
            Experiment.TableCol.Verify_Finger.value,
            Experiment.TableCol.Verify_Sample.value,
        ]
    ] = cross_mat

    if verbose:
        print("Generating Decisions")
    rng = np.random.default_rng()
    df[Experiment.TableCol.Decision.value] = rng.choice(
        [Experiment.Decision.Accept.value, Experiment.Decision.Reject.value],
        size=num_rows,
        p=[1 - prob, prob],
    )
    if not user_groups is None:
        if verbose:
            print("Adding Group Associations")
        user_groups_tbl = GenerateUserGroup(num_users, user_groups)
        e = Experiment(far_decisions=df, fa_list=None)
        e.add_groups(user_groups_tbl)
        df = e.frr_decisions()

    time_end = time.perf_counter()
    if verbose:
        print(f"Generation took {time_end-time_start:.5}s.")

    return df


class SimulateEvalResults:
    """Create a simulation of fingerprint evaluation tool results."""

    def __init__(
        self,
        num_users: int = 72,
        num_fingers: int = 6,
        num_verify_samples: int = 80,
    ) -> None:
        self._num_users = num_users
        self._num_fingers = num_fingers
        self._num_verify_samples = num_verify_samples
        self._exp = Experiment()

    def GenerateUserGroup(
        self,
        groups: list[str] = ["A", "B", "C", "D", "E", "F"],
        verbose: bool = False,
    ):
        if verbose:
            print("Adding Group Associations")
        self._exp.add_groups(GenerateUserGroup(self._num_users, groups))

    def GenerateFARResults(
        self,
        prob: float = 1 / 100000.0,
        verbose: bool = False,
    ):
        self._exp.add_far_decisions(
            GenerateFARResults(
                self._num_users,
                self._num_fingers,
                self._num_verify_samples,
                user_groups=None,
                prob=prob,
                verbose=verbose,
            )
        )

    def GenerateFRRResults(
        self,
        prob: float = 2 / 100.0,
        verbose: bool = False,
    ):
        self._exp.add_frr_decisions(
            GenerateFRRResults(
                self._num_users,
                self._num_fingers,
                self._num_verify_samples,
                user_groups=None,
                prob=prob,
                verbose=verbose,
            )
        )

    def Experiment(self) -> Experiment:
        return self._exp


def main(argv: Optional[list[str]] = None) -> Optional[int]:
    """Generate simulated performance evaluation results.

    This will output three CSV files inside the output_testcase_dir.
    """

    parser = argparse.ArgumentParser(description=main.__doc__)
    parser.add_argument(
        "output_testcase_dir",
        default="simulated_testcase",
        type=pathlib.Path,
        help="simulated testcase directory path which will holding the "
        "simulated decisions",
    )
    parser.add_argument(
        "--users",
        default=72,
        type=int,
        help="number of participants (default: 72)",
    )
    parser.add_argument(
        "--fingers",
        default=6,
        type=int,
        help="number of fingers per participants (default: 6)",
    )
    parser.add_argument(
        "--samples",
        default=80,
        type=int,
        help="number of verification samples per participants" " (default: 80)",
    )
    parser.add_argument(
        "--groups",
        default=6,
        type=int,
        help="number of groups to split participants into"
        " -- "
        "set to 0 to disable groups columns (default: 6)",
    )
    parser.add_argument(
        "--far_prob_divisor",
        default=100000,
        type=int,
        help="divisor of the probability of False Accept"
        " (p = 1/far_prob_divisor) or 0 to yield no false accepts"
        " (default: 100000)",
    )
    parser.add_argument(
        "--frr_prob_percent",
        default=2,
        type=int,
        help="the percent probability of False Reject"
        " (p = frr_prob_percent/100)"
        " (default: 2)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        default=False,
        help="enable verbose output (default: False)",
    )
    args = parser.parse_args(argv)

    verbose = args.verbose

    if args.frr_prob_percent < 0 or args.frr_prob_percent > 100:
        parser.error("The frr_prob_percent must be between 0 to 100")

    if args.far_prob_divisor < 0:
        parser.error("The far_prob_divisor must be 0 or greater")

    output_dir: pathlib.Path = args.output_testcase_dir
    if output_dir.exists() and not output_dir.is_dir():
        parser.error(f'The output directory "{output_dir}" is not a directory.')

    if verbose:
        print(f"Creating dir {output_dir}.")
    output_dir.mkdir(parents=True, exist_ok=True)

    csv_far_path = output_dir / "FAR_decisions.csv"
    csv_frr_path = output_dir / "FRR_decisions.csv"
    csv_groups_path = output_dir / "user_groups.csv"
    if csv_far_path.is_dir():
        parser.error(f'The FAR CSV file "{csv_far_path}" is a directory.')
    if csv_frr_path.is_dir():
        parser.error(f'The FRR CSV file "{csv_frr_path}" is a directory.')
    if args.groups > 0 and csv_groups_path.is_dir():
        parser.error(
            f'The User/Groups CSV file "{csv_groups_path}" is a directory.'
        )

    groups_list: Optional[list[str]] = None
    if args.groups > 0:
        groups_list = [chr(a) for a in range(ord("A"), ord("A") + args.groups)]
        if verbose:
            print(f"Using generated groups {groups_list}.")
        if args.users % len(groups_list) != 0:
            parser.error(
                f"User ({args.users}) is not divisible by number "
                f"of groups ({len(groups_list)})."
            )

    far_prob = (
        0.0 if args.far_prob_divisor == 0 else 1.0 / args.far_prob_divisor
    )
    frr_prob = args.frr_prob_percent / 100.0
    if verbose:
        print(f"Using probability of false accept {far_prob}.")
        print(f"Using probability of false reject {frr_prob}.")

    s = SimulateEvalResults(
        num_users=args.users,
        num_fingers=args.fingers,
        num_verify_samples=args.samples,
    )

    s.GenerateFARResults(
        prob=far_prob,
        verbose=verbose,
    )

    s.GenerateFRRResults(
        prob=frr_prob,
        verbose=verbose,
    )

    if groups_list:
        s.GenerateUserGroup(
            groups_list,
            verbose=verbose,
        )
    exp = s.Experiment()

    if verbose:
        print(f"Generated {exp.far_decisions().shape[0]} cross matches.")

    print(f"Writing output csv {csv_far_path}.")
    exp.far_decisions_to_csv(csv_far_path)
    print(f"Writing output csv {csv_frr_path}.")
    exp.frr_decisions_to_csv(csv_frr_path)
    if groups_list:
        print(f"Writing output csv {csv_groups_path}.")
        exp.user_groups_table_to_csv(csv_groups_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
