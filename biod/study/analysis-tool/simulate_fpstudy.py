#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to generate simulated fingerprint study results."""

# %%
import argparse
import pathlib
import sys
from typing import List, Optional

import numpy as np
import pandas as pd

from experiment import Experiment


def GenerateFARResults(
    num_users: int = 72,
    num_fingers: int = 6,
    num_verify_samples: int = 80,
    user_groups: List[str] = ['A', 'B', 'C', 'D', 'E', 'F'],
    prob: float = 1/100000.0,
    verbose: bool = False,
) -> pd.DataFrame:
    users = np.arange(10001, 10001+num_users)
    fingers = np.arange(num_fingers)
    samples = np.arange(num_verify_samples)

    # This includes match attempts between the same enroll and verification
    # fingers. We need this for initial dataframe sizing, before we prune the
    # invalid matches.
    num_rows_naive = (
        (num_users * num_fingers * num_verify_samples)
        * (num_users * num_fingers)
    )

    num_rows = (
        (num_users * num_fingers * num_verify_samples)
        * ((num_users * num_fingers) - 1)
    )

    # Generate all cross combinations.
    if verbose:
        print('Generating User x Finger x Samples Matrix')
    cross_mat = np.array(np.meshgrid(
        users,
        fingers,
        samples,
        users,
        fingers,  # This will not generate the final user assignments.
    )).T.reshape(-1, 5)

    assert cross_mat.shape == (num_rows_naive, 5)

    df = pd.DataFrame(
        columns=[
            Experiment.TableCol.Verify_User.value,
            Experiment.TableCol.Verify_Finger.value,
            Experiment.TableCol.Verify_Sample.value,
            Experiment.TableCol.Enroll_User.value,
            Experiment.TableCol.Enroll_Finger.value,
            Experiment.TableCol.Decision.value,
            # Experiment.TableCol.Verify_Group.value,
            # Experiment.TableCol.Enroll_Group.value,
        ]
    )

    if verbose:
        print('Initializing Data Frame')
    df[Experiment.TableCol.Verify_User.value] = np.zeros(
        num_rows_naive, dtype=int)

    if verbose:
        print('Loading Matrix Into Data Frame')
    df[
        [Experiment.TableCol.Verify_User.value,
         Experiment.TableCol.Verify_Finger.value,
         Experiment.TableCol.Verify_Sample.value,
         Experiment.TableCol.Enroll_User.value,
         Experiment.TableCol.Enroll_Finger.value]
    ] = cross_mat

    # Since we don't try to match a verification sample with its own template,
    # drop these attempts.
    if verbose:
        print('Dropping Invalid Matching Combinations')
    df.drop(
        df.loc[
            (df.VerifyUser == df.EnrollUser)
            & (df.VerifyFinger == df.EnrollFinger)
        ].index,
        inplace=True,
    )

    assert df.shape[0] == num_rows

    if verbose:
        print('Generate Decisions')
    rng = np.random.default_rng()
    df[Experiment.TableCol.Decision.value] = \
        rng.choice([False, True], size=num_rows,
                   p=[1-prob, prob])

    if verbose:
        print('Adding Group Associations')
    if num_users % len(user_groups) != 0:
        print('Warning - The number users cannot be evenly split into'
              f' {len(user_groups)} groups.')
    user_groups_tbl = pd.DataFrame({
        Experiment.TableCol.User.value: users,
        Experiment.TableCol.Group.value: np.repeat(
            user_groups,
            repeats=num_users/len(user_groups)
        ),
    })
    e = Experiment(num_verification=num_verify_samples,
                   num_fingers=num_fingers,
                   num_users=num_users,
                   far_decisions=df,
                   fa_list=None)
    e.AddGroups(user_groups_tbl)
    df = e.FARDecisions()

    # TODO: Add options to allow forcing specific anomalous users.
    # One way to do this might be to choose a few user and redraw the
    # decisions from a new more frequent distribution (like 1/20k).

    return df


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    parser = argparse.ArgumentParser(
        description='Generate simulated fpstudy data.')
    parser.add_argument('output_csv_file', type=str,
                        help='The file path to write the simulated csv data to.')
    args = parser.parse_args()
    csv_path = pathlib.Path(args.output_csv_file)

    if not csv_path.parents[0].is_dir():
        parser.error(f'The directory "{csv_path.parents[0]}" doesn\'t exist.')
    if csv_path.is_dir():
        parser.error(f'The output file "{csv_path}" is a directory.')

    df = GenerateFARResults(verbose=True)
    print(df)
    print(f'Writing output csv to {csv_path}')
    df.to_csv(csv_path, index=False)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
