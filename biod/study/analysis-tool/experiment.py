#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
from enum import Enum
from typing import Optional

import numpy as np
import numpy.typing as npt
import pandas as pd

from collection import Collection


class Experiment:
    """Represents a fingerprint study experiment that is being analyzed."""

    class Finger(Enum):

        Thumb_Left = 0
        Thumb_Right = 1
        Index_Left = 2
        Index_Right = 3
        Middle_Left = 4
        Middle_Right = 5

    class UserGroup(Enum):

        A = 0
        B = 1
        C = 2
        D = 3
        E = 4
        F = 5

    class Decision(Enum):

        Accept = 'ACCEPT'
        Reject = 'REJECT'

    class TableCol(Enum):

        Enroll_User = 'EnrollUser'
        Enroll_Finger = 'EnrollFinger'
        Enroll_Group = 'EnrollGroup'
        Verify_User = 'VerifyUser'
        Verify_Finger = 'VerifyFinger'
        Verify_Sample = 'VerifySample'
        Verify_Group = 'VerifyGroup'
        Decision = 'Decision'
        User = 'User'
        Group = 'Group'

        @classmethod
        def all(cls) -> list:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> list:
            return list(level.value for level in cls)

    FALSE_TABLE_COLS = {TableCol.Enroll_User.value,
                        TableCol.Enroll_Finger.value,
                        TableCol.Verify_User.value,
                        TableCol.Verify_Finger.value,
                        TableCol.Verify_Sample.value}
    DECISION_TABLE_COLS = {TableCol.Enroll_User.value,
                           TableCol.Enroll_Finger.value,
                           TableCol.Verify_User.value,
                           TableCol.Verify_Finger.value,
                           TableCol.Verify_Sample.value,
                           TableCol.Decision.value}

    @staticmethod
    def _false_table_query(false_table: pd.DataFrame,
                           enroll_user_id: Optional[int] = None,
                           enroll_finger_id: Optional[int] = None,
                           verify_user_id: Optional[int] = None,
                           verify_finger_id: Optional[int] = None,
                           verify_sample_index: Optional[int] = None) -> pd.DataFrame:
        query_parts = []

        for arg, col in [
            (enroll_user_id, Experiment.TableCol.Enroll_User),
            (enroll_finger_id, Experiment.TableCol.Enroll_Finger),
            (verify_user_id, Experiment.TableCol.Verify_User),
            (verify_finger_id, Experiment.TableCol.Verify_Finger),
            (verify_sample_index, Experiment.TableCol.Verify_Sample),
        ]:
            if arg:
                query_parts.append(f'({col.value} == {arg})')

        query_str = ' & '.join(query_parts)

        return false_table.query(query_str) if query_str else false_table

    @staticmethod
    def _false_table_query2(false_table: pd.DataFrame,
                            enroll_user_id: Optional[int] = None,
                            enroll_finger_id: Optional[int] = None,
                            verify_user_id: Optional[int] = None,
                            verify_finger_id: Optional[int] = None,
                            verify_sample_index: Optional[int] = None) -> pd.DataFrame:

        query_cols = []
        query_vals = ()

        for arg, col in [
            (enroll_user_id, Experiment.TableCol.Enroll_User),
            (enroll_finger_id, Experiment.TableCol.Enroll_Finger),
            (verify_user_id, Experiment.TableCol.Verify_User),
            (verify_finger_id, Experiment.TableCol.Verify_Finger),
            (verify_sample_index, Experiment.TableCol.Verify_Sample),
        ]:
            if arg:
                query_cols.append(col.value)
                query_vals += (arg,)

        if query_cols:
            res = (false_table[query_cols] == query_vals)
            return false_table.loc[res.all(axis=1)]

        return false_table

    @staticmethod
    def _add_groups_to_table(tbl: pd.DataFrame,
                             user_groups: pd.DataFrame) -> pd.DataFrame:
        """Adds the appropriate group columns for any user columns in `tbl`.

        This joins the `Group` columns from `users_groups` with any user columns
        in `tbl`.

        The `user_groups` table is expected to have a `User` and `Group` column.
        """

        # Add Group column.
        if Experiment.TableCol.User.value in tbl.columns:

            tbl = tbl.join(
                user_groups.set_index(Experiment.TableCol.Verify_User.value),
                on=Experiment.TableCol.Verify_User.value,
            )

        # Add Verify_Group column.
        if Experiment.TableCol.Verify_User.value in tbl.columns:
            tbl = tbl.join(
                user_groups.rename(columns={
                    Experiment.TableCol.User.value: Experiment.TableCol.Verify_User.value,
                    Experiment.TableCol.Group.value: Experiment.TableCol.Verify_Group.value,
                }).set_index(Experiment.TableCol.Verify_User.value),
                on=Experiment.TableCol.Verify_User.value,
            )

        # Add Enroll_Group column.
        if Experiment.TableCol.Enroll_User.value in tbl.columns:
            tbl = tbl.join(
                user_groups.rename(columns={
                    Experiment.TableCol.User.value: Experiment.TableCol.Enroll_User.value,
                    Experiment.TableCol.Group.value: Experiment.TableCol.Enroll_Group.value,
                }).set_index(Experiment.TableCol.Enroll_User.value),
                on=Experiment.TableCol.Enroll_User.value,
            )

        return tbl

    @staticmethod
    def _read_far_decision_file(csv_file_path: pathlib.Path) -> pd.DataFrame:
        far_decisions = pd.read_csv(csv_file_path)
        # Ensure that the required columns exist.
        assert Experiment.DECISION_TABLE_COLS <= set(far_decisions.columns)
        return far_decisions

    def __init__(self,
                 #  num_enrollment: int,
                 num_verification: int,
                 num_fingers: int,
                 num_users: int,
                 far_decisions: Optional[pd.DataFrame] = None,
                 fa_list: Optional[pd.DataFrame] = None):
        """Initialize a new experiment."""

        # self.num_enrollment = num_enrollment
        self.num_verification = num_verification
        self.num_fingers = num_fingers
        self.num_users = num_users
        self._tbl_far_decisions = far_decisions
        self._tbl_fa_list = fa_list

    def Describe(self):
        print('Users:', self.num_users)
        print('Fingers:', self.num_fingers)
        print('Verification Samples:', self.num_verification)
        print(self.far_decisions().describe())
        print(self.fa_table().describe())

    def fa_table(self) -> pd.DataFrame:
        """Return an FAR table that only contains the False Acceptances."""
        if self._tbl_fa_list is None:
            far = self.far_decisions()
            fa_list = far.loc[
                far[Experiment.TableCol.Decision.value] ==
                Experiment.Decision.Accept.value
            ].copy(deep=True)
            fa_list.drop([Experiment.TableCol.Decision.value],
                         axis=1, inplace=True)
            fa_list.reset_index(drop=True, inplace=True)
            self._tbl_fa_list = fa_list

        return self._tbl_fa_list

    def fa_trials_count(self) -> int:
        """Return the total number of false accept cross matches."""
        return self.far_decisions().shape[0]

    def fa_count(self) -> int:
        """Return the number of False Acceptances."""
        return self.fa_table().shape[0]

    def far_decisions(self) -> pd.DataFrame:
        return self._tbl_far_decisions

    def unique_list(self, column: TableCol) -> npt.NDArray:
        """Return a unique and sorted list of items for the given column.

        These items are discovered from the FAR decision table.
        The returned array is indexable from 0, since they are in a numpy array.
        """

        return np.sort(self._tbl_far_decisions[column.value].unique())

    def user_list(self) -> npt.NDArray:
        """Return a unique set of sorted User IDs from the verification set.

        These are indexable from 0, since they are in a numpy array.
        """

        return self.unique_list(Experiment.TableCol.Verify_User)

    def finger_list(self) -> npt.NDArray:
        """Return a unique set of sorted Finger IDs from the verification sets.

        These are indexable from 0, since they are in a numpy array.
        """

        return self.unique_list(Experiment.TableCol.Verify_Finger)

    def sample_list(self) -> npt.NDArray:
        """Return a unique set of sorted Sample IDs from the verification sets.

        These are indexable from 0, since they are in a numpy array.
        """

        return self.unique_list(Experiment.TableCol.Verify_Sample)

    def group_list(self) -> npt.NDArray:
        """Return a unique set of sorted Group IDs from the verification sets.

        Recall that these groups might have been added by this class itself.

        These are indexable from 0, since they are in a numpy array.
        """

        return self.unique_list(Experiment.TableCol.Verify_Group)

    def fa_query(self,
                 enroll_user_id: Optional[int] = None,
                 enroll_finger_id: Optional[int] = None,
                 verify_user_id: Optional[int] = None,
                 verify_finger_id: Optional[int] = None,
                 verify_sample_index: Optional[int] = None) -> pd.DataFrame:
        return Experiment._false_table_query(false_table=self.fa_table(),
                                             enroll_user_id=enroll_user_id,
                                             enroll_finger_id=enroll_finger_id,
                                             verify_user_id=verify_user_id,
                                             verify_finger_id=verify_finger_id,
                                             verify_sample_index=verify_sample_index)

    def fa_query2(self,
                  enroll_user_id: Optional[int] = None,
                  enroll_finger_id: Optional[int] = None,
                  verify_user_id: Optional[int] = None,
                  verify_finger_id: Optional[int] = None,
                  verify_sample_index: Optional[int] = None) -> pd.DataFrame:
        return Experiment._false_table_query2(false_table=self.fa_table(),
                                              enroll_user_id=enroll_user_id,
                                              enroll_finger_id=enroll_finger_id,
                                              verify_user_id=verify_user_id,
                                              verify_finger_id=verify_finger_id,
                                              verify_sample_index=verify_sample_index)

    def add_far_decisions_from_csv(self, csv_file_path: pathlib.Path):
        """Read FAR decision file and add to experiment."""
        self._tbl_far_decisions = Experiment._read_far_decision_file(
            csv_file_path)

    def add_groups(self, user_groups: pd.DataFrame):
        """Add the appropriate group columns to all saved tables."""

        if not self._tbl_far_decisions is None:
            self._tbl_far_decisions = Experiment._add_groups_to_table(
                self._tbl_far_decisions, user_groups)

        if not self._tbl_fa_list is None:
            self._tbl_fa_list = Experiment._add_groups_to_table(
                self._tbl_fa_list, user_groups)

    def add_groups_from_collection_dir(self, collection_dir: pathlib.Path):
        """Add the appropriate group columns to all saved tables.

        This group information is learned from the subdirectory structure
        of the raw collection directory.
        """

        collection = Collection(collection_dir)
        self.add_groups(pd.DataFrame(collection.discover_user_groups(),
                                     columns=[Experiment.TableCol.User.value,
                                              Experiment.TableCol.Group.value]))
