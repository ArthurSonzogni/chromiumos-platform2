#!/usr/bin/env python3

import pathlib
from enum import Enum
from typing import List, Union

import pandas as pd

from experiment import Experiment


class FPCBETResults:

    class TestCase(Enum):
        '''Identify which experiment test case.'''

        TUDisabled = 'TC-01'
        TUSpecificSamples = 'TC-02-TU'
        TUEnabled = 'TC-03-TU-Continuous'

        @classmethod
        def all(cls) -> list:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> List[str]:
            return list(level.value for level in cls)

    class TableType(Enum):
        '''Identify what type of experiment data is represented in a table.'''

        FAR = 'FAR_stats_4levels.txt'
        FRR = 'FRR_stats_4levels.txt'
        FA_List = 'FalseAccepts.txt'
        FAR_Decision = 'FAR_decisions.csv'
        FRR_Decision = 'FRR_decisions.csv'

        @classmethod
        def all(cls) -> list:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> List[str]:
            return list(level.value for level in cls)

    class SecLevel(Enum):
        '''The order of these item are in increasing security level.'''

        Target_20K = 'FPC_BIO_SECURITY_LEVEL_LOW'
        Target_50K = 'FPC_BIO_SECURITY_LEVEL_STANDARD'
        Target_100K = 'FPC_BIO_SECURITY_LEVEL_SECURE'
        Target_500K = 'FPC_BIO_SECURITY_LEVEL_HIGH'

        @classmethod
        def all(cls) -> list:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> List[str]:
            return list(level.value for level in cls)

        def column_false(self) -> str:
            return self.name + '_False'

        def column_total(self) -> str:
            return self.name + '_Total'

    def __init__(self, report_directory: Union[pathlib.Path, str]):
        self.dir = pathlib.Path(report_directory)

    def file_name(self, test_case: TestCase, table_type: TableType) -> str:
        return self.dir.joinpath(test_case.value).joinpath(table_type.value)

    @staticmethod
    def find_blank_lines(file_name: str) -> int:
        with open(file_name, 'r') as f:
            return list(i for i, l in enumerate(f.readlines()) if l.isspace())

    def read_fa_list_file(self, test_case: TestCase) -> pd.DataFrame:
        '''Read the `TableType.FA_List` (FalseAccepts.txt) file.

        The table will include the following columns:
        VerifyUser, VerifyFinger, VerifySample, EnrollUser, EnrollFinger, Strong FA
        '''

        assert test_case in self.TestCase

        file_name = self.file_name(test_case, self.TableType.FA_List)
        tbl = pd.read_csv(
            file_name,
            skiprows=[0, 1, 2, 3, 4],
            header=None,
            names=[Experiment.TableCol.Verify_User.value,
                   Experiment.TableCol.Verify_Finger.value,
                   Experiment.TableCol.Verify_Sample.value,
                   Experiment.TableCol.Enroll_User.value,
                   Experiment.TableCol.Enroll_Finger.value,
                   'StrongFA'],
            sep=' ?[,\/] ?',
            engine='python',
        )

        for col in Experiment.FalseTableCols:
            col_text = tbl[col].str.extract('= (\d+)', expand=False)
            tbl[col] = pd.to_numeric(col_text)

        tbl['StrongFA'] = (tbl['StrongFA'] != 'no')

        tbl.attrs['ReportDir'] = self.dir
        tbl.attrs['TestCase'] = test_case.name
        tbl.attrs['TableType'] = self.TableType.FA_List
        return tbl

    def read_decision_file(self,
                           test_case: TestCase,
                           table_type: TableType) -> pd.DataFrame:
        '''Read the `TableType.FAR_Decision` or `TableType.FRR_Decision` file.

        The table will include the following columns:
        VerifyUser, VerifyFinger, VerifySample, EnrollUser, EnrollFinger, Strong FA
        '''

        assert test_case in self.TestCase
        assert table_type in [self.TableType.FAR_Decision,
                              self.TableType.FRR_Decision]

        file_name = self.file_name(test_case, table_type)
        tbl = pd.read_csv(file_name)

        tbl.rename(
            columns={
                'Enrolled user': Experiment.TableCol.Enroll_User.value,
                'Enrolled finger': Experiment.TableCol.Enroll_Finger.value,
                'User': Experiment.TableCol.Verify_User.value,
                'Finger': Experiment.TableCol.Verify_Finger.value,
                'Sample': Experiment.TableCol.Verify_Sample.value,
                'Decision': Experiment.TableCol.Decision.value,
            },
            inplace=True,
        )

        tbl.attrs['ReportDir'] = self.dir
        tbl.attrs['TestCase'] = test_case.name
        tbl.attrs['TableType'] = table_type.name
        return tbl

    def read_far_frr_file(self,
                          test_case: TestCase,
                          table_type: TableType,
                          sec_levels: List[SecLevel] = SecLevel.all()) -> pd.DataFrame:
        '''Read `TableType.FAR` and `TableType.FRR` (F[AR]R_stats_4level.txt) file.

        This only reads the last/bottom table of the file.
        '''

        assert test_case in self.TestCase
        assert table_type in [self.TableType.FAR, self.TableType.FRR]

        file_name = self.file_name(test_case, table_type)
        blank_lines = self.find_blank_lines(file_name)
        # Account for possibly extra blank lines.
        if len(blank_lines) < 2:
            return None

        tbl: pd.DataFrame = pd.read_table(
            file_name,
            skiprows=blank_lines[1] + 1,
            header=None,
            names=['User', 'Finger'] + self.SecLevel.all_values(),
            # Comma or more than 2 spaces.
            sep=r'\, | {2,}',
            engine='python',
        )

        user_id = tbl['User'].str.extract('(\d+)', expand=False)
        finger_id = tbl['Finger'].str.extract('(\d+)', expand=False)
        tbl['User'] = pd.to_numeric(user_id)
        tbl['Finger'] = pd.to_numeric(finger_id)

        for level in sec_levels:
            false_count = tbl[level.value].str.extract('(\d+)\/', expand=False)
            total_count = tbl[level.value].str.extract('\/(\d+)', expand=False)
            tbl[level.column_false()] = pd.to_numeric(false_count)
            tbl[level.column_total()] = pd.to_numeric(total_count)
        tbl = tbl.drop(columns=self.SecLevel.all_values())
        tbl.attrs['ReportDir'] = self.dir
        tbl.attrs['TestCase'] = test_case.name
        tbl.attrs['TableType'] = table_type.name

        return tbl

    def read_file(self,
                  test_case: TestCase,
                  table_type: TableType) -> pd.DataFrame:
        '''Read specified BET generated table for the specified `test_case`.

        This is an interface that calls on the correct read file function for
        the specified table.
        '''

        assert test_case in self.TestCase
        assert table_type in self.TableType

        if table_type in [self.TableType.FAR, self.TableType.FRR]:
            return self.read_far_frr_file(test_case, table_type)
        elif table_type is self.TableType.FA_List:
            return self.read_fa_list_file(test_case)
        elif table_type in [self.TableType.FAR_Decision, self.TableType.FRR_Decision]:
            return self.read_decision_file(test_case, table_type)
        else:
            return None
