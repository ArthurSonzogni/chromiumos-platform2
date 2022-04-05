#!/usr/bin/env python3

from enum import Enum
import glob
import pathlib
from typing import Optional

import pandas as pd


class Experiment:

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

    FalseTableCols = {TableCol.Enroll_User,
                      TableCol.Enroll_Finger,
                      TableCol.Verify_User,
                      TableCol.Verify_Finger,
                      TableCol.Verify_Sample}
    DecisionTableCols = {TableCol.Enroll_User,
                         TableCol.Enroll_Finger,
                         TableCol.Verify_User,
                         TableCol.Verify_Finger,
                         TableCol.Verify_Sample}

    def _FalseTableQuery(false_table: pd.DataFrame,
                         enroll_user_id: int = None,
                         enroll_finger_id: int = None,
                         verify_user_id: int = None,
                         verify_finger_id: int = None,
                         verify_sample_index: int = None) -> pd.DataFrame:
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
        # print('Query string:', query_str)

        return false_table.query(query_str) if query_str else false_table

    def _FalseTableQuery2(false_table: pd.DataFrame,
                          enroll_user_id: int = None,
                          enroll_finger_id: int = None,
                          verify_user_id: int = None,
                          verify_finger_id: int = None,
                          verify_sample_index: int = None) -> pd.DataFrame:

        query_cols = []
        query_vals = ()
        # query_dict = {}

        for arg, col in [
            (enroll_user_id, Experiment.TableCol.Enroll_User),
            (enroll_finger_id, Experiment.TableCol.Enroll_Finger),
            (verify_user_id, Experiment.TableCol.Verify_User),
            (verify_finger_id, Experiment.TableCol.Verify_Finger),
            (verify_sample_index, Experiment.TableCol.Verify_Sample),
        ]:
            if arg:
                # query_dict[col.value] = arg
                query_cols.append(col.value)
                query_vals += (arg,)

        # if query_dict:
            # return false_table.isin(query_dict)

        if query_cols:
            res = (false_table[query_cols] == query_vals)
            return false_table.loc[res.all(axis=1)]

        return false_table

    def _AddGroupsTo(tbl: pd.DataFrame, user_groups: pd.DataFrame) -> pd.DataFrame:
        '''Adds the appropriate group columns for any user columns in `tbl`.

        This joins the `Group` columns from `users_groups` with any user columns
        in `tbl`.

        The `user_groups` table is expected to have a `User` and `Group` column.
        '''

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

    def _UserGroupsFromCollectionDir(collection_dir: pathlib.Path) -> pd.DataFrame:
        assert collection_dir.exists()

        user_groups_set = []
        for path in glob.glob(str(collection_dir) + '/*/*'):
            path_split = path.split('/')
            user_id, user_group = path_split[-2], path_split[-1]
            user_groups_set.append((int(user_id), user_group))

        return pd.DataFrame(user_groups_set,
                            columns=[Experiment.TableCol.User.value,
                                     Experiment.TableCol.Group.value])

    def __init__(self,
                 #  num_enrollment: int,
                 num_verification: int,
                 num_fingers: int,
                 num_users: int,
                 far_decisions: Optional[pd.DataFrame] = None,
                 fa_list: Optional[pd.DataFrame] = None):

        # self.num_enrollment = num_enrollment
        self.num_verification = num_verification
        self.num_fingers = num_fingers
        self.num_users = num_users
        self.tbl_far_decisions = far_decisions
        self.tbl_fa_list = fa_list

        if self.tbl_fa_list is None:
            far = self.FARDecisions()
            fa_list = far.loc[
                far[Experiment.TableCol.Decision.value]
            ].copy(deep=True)
            fa_list.drop([Experiment.TableCol.Decision.value],
                         axis=1, inplace=True)
            fa_list.reset_index(drop=True, inplace=True)
            self.tbl_fa_list = fa_list

    def FAList(self) -> pd.DataFrame:
        return self.tbl_fa_list

    def FARDecisions(self) -> pd.DataFrame:
        return self.tbl_far_decisions

    def FAQuery(self,
                enroll_user_id: int = None,
                enroll_finger_id: int = None,
                verify_user_id: int = None,
                verify_finger_id: int = None,
                verify_sample_index: int = None) -> pd.DataFrame:
        return Experiment._FalseTableQuery(false_table=self.FAList(),
                                           enroll_user_id=enroll_user_id,
                                           enroll_finger_id=enroll_finger_id,
                                           verify_user_id=verify_user_id,
                                           verify_finger_id=verify_finger_id,
                                           verify_sample_index=verify_sample_index)

    def FAQuery2(self,
                 enroll_user_id: int = None,
                 enroll_finger_id: int = None,
                 verify_user_id: int = None,
                 verify_finger_id: int = None,
                 verify_sample_index: int = None) -> pd.DataFrame:
        return Experiment._FalseTableQuery2(false_table=self.FAList(),
                                            enroll_user_id=enroll_user_id,
                                            enroll_finger_id=enroll_finger_id,
                                            verify_user_id=verify_user_id,
                                            verify_finger_id=verify_finger_id,
                                            verify_sample_index=verify_sample_index)

    def AddGroups(self, user_groups: pd.DataFrame):
        '''Add the appropriate group columns to all saved tables.'''

        if not self.tbl_far_decisions is None:
            self.tbl_far_decisions = Experiment._AddGroupsTo(
                self.tbl_far_decisions, user_groups)

        if not self.tbl_fa_list is None:
            self.tbl_fa_list = Experiment._AddGroupsTo(
                self.tbl_fa_list, user_groups)

    def AddGroupsFromCollectionDir(self, collection_dir: pathlib.Path):
        '''Add the appropriate group columns to all saved tables.

        This group information is learned from the subdirectory structure
        of the raw collection directory.
        '''

        user_groups = Experiment._UserGroupsFromCollectionDir(collection_dir)
        self.AddGroups(user_groups)

    def FATestResult() -> pd.DataFrame:
        pass
