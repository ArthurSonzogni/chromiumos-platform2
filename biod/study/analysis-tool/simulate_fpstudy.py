#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to generate simulated fingerprint study results."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time
from typing import Optional

from experiment import Experiment
import numpy as np
import pandas as pd


def GenerateFARResults(
    num_users: int = 72,
    num_fingers: int = 6,
    num_verify_samples: int = 80,
    user_groups: Optional[list[str]] = ["A", "B", "C", "D", "E", "F"],
    prob: float = 1 / 100000.0,
    verbose: bool = False,
) -> pd.DataFrame:
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
    num_rows = (num_users * num_fingers * num_verify_samples) * (
        (num_users * num_fingers) - 1
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
        print("Generate Decisions")
    rng = np.random.default_rng()
    df[Experiment.TableCol.Decision.value] = rng.choice(
        [Experiment.Decision.Reject.value, Experiment.Decision.Accept.value],
        size=num_rows,
        p=[1 - prob, prob],
    )
    if not user_groups is None:
        if verbose:
            print("Adding Group Associations")
        if num_users % len(user_groups) != 0:
            print(
                "Warning - The number users cannot be evenly split into"
                f" {len(user_groups)} groups."
            )
        user_groups_tbl = pd.DataFrame(
            {
                Experiment.TableCol.User.value: users,
                Experiment.TableCol.Group.value: np.repeat(
                    user_groups, repeats=num_users / len(user_groups)
                ),
            }
        )
        e = Experiment(
            num_verification=num_verify_samples,
            num_fingers=num_fingers,
            num_users=num_users,
            far_decisions=df,
            fa_list=None,
        )
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


def main(argv: Optional[list[str]] = None) -> Optional[int]:
    parser = argparse.ArgumentParser(
        description="Generate simulated fpstudy data."
    )
    parser.add_argument(
        "output_csv_file",
        type=str,
        help="file path to write the simulated csv data to",
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
        "--prob_div",
        default=100000,
        type=int,
        help="divisor of the Probability of False Positive"
        " (p = 1/prob_div)"
        " (default: 100000)",
    )
    args = parser.parse_args(argv)

    csv_path = pathlib.Path(args.output_csv_file)
    if not csv_path.parents[0].is_dir():
        parser.error(f'The directory "{csv_path.parents[0]}" doesn\'t exist.')
    if csv_path.is_dir():
        parser.error(f'The output file "{csv_path}" is a directory.')

    user_groups = None
    if args.groups > 0:
        user_groups = [chr(a) for a in range(ord("A"), ord("A") + args.groups)]
        print(f"Using generated groups {user_groups}.")
        if args.users % len(user_groups) != 0:
            parser.error(
                f"User ({args.users}) is not divisible by number "
                f"of groups ({len(user_groups)})."
            )
    else:
        print("Disabling groups.")

    prob = 1.0 / args.prob_div
    print(f"Using probability of false positive {prob}.")

    df = GenerateFARResults(
        num_users=args.users,
        num_fingers=args.fingers,
        num_verify_samples=args.samples,
        user_groups=user_groups,
        prob=prob,
        verbose=True,
    )

    print(f"Generated {df.shape[0]} cross matches.")

    print(df)
    print(f"Writing output csv to {csv_path}")
    df.to_csv(csv_path, index=False)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
