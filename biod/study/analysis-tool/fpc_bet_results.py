# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Read and parse output artifacts from FPC's Biometric Evaluation Tool."""

from __future__ import annotations

import enum
import pathlib
from typing import Iterable

import cached_data_file
import pandas as pd
import table
from test_case_enum import TestCaseEnum


class FPCBETResults:
    """Parses for FPC biometric evaluation tool results."""

    # TODO: Add parsing of the BET yaml config file to get information
    # about number of enrollment samples and whatnot.

    class TestCase(TestCaseEnum):
        """Identify which experiment test case."""

        # TODO: Add note about using different sets of captures during each
        # test case. I believe we add the template updating samples into
        # the verification set on the TUContinuous TC.

        TUDisabled = ("All template updating disabled.", "TC-01")
        TUSpecificSamples_Disabled = (
            "Use template updating on dedicated samples, but "
            "temporarily disable this mechanism as a control.",
            "TC-02-TU/EnableTemplateUpdating-0",
        )
        TUSpecificSamples_Enabled = (
            "Use template updating on dedicated samples.",
            "TC-02-TU/EnableTemplateUpdating-1",
        )
        # The normal operating mode for production.
        TUContinuous_Disabled = (
            "Update the template after each match, but "
            "temporarily disable this mechanism as a control.",
            "TC-03-TU-Continuous/EnableTemplateUpdating-0",
        )
        TUContinuous_Enabled = (
            "Update the template after each match.",
            "TC-03-TU-Continuous/EnableTemplateUpdating-1",
        )

        def path(self) -> str:
            assert len(self.extra()) == 1
            return self.extra()[0]

    class TableType(enum.Enum):
        """Identify what type of experiment data is represented in a table."""

        FAR_Stats = "FAR_stats_4levels.txt"
        FRR_Stats = "FRR_stats_4levels.txt"
        FA_List = "FalseAccepts.txt"
        FAR_Decision = "FAR_decisions.csv"
        FRR_Decision = "FRR_decisions.csv"

        @classmethod
        def all(cls) -> list[FPCBETResults.TableType]:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> list[str]:
            return list(level.value for level in cls)

    class Column(enum.Enum):
        """Columns produced when reading the non-standard stats and FA files.

        Count and Total are appended to `SecLevel.Target_*K`.
        See `SecLevel.column_count` and `SecLevel.column_total`.
        """

        User = "User"
        Finger = "Finger"
        Count = "Count"
        Total = "Total"
        Strong_FA = "Strong_FA"

    class SecLevel(enum.Enum):
        """The order of these item are in increasing security level."""

        Target_20K = "FPC_BIO_SECURITY_LEVEL_LOW"
        Target_50K = "FPC_BIO_SECURITY_LEVEL_STANDARD"
        Target_100K = "FPC_BIO_SECURITY_LEVEL_SECURE"
        Target_500K = "FPC_BIO_SECURITY_LEVEL_HIGH"

        @classmethod
        def all(cls) -> list[FPCBETResults.SecLevel]:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> list[str]:
            return list(level.value for level in cls)

        def column_count(self) -> str:
            return self.name + "_" + FPCBETResults.Column.Count.value

        def column_total(self) -> str:
            return self.name + "_" + FPCBETResults.Column.Total.value

    def __init__(self, report_directory: pathlib.Path | str):
        self._dir = pathlib.Path(report_directory)

    def _file_name(
        self, test_case: TestCaseEnum, table_type: TableType
    ) -> pathlib.Path:
        """Return the file path for the given `test_case` and `table_type`."""
        assert isinstance(test_case, FPCBETResults.TestCase)
        return self._dir / test_case.path() / table_type.value

    @staticmethod
    def _find_blank_lines(file_name: pathlib.Path) -> list[int]:
        with open(file_name, "r", encoding="utf-8") as f:
            return list(i for i, l in enumerate(f.readlines()) if l.isspace())

    def read_fa_list_file(
        self, test_case: FPCBETResults.TestCase
    ) -> pd.DataFrame:
        """Read the `TableType.FA_List` (FalseAccepts.txt) file.

        The input and output table includes the columns VerifyUser,
        VerifyFinger, VerifySample, EnrollUser, EnrollFinger, and Strong FA.
        """

        assert isinstance(test_case, FPCBETResults.TestCase)
        assert test_case in FPCBETResults.TestCase

        file_name = self._file_name(test_case, self.TableType.FA_List)
        tbl = pd.read_csv(
            file_name,
            skiprows=[0, 1, 2, 3, 4],
            header=None,
            names=[
                table.Col.Verify_User.value,
                table.Col.Verify_Finger.value,
                table.Col.Verify_Sample.value,
                table.Col.Enroll_User.value,
                table.Col.Enroll_Finger.value,
            ]
            + [FPCBETResults.Column.Strong_FA.value],
            sep=r" ?[,\/] ?",
            engine="python",
        )

        for col in table.FALSE_TABLE_COLS:
            col_text = tbl[col].str.extract(r"= (\d+)", expand=False)
            tbl[col] = pd.to_numeric(col_text)

        tbl[FPCBETResults.Column.Strong_FA.value] = (
            tbl[FPCBETResults.Column.Strong_FA.value] != "no"
        )

        tbl.attrs["ReportDir"] = self._dir
        tbl.attrs["TestCase"] = test_case.name
        tbl.attrs["TableType"] = self.TableType.FA_List
        return tbl

    def read_decision_file(
        self, test_case: TestCaseEnum, table_type: TableType
    ) -> pd.DataFrame:
        """Read the `TableType.FAR_Decision` or `TableType.FRR_Decision` file.

        The output table will include the columns EnrollUser, EnrollFinger,
        VerifyUser, VerifyFinger, VerifySample, and Decision.
        """

        assert isinstance(test_case, FPCBETResults.TestCase)
        assert test_case in FPCBETResults.TestCase
        assert table_type in [
            self.TableType.FAR_Decision,
            self.TableType.FRR_Decision,
        ]

        file_name = self._file_name(test_case, table_type)
        with cached_data_file.CachedCSVFile(file_name) as csv:
            tbl = csv.get(disable_cache=False)

        tbl.rename(
            columns={
                "Enrolled user": table.Col.Enroll_User.value,
                "Enrolled finger": table.Col.Enroll_Finger.value,
                "User": table.Col.Verify_User.value,
                "Finger": table.Col.Verify_Finger.value,
                "Sample": table.Col.Verify_Sample.value,
                "Decision": table.Col.Decision.value,
            },
            inplace=True,
        )

        tbl.attrs["ReportDir"] = self._dir
        tbl.attrs["TestCase"] = test_case.name
        tbl.attrs["TableType"] = table_type.name
        return tbl

    def read_far_frr_file(
        self,
        test_case: TestCaseEnum,
        table_type: TableType,
        sec_levels: list[SecLevel] = SecLevel.all(),
    ) -> pd.DataFrame | None:
        """Read `TableType.FAR` and `TableType.FRR` stats file.

        These are the FAR_stats_4level.txt / FRR_stats_4level.txt files.

        This only reads the last/bottom table of the file.
        This file and function only gives a summary counts of groups of matches.
        """

        assert isinstance(test_case, FPCBETResults.TestCase)
        assert test_case in FPCBETResults.TestCase
        assert table_type in [
            self.TableType.FAR_Stats,
            self.TableType.FRR_Stats,
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
                FPCBETResults.Column.Finger.value,
            ]
            + self.SecLevel.all_values(),
            # Comma or more than 2 spaces.
            sep=r"\, | {2,}",
            engine="python",
        )

        # Fix User and Finger ID columns.
        user_id = tbl[FPCBETResults.Column.User.value].str.extract(
            r"(\d+)", expand=False
        )
        finger_id = tbl[FPCBETResults.Column.Finger.value].str.extract(
            r"(\d+)", expand=False
        )
        tbl[FPCBETResults.Column.User.value] = pd.to_numeric(user_id)
        tbl[FPCBETResults.Column.Finger.value] = pd.to_numeric(finger_id)

        # Break open each security level column into a count and total column.
        for level in sec_levels:
            false_count = tbl[level.value].str.extract(r"(\d+)\/", expand=False)
            total_count = tbl[level.value].str.extract(r"\/(\d+)", expand=False)
            tbl[level.column_count()] = pd.to_numeric(false_count)
            tbl[level.column_total()] = pd.to_numeric(total_count)
        # Remove the original count and total security column.
        tbl = tbl.drop(columns=self.SecLevel.all_values())

        tbl.attrs["ReportDir"] = self._dir
        tbl.attrs["TestCase"] = test_case.name
        tbl.attrs["TableType"] = table_type.name
        return tbl

    def read_file(
        self, test_case: TestCaseEnum, table_type: TableType
    ) -> pd.DataFrame | None:
        """Read specified BET generated table for the specified `test_case`.

        This is an interface that calls on the correct read file function for
        the specified table.
        """

        assert isinstance(test_case, FPCBETResults.TestCase)
        assert test_case in FPCBETResults.TestCase
        assert table_type in self.TableType

        if table_type in [self.TableType.FAR_Stats, self.TableType.FRR_Stats]:
            return self.read_far_frr_file(test_case, table_type)
        elif table_type is self.TableType.FA_List:
            return self.read_fa_list_file(test_case)
        elif table_type in [
            self.TableType.FAR_Decision,
            self.TableType.FRR_Decision,
        ]:
            return self.read_decision_file(test_case, table_type)
        else:
            return None

    def read_files(
        self, case_table_pairs: Iterable[tuple[TestCaseEnum, TableType]]
    ) -> list[pd.DataFrame | None]:
        """Read all test-case/table-type pairs as fast as possible.

        This may be parallelized in the future.
        """

        dfs: list[pd.DataFrame | None] = []
        for c in case_table_pairs:
            dfs.append(self.read_file(*c))
        return dfs
