#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Read and parse output artifacts from FPC's Biometric Evaluation Tool."""

from __future__ import annotations

import pathlib
from enum import Enum
from typing import List, Optional, Tuple, TypeVar, Union

import pandas as pd

from cached_data_file import CachedCSVFile
from experiment import Experiment


class FPCBETResults:

    class TestCase(Enum):
        """Identify which experiment test case."""

        TUDisabled = 'TC-01'
        TUSpecificSamples_Disabled = 'TC-02-TU/EnableTemplateUpdating-0'
        TUSpecificSamples_Enabled = 'TC-02-TU/EnableTemplateUpdating-0'
        # The normal operating mode for production.
        TUContinuous_Disabled = 'TC-03-TU-Continuous/EnableTemplateUpdating-0'
        TUContinuous_Enabled = 'TC-03-TU-Continuous/EnableTemplateUpdating-1'

        @classmethod
        def all(cls) -> List[FPCBETResults.TestCase]:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> List[str]:
            return list(level.value for level in cls)

    class TableType(Enum):
        """Identify what type of experiment data is represented in a table."""

        FAR_Stats = 'FAR_stats_4levels.txt'
        FRR_Stats = 'FRR_stats_4levels.txt'
        FA_List = 'FalseAccepts.txt'
        FAR_Decision = 'FAR_decisions.csv'
        FRR_Decision = 'FRR_decisions.csv'

        @classmethod
        def all(cls) -> List[FPCBETResults.TableType]:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> List[str]:
            return list(level.value for level in cls)

    class Column(Enum):
        """Columns produced when reading the non-standard stats and FA files.

        Count and Total are appended to `SecLevel.Target_*K`.
        See `SecLevel.column_count` and `SecLevel.column_total`.
        """

        User = 'User'
        Finger = 'Finger'
        Count = 'Count'
        Total = 'Total'
        Strong_FA = 'Strong_FA'

    class SecLevel(Enum):
        """The order of these item are in increasing security level."""

        Target_20K = 'FPC_BIO_SECURITY_LEVEL_LOW'
        Target_50K = 'FPC_BIO_SECURITY_LEVEL_STANDARD'
        Target_100K = 'FPC_BIO_SECURITY_LEVEL_SECURE'
        Target_500K = 'FPC_BIO_SECURITY_LEVEL_HIGH'

        @classmethod
        def all(cls) -> List[FPCBETResults.SecLevel]:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> List[str]:
            return list(level.value for level in cls)

        def column_count(self) -> str:
            return self.name + '_' + FPCBETResults.Column.Count.value

        def column_total(self) -> str:
            return self.name + '_' + FPCBETResults.Column.Total.value

    def __init__(self, report_directory: Union[pathlib.Path, str]):
        self._dir = pathlib.Path(report_directory)

    def _file_name(self, test_case: TestCase, table_type: TableType) \
            -> pathlib.Path:
        """Return the file path for the given `test_case` and `table_type`."""
        return self._dir.joinpath(test_case.value).joinpath(table_type.value)

    @staticmethod
    def _find_blank_lines(file_name: pathlib.Path) -> List[int]:
        with open(file_name, 'r') as f:
            return list(i for i, l in enumerate(f.readlines()) if l.isspace())

    def read_fa_list_file(self, test_case: TestCase) -> pd.DataFrame:
        """Read the `TableType.FA_List` (FalseAccepts.txt) file.

        The table will include the following columns:
        VerifyUser, VerifyFinger, VerifySample, EnrollUser, EnrollFinger, Strong FA
        """

        assert test_case in self.TestCase

        file_name = self._file_name(test_case, self.TableType.FA_List)
        tbl = pd.read_csv(
            file_name,
            skiprows=[0, 1, 2, 3, 4],
            header=None,
            names=[Experiment.TableCol.Verify_User.value,
                   Experiment.TableCol.Verify_Finger.value,
                   Experiment.TableCol.Verify_Sample.value,
                   Experiment.TableCol.Enroll_User.value,
                   Experiment.TableCol.Enroll_Finger.value,
                   ] + [FPCBETResults.Column.Strong_FA.value],
            sep=' ?[,\/] ?',
            engine='python',
        )

        for col in Experiment.FALSE_TABLE_COLS:
            col_text = tbl[col].str.extract('= (\d+)', expand=False)
            tbl[col] = pd.to_numeric(col_text)

        tbl[FPCBETResults.Column.Strong_FA.value] = (
            tbl[FPCBETResults.Column.Strong_FA.value] != 'no'
        )

        tbl.attrs['ReportDir'] = self._dir
        tbl.attrs['TestCase'] = test_case.name
        tbl.attrs['TableType'] = self.TableType.FA_List
        return tbl

    def read_decision_file(self,
                           test_case: TestCase,
                           table_type: TableType) -> pd.DataFrame:
        """Read the `TableType.FAR_Decision` or `TableType.FRR_Decision` file.

        The table will include the following columns:
        VerifyUser, VerifyFinger, VerifySample, EnrollUser, EnrollFinger, Strong FA
        """

        assert test_case in self.TestCase
        assert table_type in [self.TableType.FAR_Decision,
                              self.TableType.FRR_Decision]

        file_name = self._file_name(test_case, table_type)
        with CachedCSVFile(file_name) as csv:
            tbl = csv.get(disable_cache=False)

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

        tbl.attrs['ReportDir'] = self._dir
        tbl.attrs['TestCase'] = test_case.name
        tbl.attrs['TableType'] = table_type.name
        return tbl

    def read_far_frr_file(self,
                          test_case: TestCase,
                          table_type: TableType,
                          sec_levels: List[SecLevel] = SecLevel.all()) \
            -> Optional[pd.DataFrame]:
        """Read `TableType.FAR` and `TableType.FRR` (F[AR]R_stats_4level.txt) file.

        This only reads the last/bottom table of the file.
        This file and function only gives a summary counts of groups of matches.
        """

        assert test_case in self.TestCase
        assert table_type in [
            self.TableType.FAR_Stats,
            self.TableType.FRR_Stats
        ]

        file_name = self._file_name(test_case, table_type)
        blank_lines = self._find_blank_lines(file_name)
        # Account for possibly extra blank lines.
        if len(blank_lines) < 2:
            return None

        # Note that each security level column contains the count and total
        # as a string. This will be expanded later.
        tbl = pd.read_table(
            file_name,
            skiprows=blank_lines[1] + 1,
            header=None,
            names=[
                FPCBETResults.Column.User.value,
                FPCBETResults.Column.Finger.value
            ] + self.SecLevel.all_values(),
            # Comma or more than 2 spaces.
            sep=r'\, | {2,}',
            engine='python',
        )

        # Fix User and Finger ID columns.
        user_id = tbl[FPCBETResults.Column.User.value].str.extract(
            '(\d+)', expand=False)
        finger_id = tbl[FPCBETResults.Column.Finger.value].str.extract(
            '(\d+)', expand=False)
        tbl[FPCBETResults.Column.User.value] = pd.to_numeric(user_id)
        tbl[FPCBETResults.Column.Finger.value] = pd.to_numeric(finger_id)

        # Break open each security level column into a count and total column.
        for level in sec_levels:
            false_count = tbl[level.value].str.extract('(\d+)\/', expand=False)
            total_count = tbl[level.value].str.extract('\/(\d+)', expand=False)
            tbl[level.column_count()] = pd.to_numeric(false_count)
            tbl[level.column_total()] = pd.to_numeric(total_count)
        # Remove the original count and total security column.
        tbl = tbl.drop(columns=self.SecLevel.all_values())

        tbl.attrs['ReportDir'] = self._dir
        tbl.attrs['TestCase'] = test_case.name
        tbl.attrs['TableType'] = table_type.name
        return tbl

    def read_file(self,
                  test_case: TestCase,
                  table_type: TableType) -> Optional[pd.DataFrame]:
        """Read specified BET generated table for the specified `test_case`.

        This is an interface that calls on the correct read file function for
        the specified table.
        """

        assert test_case in self.TestCase
        assert table_type in self.TableType

        if table_type in [self.TableType.FAR_Stats, self.TableType.FRR_Stats]:
            return self.read_far_frr_file(test_case, table_type)
        elif table_type is self.TableType.FA_List:
            return self.read_fa_list_file(test_case)
        elif table_type in [self.TableType.FAR_Decision, self.TableType.FRR_Decision]:
            return self.read_decision_file(test_case, table_type)
        else:
            return None

    def read_files(self, case_table_pairs: List[Tuple[TestCase, TableType]]) \
            -> List[Optional[pd.DataFrame]]:
        """Read all test-case/table-type pairs as fast as possible.

        This may be parallelizied in the future.
        """

        dfs = list()
        for c in case_table_pairs:
            dfs.append(self.read_file(*c))
        return dfs
