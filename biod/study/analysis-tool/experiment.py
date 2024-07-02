#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

from enum import Enum
import pathlib
from typing import Literal, Optional

from collection import Collection
import fpsutils
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
        """All known table column names used across different table types."""

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
        def all(cls) -> list[Enum]:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> list[str]:
            return list(level.value for level in cls)

    FALSE_TABLE_COLS = [
        TableCol.Enroll_User.value,
        TableCol.Enroll_Finger.value,
        TableCol.Verify_User.value,
        TableCol.Verify_Finger.value,
        TableCol.Verify_Sample.value,
    ]
    DECISION_TABLE_COLS = [
        TableCol.Enroll_User.value,
        TableCol.Enroll_Finger.value,
        TableCol.Verify_User.value,
        TableCol.Verify_Finger.value,
        TableCol.Verify_Sample.value,
        TableCol.Decision.value,
    ]
    DECISION_TABLE_GROUP_COLS = [
        TableCol.Enroll_Group.value,
        TableCol.Verify_Group.value,
    ]
    USER_GROUP_TABLE_COLS = [
        TableCol.User.value,
        TableCol.Group.value,
    ]
    """Column names used in a user_group mapping table."""

    @staticmethod
    def _false_table_query(
        false_table: pd.DataFrame,
        enroll_user_id: Optional[int] = None,
        enroll_finger_id: Optional[int] = None,
        verify_user_id: Optional[int] = None,
        verify_finger_id: Optional[int] = None,
        verify_sample_index: Optional[int] = None,
    ) -> pd.DataFrame:
        query_parts: list[str] = []

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
        """A faster version of `_false_table_query`.

        See the unit test benchmarks.
        """
        query_cols: list[str] = []
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

    def __init__(
        self,
        far_decisions: Optional[pd.DataFrame] = None,
        frr_decisions: Optional[pd.DataFrame] = None,
    ):
        """Initialize a new experiment."""

        self._tbl_far_decisions = far_decisions
        self._tbl_frr_decisions = frr_decisions
        self._tbl_fa_list = None
        self._tbl_fr_list = None
        self._tbl_user_groups = None

    def Describe(self):
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

    def has_far_decisions(self) -> bool:
        """Return True if an FAR decision table has been set."""
        return self._tbl_far_decisions is not None

    def has_frr_decisions(self) -> bool:
        """Return True if an FRR decision table has been set."""
        return self._tbl_frr_decisions is not None

    def has_user_groups(self) -> bool:
        """Return True if user group mapping table is available."""
        return self.user_groups_table() is not None

    def user_groups_table(self) -> Optional[pd.DataFrame]:
        """Return the user group mapping table.

        This function will search for user-group mapping information in the
        following order:
        1. Preexisting user groups added through the add_groups function
        2. Group mappings in FRR decisions table
        3. Group mappings in FAR decisions table

        If no group information is found in any of these, None is returned.

        Considering we do not know whether all user/groups are represented in
        the enrollment or verification set, we scan both. However, we do not
        scan both FAR and FRR decision tables.
        """

        def exists_with_groups(table: Optional[pd.DataFrame]) -> bool:
            """Return True if `table` exists and contains group cols."""
            if table is None:
                return False
            return fpsutils.has_columns(table, self.DECISION_TABLE_GROUP_COLS)

        if self._tbl_user_groups is None:
            if exists_with_groups(self._tbl_frr_decisions):
                tbl = self._tbl_frr_decisions
            elif exists_with_groups(self._tbl_far_decisions):
                tbl = self._tbl_far_decisions
            else:
                return None

            assert tbl is not None, "exists_with_groups allowed None table"

            enroll_groups = tbl[
                [
                    Experiment.TableCol.Enroll_User.value,
                    Experiment.TableCol.Enroll_Group.value,
                ]
            ].copy(deep=True)
            verify_groups = tbl[
                [
                    Experiment.TableCol.Verify_User.value,
                    Experiment.TableCol.Verify_Group.value,
                ]
            ].copy(deep=True)

            # Rename columns.
            enroll_groups.columns = [
                Experiment.TableCol.User.value,
                Experiment.TableCol.Group.value,
            ]
            verify_groups.columns = [
                Experiment.TableCol.User.value,
                Experiment.TableCol.Group.value,
            ]

            user_groups = pd.concat([enroll_groups, verify_groups])
            user_groups.drop_duplicates(inplace=True)
            user_groups.sort_values(
                Experiment.TableCol.User.value, inplace=True
            )
            user_groups.reset_index(inplace=True, drop=True)
            self._tbl_user_groups = user_groups

        return self._tbl_user_groups

    def user_groups_table_to_csv(self, csv_file_path: pathlib.Path) -> None:
        """Write out the user group mapping table to a CSV file."""
        user_groups = self.user_groups_table()
        if user_groups is None:
            raise ValueError("No user group information found")
        user_groups.to_csv(csv_file_path, index=False)

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

    def far_decisions_to_csv(
        self, csv_file_path: pathlib.Path, exclude_groups: bool = True
    ) -> None:
        """Write out the FAR decisions to a CSV file.

        By default, any added groups will be stripped from the output decisions.
        When `exclude_groups` is False, any groups added will be saved within
        the the output decisions CSV.
        """
        _write_decision_file(
            self.far_decisions(), csv_file_path, exclude_groups
        )

    def frr_decisions(self) -> pd.DataFrame:
        return self._tbl_frr_decisions

    def frr_decisions_to_csv(
        self, csv_file_path: pathlib.Path, exclude_groups: bool = True
    ) -> None:
        """Write out the FRR decisions to a CSV file.

        By default, any added groups will be stripped from the output decisions.
        When `exclude_groups` is False, any groups added will be saved within
        the the output decisions CSV.
        """
        _write_decision_file(
            self.frr_decisions(), csv_file_path, exclude_groups
        )

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

    def add_far_decisions(self, table: pd.DataFrame):
        """Add an FAR decision table to experiment."""
        self._tbl_far_decisions = table

    def add_far_decisions_from_csv(self, csv_file_path: pathlib.Path):
        """Read FAR decision file and add to experiment."""
        self.add_far_decisions(_read_decision_file(csv_file_path))

    def add_frr_decisions(self, table: pd.DataFrame):
        """Add an FRR decision table to experiment."""
        self._tbl_frr_decisions = table

    def add_frr_decisions_from_csv(self, csv_file_path: pathlib.Path):
        """Read FRR decision file and add to experiment."""
        self.add_frr_decisions(_read_decision_file(csv_file_path))

    def add_groups(self, user_groups: pd.DataFrame):
        """Add the appropriate group columns to all saved tables."""

        self._tbl_user_groups = user_groups

        if self.has_far_decisions():
            self._tbl_far_decisions = _add_groups_to_table(
                self._tbl_far_decisions, user_groups
            )

        if self.has_frr_decisions():
            self._tbl_frr_decisions = _add_groups_to_table(
                self._tbl_frr_decisions, user_groups
            )

        if not self._tbl_fa_list is None:
            self._tbl_fa_list = _add_groups_to_table(
                self._tbl_fa_list, user_groups
            )

    def add_groups_from_csv(
        self,
        csv_file_path: pathlib.Path = pathlib.Path("user_groups.csv"),
    ):
        """Add group information from a user group mapping CSV file."""

        user_groups: pd.DataFrame = pd.read_csv(csv_file_path)
        # Ensure that the required columns exist.
        if not fpsutils.has_columns(
            user_groups,
            Experiment.USER_GROUP_TABLE_COLS,
        ):
            raise ValueError(
                f"CSV file {csv_file_path} doesn't contain columns"
                f" {Experiment.USER_GROUP_TABLE_COLS}."
            )

        self.add_groups(user_groups)

    def add_groups_from_collection_dir(self, collection_dir: pathlib.Path):
        """Add the appropriate group columns to all saved tables.

        This group information is learned from the subdirectory structure
        of the raw collection directory.
        """

        collection = Collection(collection_dir)
        user_groups = pd.DataFrame(
            collection.discover_user_groups(),
            columns=[
                Experiment.TableCol.User.value,
                Experiment.TableCol.Group.value,
            ],
        )
        user_groups.sort_values(Experiment.TableCol.User.value, inplace=True)
        self.add_groups(user_groups)

    def check(self) -> None:
        """Check for consistency between tables and study parameters.

        - A decisions table should either not have any group information or
          both Enroll and Verify group columns must exist.
        - It is okay for one decisions table to contain groups and the other
          missing groups. This is considered a correct in-between state that
          will be adjusted the next time the table without groups is accessed.
        - The FAR table should not include "genuine" FRR matches.
        - The FRR table should not include "imposter" FAR matches.

        Raises an exception if an inconsistency is found.
        """

        def check_decisions(tbl: pd.DataFrame, name: Literal["FAR", "FRR"]):
            """Check basic decisions table properties, like columns."""
            if not fpsutils.has_columns(tbl, Experiment.DECISION_TABLE_COLS):
                raise TypeError(
                    f"{name} decision table is missing some required columns."
                )

            # If one of the Enroll/Verify group columns is present, then the
            # other must be present.
            grps = set(tbl.columns) & set(Experiment.DECISION_TABLE_GROUP_COLS)
            if len(grps) == 1:
                raise TypeError(
                    f"{name} decision table is missing one group column."
                )

        if self.has_far_decisions():
            assert self._tbl_far_decisions is not None
            far = self._tbl_far_decisions

            check_decisions(far, "FAR")

            # FAR table should not contain any match attempts against the
            # finger's own template, where Enroll User+Finger equals
            # Verify User+Finger.

            bad_fa_attempts = far.loc[
                (
                    far[Experiment.TableCol.Enroll_User.value]
                    == far[Experiment.TableCol.Verify_User.value]
                )
                & (
                    far[Experiment.TableCol.Enroll_Finger.value]
                    == far[Experiment.TableCol.Verify_Finger.value]
                )
            ]
            if len(bad_fa_attempts) > 0:
                print(
                    f"Found {len(bad_fa_attempts)} FRR match attempts in FAR "
                    "decisions table."
                )
                print("Example:")
                # Pass array to iloc to ensure it prints out as table row,
                # instead of vertical single item series.
                print(bad_fa_attempts.iloc[[0]])
                raise ValueError("FAR table contains genuine match attempts.")

        if self.has_frr_decisions():
            assert self._tbl_frr_decisions is not None
            frr = self._tbl_frr_decisions

            check_decisions(frr, "FRR")

            # FRR table should not contain any imposter matches, where the
            # Verify User+Finger doesn't equal Enroll USer+Finger.

            bad_fr_attempts = frr.loc[
                (
                    frr[Experiment.TableCol.Enroll_User.value]
                    != frr[Experiment.TableCol.Verify_User.value]
                )
                | (
                    frr[Experiment.TableCol.Enroll_Finger.value]
                    != frr[Experiment.TableCol.Verify_Finger.value]
                )
            ]
            if len(bad_fr_attempts) > 0:
                print(
                    f"Found {len(bad_fr_attempts)} FAR match attempts in FRR "
                    "decisions table."
                )
                print("Example:")
                print(bad_fr_attempts.iloc[[0]])
                raise ValueError("FRR table contains imposter match attempts.")


