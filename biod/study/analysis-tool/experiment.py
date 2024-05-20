#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from enum import Enum
import pathlib
from typing import Optional

from collection import Collection
import numpy as np
import numpy.typing as npt
import pandas as pd


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
        Accept = "ACCEPT"
        Reject = "REJECT"

    class TableCol(Enum):
        Enroll_User = "EnrollUser"
        Enroll_Finger = "EnrollFinger"
        Enroll_Group = "EnrollGroup"
        Verify_User = "VerifyUser"
        Verify_Finger = "VerifyFinger"
        Verify_Sample = "VerifySample"
        Verify_Group = "VerifyGroup"
        Decision = "Decision"
        User = "User"
        Group = "Group"

        @classmethod
        def all(cls) -> list:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> list:
            return list(level.value for level in cls)

    FALSE_TABLE_COLS = {
        TableCol.Enroll_User.value,
        TableCol.Enroll_Finger.value,
        TableCol.Verify_User.value,
        TableCol.Verify_Finger.value,
        TableCol.Verify_Sample.value,
    }
    DECISION_TABLE_COLS = {
        TableCol.Enroll_User.value,
        TableCol.Enroll_Finger.value,
        TableCol.Verify_User.value,
        TableCol.Verify_Finger.value,
        TableCol.Verify_Sample.value,
        TableCol.Decision.value,
    }

    @staticmethod
    def _false_table_query(
        false_table: pd.DataFrame,
        enroll_user_id: Optional[int] = None,
        enroll_finger_id: Optional[int] = None,
        verify_user_id: Optional[int] = None,
        verify_finger_id: Optional[int] = None,
        verify_sample_index: Optional[int] = None,
    ) -> pd.DataFrame:
        query_parts = []

        for arg, col in [
            (enroll_user_id, Experiment.TableCol.Enroll_User),
            (enroll_finger_id, Experiment.TableCol.Enroll_Finger),
            (verify_user_id, Experiment.TableCol.Verify_User),
            (verify_finger_id, Experiment.TableCol.Verify_Finger),
            (verify_sample_index, Experiment.TableCol.Verify_Sample),
        ]:
            if arg:
                query_parts.append(f"({col.value} == {arg})")

        query_str = " & ".join(query_parts)

        return false_table.query(query_str) if query_str else false_table

    @staticmethod
    def _false_table_query2(
        false_table: pd.DataFrame,
        enroll_user_id: Optional[int] = None,
        enroll_finger_id: Optional[int] = None,
        verify_user_id: Optional[int] = None,
        verify_finger_id: Optional[int] = None,
        verify_sample_index: Optional[int] = None,
    ) -> pd.DataFrame:
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
            res = false_table[query_cols] == query_vals
            return false_table.loc[res.all(axis=1)]

        return false_table

    @staticmethod
    def _add_groups_to_table(
        tbl: pd.DataFrame, user_groups: pd.DataFrame
    ) -> pd.DataFrame:
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
                user_groups.rename(
                    columns={
                        Experiment.TableCol.User.value: Experiment.TableCol.Verify_User.value,
                        Experiment.TableCol.Group.value: Experiment.TableCol.Verify_Group.value,
                    }
                ).set_index(Experiment.TableCol.Verify_User.value),
                on=Experiment.TableCol.Verify_User.value,
            )

        # Add Enroll_Group column.
        if Experiment.TableCol.Enroll_User.value in tbl.columns:
            tbl = tbl.join(
                user_groups.rename(
                    columns={
                        Experiment.TableCol.User.value: Experiment.TableCol.Enroll_User.value,
                        Experiment.TableCol.Group.value: Experiment.TableCol.Enroll_Group.value,
                    }
                ).set_index(Experiment.TableCol.Enroll_User.value),
                on=Experiment.TableCol.Enroll_User.value,
            )

        return tbl

    @staticmethod
    def _read_far_decision_file(csv_file_path: pathlib.Path) -> pd.DataFrame:
        far_decisions = pd.read_csv(csv_file_path)
        # Ensure that the required columns exist.
        assert Experiment.DECISION_TABLE_COLS <= set(far_decisions.columns)
        return far_decisions

    def __init__(
        self,
        #  num_enrollment: int,
        num_verification: int,
        num_fingers: int,
        num_users: int,
        far_decisions: Optional[pd.DataFrame] = None,
        frr_decisions: Optional[pd.DataFrame] = None,
        fa_list: Optional[pd.DataFrame] = None,
    ):
        """Initialize a new experiment."""

        # self.num_enrollment = num_enrollment
        self.num_verification = num_verification
        self.num_fingers = num_fingers
        self.num_users = num_users
        self._tbl_far_decisions = far_decisions
        self._tbl_frr_decisions = frr_decisions
        self._tbl_fa_list = fa_list
        self._tbl_fr_list = None

    def Describe(self):
        print("Users:", self.num_users)
        print("Fingers:", self.num_fingers)
        print("Verification Samples:", self.num_verification)
        print(self.far_decisions().describe())
        print(self.fa_table().describe())

    def fa_table(self) -> pd.DataFrame:
        """Return an FAR table that only contains the False Acceptances."""
        if self._tbl_fa_list is None:
            far = self.far_decisions()
            fa_list = far.loc[
                far[Experiment.TableCol.Decision.value]
                == Experiment.Decision.Accept.value
            ].copy(deep=True)
            fa_list.drop(
                [Experiment.TableCol.Decision.value], axis=1, inplace=True
            )
            fa_list.reset_index(drop=True, inplace=True)
            self._tbl_fa_list = fa_list

        return self._tbl_fa_list

    def fr_table(self) -> pd.DataFrame:
        """Return an FRR table that only contains the False Rejections."""
        if self._tbl_fr_list is None:
            frr = self.frr_decisions()
            fr_list = frr.loc[
                frr[Experiment.TableCol.Decision.value]
                == Experiment.Decision.Reject.value
            ].copy(deep=True)
            fr_list.drop(
                [Experiment.TableCol.Decision.value], axis=1, inplace=True
            )
            fr_list.reset_index(drop=True, inplace=True)
            self._tbl_fr_list = fr_list

        return self._tbl_fr_list

    def fa_trials_count(self) -> int:
        """Return the total number of false accept cross matches."""
        return self.far_decisions().shape[0]

    def fa_count(self) -> int:
        """Return the number of False Acceptances."""
        return self.fa_table().shape[0]

    def fr_trials_count(self) -> int:
        """Return the total number of false reject cross matches."""
        return self.frr_decisions().shape[0]

    def fr_count(self) -> int:
        """Return the number of False Rejections."""
        return self.fr_table().shape[0]

    def far_decisions(self) -> pd.DataFrame:
        return self._tbl_far_decisions

    def frr_decisions(self) -> pd.DataFrame:
        return self._tbl_frr_decisions

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

    def fa_counts_by(self, column: TableCol) -> pd.Series:
        """Return a Series of False Accept counts based on `column`.

        The series index will be `column` values and the data values will be the
        counts. All column values will be represented from `unique_list(column)`.

        This function is faster than doing the counts directly from the entire
        FAR decision table.
        """

        # Ultimately, we want to generate a histogram over the entire FAR/FRR
        # decision dataset, but doing so directly with the large DataFrame
        # is much too slow.
        #
        # The fastest method is by reverse constructing the complete
        # counts table by using the pre-aggregated fa_table.
        # This runs in about 66ms, which is primarily the time to run
        # `self.unique_list(column)`.
        # This method could be sped up by caching the `unique_list` function.
        #
        # A similar method might be using the following, which runs
        # in about 300ms:
        # far[[column, 'Decision']].groupby([column]).sum()

        fa_table = self.fa_table()
        unique_indices = self.unique_list(column)
        non_zero_counts = fa_table[column.value].value_counts()
        # We will now backfill the 0 counts for indices that aren't represented
        # in the simplified false list. Providing a sorted index list means
        # that the output series will have sorted indices.
        return non_zero_counts.reindex(index=unique_indices, fill_value=0)

    def fr_counts_by(self, column: TableCol) -> pd.Series:
        """Return a Series of False Reject counts based on `column`.

        The series index will be `column` values and the data values will be the
        counts. All column values will be represented from `unique_list(column)`.

        This function is faster than doing the counts directly from the entire
        FRR decision table.
        """

        # Ultimately, we want to generate a histogram over the entire FAR/FRR
        # decision dataset, but doing so directly with the large DataFrame
        # is much too slow.
        #
        # The fastest method is by reverse constructing the complete
        # counts table by using the pre-aggregated fa_table.
        # This runs in about 66ms, which is primarily the time to run
        # `self.unique_list(column)`.
        # This method could be sped up by caching the `unique_list` function.
        #
        # A similar method might be using the following, which runs
        # in about 300ms:
        # frr[[column, 'Decision']].groupby([column]).sum()

        fr_table = self.fr_table()
        unique_indices = self.unique_list(column)
        non_zero_counts = fr_table[column.value].value_counts()
        # We will now backfill the 0 counts for indices that aren't represented
        # in the simplified false list. Providing a sorted index list means
        # that the output series will have sorted indices.
        return non_zero_counts.reindex(index=unique_indices, fill_value=0)

    def fa_query(
        self,
        enroll_user_id: Optional[int] = None,
        enroll_finger_id: Optional[int] = None,
        verify_user_id: Optional[int] = None,
        verify_finger_id: Optional[int] = None,
        verify_sample_index: Optional[int] = None,
    ) -> pd.DataFrame:
        return Experiment._false_table_query(
            false_table=self.fa_table(),
            enroll_user_id=enroll_user_id,
            enroll_finger_id=enroll_finger_id,
            verify_user_id=verify_user_id,
            verify_finger_id=verify_finger_id,
            verify_sample_index=verify_sample_index,
        )

    def fa_query2(
        self,
        enroll_user_id: Optional[int] = None,
        enroll_finger_id: Optional[int] = None,
        verify_user_id: Optional[int] = None,
        verify_finger_id: Optional[int] = None,
        verify_sample_index: Optional[int] = None,
    ) -> pd.DataFrame:
        return Experiment._false_table_query2(
            false_table=self.fa_table(),
            enroll_user_id=enroll_user_id,
            enroll_finger_id=enroll_finger_id,
            verify_user_id=verify_user_id,
            verify_finger_id=verify_finger_id,
            verify_sample_index=verify_sample_index,
        )

    def add_far_decisions_from_csv(self, csv_file_path: pathlib.Path):
        """Read FAR decision file and add to experiment."""
        self._tbl_far_decisions = Experiment._read_far_decision_file(
            csv_file_path
        )

    def add_groups(self, user_groups: pd.DataFrame):
        """Add the appropriate group columns to all saved tables."""

        if not self._tbl_far_decisions is None:
            self._tbl_far_decisions = Experiment._add_groups_to_table(
                self._tbl_far_decisions, user_groups
            )

        if not self._tbl_frr_decisions is None:
            self._tbl_frr_decisions = Experiment._add_groups_to_table(
                self._tbl_frr_decisions, user_groups
            )

        if not self._tbl_fa_list is None:
            self._tbl_fa_list = Experiment._add_groups_to_table(
                self._tbl_fa_list, user_groups
            )

    def add_groups_from_collection_dir(self, collection_dir: pathlib.Path):
        """Add the appropriate group columns to all saved tables.

        This group information is learned from the subdirectory structure
        of the raw collection directory.
        """

        collection = Collection(collection_dir)
        self.add_groups(
            pd.DataFrame(
                collection.discover_user_groups(),
                columns=[
                    Experiment.TableCol.User.value,
                    Experiment.TableCol.Group.value,
                ],
            )
        )
