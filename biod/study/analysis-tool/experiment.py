#!/usr/bin/env python3

from enum import Enum
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

    def FATestResult() -> pd.DataFrame:
        pass