def _add_groups_to_table(
    tbl: pd.DataFrame, user_groups: pd.DataFrame
) -> pd.DataFrame:
    """Adds the appropriate group columns for any user columns in `tbl`.

    This joins the `Group` columns from `users_groups` with any user columns
    in `tbl`.

    The `user_groups` table is expected to have a `User` and `Group` column.
    """

    # Add Group column, if it already contains a User column.
    if Experiment.TableCol.User.value in tbl.columns:
        tbl = tbl.join(
            user_groups.set_index(Experiment.TableCol.Verify_User.value),
            on=Experiment.TableCol.Verify_User.value,
        )

    # Add Verify_Group column, if it already contains a Verify_User column.
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

    # Add Enroll_Group column, if it already contains an Enroll_User column.
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


def _read_decision_file(csv_file_path: pathlib.Path) -> pd.DataFrame:
    """Read a CSV decisions file into a DataFrame with supported columns."""
    table: pd.DataFrame = pd.read_csv(csv_file_path)
    # Ensure that the required columns exist.
    if not fpsutils.has_columns(table, Experiment.DECISION_TABLE_COLS):
        raise ValueError(
            f"CSV file {csv_file_path} doesn't contain columns"
            f" {Experiment.DECISION_TABLE_COLS}."
        )
    return table


def _write_decision_file(
    table: pd.DataFrame,
    csv_file_path: pathlib.Path,
    exclude_groups: bool = True,
) -> None:
    """Write a decisions table out as a CSV file.

    Group are removed from the written out table, if `exclude_groups` is True.
    """
    if exclude_groups:
        table = table[Experiment.DECISION_TABLE_COLS]
    # Setting index to False avoids the "index" / primary-key of the
    # dataframe from being written out.
    table.to_csv(csv_file_path, index=False)
