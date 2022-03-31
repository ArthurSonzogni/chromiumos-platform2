# To add a new cell, type '# %%'
# To add a new markdown cell, type '# %% [markdown]'
# %% # Imports
import sys
import time
import sys  # sys.getsizeof()

from IPython.display import display, HTML, Markdown
import matplotlib.pyplot as plt
from matplotlib import pyplot as plt
import matplotlib
from math import radians, sqrt
from enum import Enum
from typing import List

import pandas as pd
import numpy as np
import scipy.stats as st
# https://ipython.readthedocs.io/en/stable/interactive/magics.html#magic-matplotlib
# To open matplotlib in interactive mode
# %matplotlib qt5
# %matplotlib notebook
# For VSCode Interactive
# get_ipython().run_line_magic('matplotlib', "widget")
%matplotlib widget
# %matplotlib widget
# %matplotlib gui
# For VSCode No Interactive
# %matplotlib inline
# %matplotlib --list


# %% # Classes

class DataFrameSetAccess:
    '''Provides a quick method of checking if a given row exists in the table.

    This look method takes hundreds of nanoseconds vs other methods that take
    hudreds of micro seconds. Given the amount of times we must query certain
    tables, this order of magnitude difference is unacceptable.
    '''

    def __init__(self, table: pd.DataFrame, cols: list = None):

        if not cols:
            cols = table.columns

        # This is an expensive operation.
        self.set = {tuple(row) for row in np.array(table[cols])}
        # print(f'Cached set takes {sys.getsizeof(self.set)/1024.0}KB of memory.')

    def isin(self, values: tuple) -> bool:
        return values in self.set


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

    class FalseTableCol(Enum):
        Enroll_User = 'EnrollUser'
        Enroll_Finger = 'EnrollFinger'
        Verify_User = 'VerifyUser'
        Verify_Finger = 'VerifyFinger'
        Verify_Sample = 'VerifySample'

        @classmethod
        def all(cls) -> list:
            return list(level for level in cls)

        @classmethod
        def all_values(cls) -> list:
            return list(level.value for level in cls)

    def _FalseTableQuery(false_table: pd.DataFrame,
                         enroll_user_id: int = None,
                         enroll_finger_id: int = None,
                         verify_user_id: int = None,
                         verify_finger_id: int = None,
                         verify_sample_index: int = None) -> pd.DataFrame:
        query_parts = []

        for arg, col in [
            (enroll_user_id, Experiment.FalseTableCol.Enroll_User),
            (enroll_finger_id, Experiment.FalseTableCol.Enroll_Finger),
            (verify_user_id, Experiment.FalseTableCol.Verify_User),
            (verify_finger_id, Experiment.FalseTableCol.Verify_Finger),
            (verify_sample_index, Experiment.FalseTableCol.Verify_Sample),
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
            (enroll_user_id, Experiment.FalseTableCol.Enroll_User),
            (enroll_finger_id, Experiment.FalseTableCol.Enroll_Finger),
            (verify_user_id, Experiment.FalseTableCol.Verify_User),
            (verify_finger_id, Experiment.FalseTableCol.Verify_Finger),
            (verify_sample_index, Experiment.FalseTableCol.Verify_Sample),
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
                 fa_list: pd.DataFrame):
        # self.num_enrollment = num_enrollment
        self.num_verification = num_verification
        self.num_fingers = num_fingers
        self.num_users = num_users
        self.fa_list = fa_list

    def FAList(self) -> pd.DataFrame:
        return self.fa_list

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


class FPCBETResults:

    class TestCase(Enum):
        '''Identify which experiment test case.'''

        TUDisabled = 'TC-01'
        TUSpecificSamples = 'TC-02-TU'
        TUEnabled = 'TC-03-TU-Continuous'

    class TableType(Enum):
        '''Identify what type of experiment data is represented in a table.'''

        FAR = 'FAR_stats_4levels.txt'
        FRR = 'FRR_stats_4levels.txt'
        FA_List = 'FalseAccepts.txt'
        FAR_Decision = 'FAR_decision.csv'
        FRR_Decision = 'FRR_decision.csv'

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
        def all_values(cls) -> list:
            return list(level.value for level in cls)

        def column_false(self) -> str:
            return self.name + '_False'

        def column_total(self) -> str:
            return self.name + '_Total'

        # def proportion(self) -> float:
        #         return 20
        #     return

    def __init__(self, report_directory):
        self.dir = report_directory

    def file_name(self, test_case: TestCase, table_type: TableType) -> str:
        return self.dir + '/' + test_case.value + '/' + table_type.value

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
            names=['VerifyUser', 'VerifyFinger', 'VerifySample',
                   'EnrollUser', 'EnrollFinger'] + ['StrongFA'],
            sep=' ?[,\/] ?',
            engine='python',
        )

        for col in Experiment.FalseTableCol.all_values():
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


def confidence(ups, downs):
    '''This is some algo from a blog that was used for reddit'''

    n = ups + downs

    if n == 0:
        return 0

    z = 1.0  # 1.44 = 85%, 1.96 = 95%
    phat = float(ups) / n
    return ((phat + z*z/(2*n) - z * sqrt((phat*(1-phat)+z*z/(4*n))/n))/(1+z*z/n))


def print_far_value(val: float, comparisons: list = [20, 50, 100, 500]) -> str:
    str = f'{val:.4e}'
    for c in comparisons:
        str += f' = {val * 100 * c*1000:.3f}% of 1/{c}k'
    return str


def discrete_hist(data, discrete_bins=None):
    if discrete_bins is None:
        unique_elements = np.unique(data)
        discrete_bins = np.arange(
            np.min(unique_elements), np.max(unique_elements)+1)
    bins = np.append(discrete_bins, np.max(discrete_bins) + 1)
    hist, bin_edges = np.histogram(data, bins=bins)
    return hist, bin_edges[:-1]


def DoReport(tbl: pd.DataFrame, level: FPCBETResults.SecLevel):
    ####################################################################
    display(Markdown('## Independent Finger'))
    display(Markdown('### General'))
    display(tbl.describe())
    display(tbl.sum())
    data = tbl[level.column_false()]

    # Attempt to plot better
    plt.figure()
    unique_elements = np.unique(data)
    hist, bins = discrete_hist(data, unique_elements)
    plt.bar(bins, hist)
    plt.xticks(bins)
    plt.xlabel('Num False Matches per Finger')
    plt.ylabel('Frequency')
    plt.show()

    display(Markdown('### Confidence'))
    p = tbl[level.column_false()] / tbl[level.column_total()]
    mean = p.mean()
    std = p.std()
    se = std / np.sqrt(p.count())

    print('Mean: ', print_far_value(mean))
    print('Std: ', std)
    print('Sample Std: ', se)
    print('Std Interval: ', [2*se])
    print('95% Interval: ', [print_far_value(
        mean - (2*se), [20]), print_far_value(mean + (2*se), [20])])

    ####################################################################
    display(Markdown('## Group by User'))
    grp_user = tbl.groupby('User').sum()
    grp_user = grp_user.drop(columns=['Finger'])
    display(grp_user)

    plt.figure()
    plt.bar(grp_user.index.to_list(), grp_user[level.column_false()])
    plt.title('False Counts per User ID Across All Fingers')
    plt.xlabel('Finger ID')
    plt.ylabel('False Counts')
    plt.show()

    display(Markdown('### General'))
    display(grp_user.describe())
    display(grp_user.sum())

    display(Markdown('### Confidence'))
    p = grp_user[level.column_false()] / grp_user[level.column_total()]
    mean = p.mean()
    std = p.std()
    se = std / np.sqrt(p.count())

    print('Mean: ', print_far_value(mean))
    print('Std: ', std)
    print('Sample Std: ', se)
    print('Std Interval: ', [2*se])
    print('95% Interval: ', [print_far_value(
        mean - (2*se), [20]), print_far_value(mean + (2*se), [20])])

    ####################################################################
    display(Markdown('## Group by Finger'))
    grp_finger = tbl.groupby('Finger').sum()
    grp_finger = grp_finger.drop(columns=['User'])
    display(grp_finger)

    plt.figure()
    plt.bar(grp_finger.index.to_list(), grp_finger[level.column_false()])
    plt.title('False Counts per Finger ID Across All Users')
    plt.xlabel('Finger ID')
    plt.ylabel('False Counts')
    plt.show()

    display(Markdown('### General'))
    display(grp_finger.describe())
    display(grp_finger.sum())

    display(Markdown('### Confidence'))
    p = grp_finger[level.column_false()] / grp_finger[level.column_total()]
    mean = p.mean()
    std = p.std()
    se = std / np.sqrt(p.count())

    print('Mean: ', print_far_value(mean))
    print('Std: ', std)
    print('Sample Std: ', se)
    print('Std Interval: ', [2*se])
    print('95% Interval: ', [print_far_value(
        mean - (2*se), [20]), print_far_value(mean + (2*se), [20])])


# %%

REPORT_DIR = 'data' # symbolic link to data

bet = FPCBETResults(REPORT_DIR)

# display(Markdown('# TC-01 FAR'))
# tc0_far = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FAR)
# display(tc0_far)

# display(Markdown('# TC-01 FRR'))
# tc0_frr = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FRR)
# display(tc0_frr)

display(Markdown('# TC-01 FAR @ 1/20k'))
tc0_far_20k: pd.DataFrame = bet.read_far_frr_file(
    FPCBETResults.TestCase.TUDisabled,
    FPCBETResults.TableType.FAR,
    [FPCBETResults.SecLevel.Target_20K])
display(tc0_far_20k)
DoReport(tc0_far_20k, FPCBETResults.SecLevel.Target_20K)
# display(tc0_far_20k.describe())
# display(tc0_far_20k.sum())

display(Markdown('# TC-01 FA List Analysis FAR @ 1/20k'))
tc0_far_20k_fa: pd.DataFrame = bet.read_fa_list_file(
    FPCBETResults.TestCase.TUDisabled)
display(tc0_far_20k_fa)
b = Experiment(num_verification=60,
               num_fingers=6,
               num_users=72,
               fa_list=tc0_far_20k_fa)

# %%
# %%timeit

NUM_SAMPLES = 1
rng = np.random.default_rng()
fa_set_tuple = [Experiment.FalseTableCol.Verify_User.value,
                Experiment.FalseTableCol.Enroll_User.value,
                Experiment.FalseTableCol.Verify_Finger.value,
                Experiment.FalseTableCol.Verify_Sample.value]
fa_set = DataFrameSetAccess(b.FAList(), fa_set_tuple)

# This nieve approach take about 500ms to run one bootstrap sample, without
# actually querying the FA table (replaced with pass).
for s in range(NUM_SAMPLES):
    sample = []
    # 72 users
    for v in rng.choice(b.num_users, size=b.num_users, replace=True):
        # print(v)
        # 71 other template users
        for t in rng.choice(list(range(v)) + list(range(v+1, b.num_users)), size=b.num_users-1, replace=True):
            # 6 fingers
            for f in rng.choice(b.num_fingers, size=b.num_fingers, replace=True):
                # 60 verification samples
                for a in rng.choice(b.num_verification, size=b.num_verification, replace=True):
                    # b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)
                    # fa = b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)
                    # sample.append(fa.shape[0] != 0)

                    query = (v, t, f, a)
                    sample.append(fa_set.isin(query))
                    # pass
    # print('')
    # print('samples =', sample)
    # print('sum(samples) =', sum(sample))

# %%
# %%timeit

NUM_SAMPLES = 30
rng = np.random.default_rng()
fa_set_tuple = [Experiment.FalseTableCol.Verify_User.value,
                Experiment.FalseTableCol.Enroll_User.value,
                Experiment.FalseTableCol.Verify_Finger.value,
                Experiment.FalseTableCol.Verify_Sample.value]
fa_set = DataFrameSetAccess(b.FAList(), fa_set_tuple)

# This nieve approach take about 500ms to run one bootstrap sample, without
# actually querying the FA table (replaced with pass).
for s in range(NUM_SAMPLES):
    sample = []
    # 72 users
    sample_verify_users = rng.choice(b.num_users, size=b.num_users, replace=True)
    sample_verify_users = np.repeat(sample_verify_users, b.num_users-1)
    # against 71 other template users
    sample_enroll_users = rng.choice(b.num_users-1, size=b.num_users*(b.num_users-1), replace=True)
    sample_users = np.stack((sample_verify_users, sample_enroll_users), axis=1)

    # Enforce nested loop invariant:
    # Effectively "omit" the verify user id from the enroll users
    sample_users[sample_users[:,0] <= sample_users[:,1]][:,1] += 1

    for v, t in sample_users:
        pass

    for v in rng.choice(b.num_users, size=b.num_users, replace=True):
        # print(v)
        # 71 other template users
        for t in rng.choice(list(range(v)) + list(range(v+1, b.num_users)), size=b.num_users-1, replace=True):
            # 6 fingers
            for f in rng.choice(b.num_fingers, size=b.num_fingers, replace=True):
                # 60 verification samples
                for a in rng.choice(b.num_verification, size=b.num_verification, replace=True):
                    # fa = b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)
                    # sample.append(fa.shape[0] != 0)
                    query = (v, t, f, a)
                    sample.append(fa_set.isin(query))
                    # print(query)
                    # sample.append(fa_set.isin((10012, 10011, 5, 12)))
                    # pass
    print('')
    # print('samples =', sample)
    print('sum(samples) =', sum(sample))
# %%

%timeit b.FAList()
%timeit b.FAQuery()


# %%
print('Query 1 Timing')
%timeit b.FAQuery(verify_user_id=10071)
%timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0)
%timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60)
%timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057)
%timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057, enroll_finger_id=4)

# %%
print('Query 2 Timing')
%timeit b.FAQuery2(verify_user_id=10071)
# %timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0)
# %timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60)
# %timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057)
%timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057, enroll_finger_id=4)

# %%

print('Query 3 Timing')
s = DataFrameSetAccess(b.FAList())
%timeit s.isin((10011, 5, 69, 10012, 0, False))

# %%

# %timeit b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)

# tc0_far_20k: pd.DataFrame = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FAR,
#     [FPCBETResults.SecLevel.Target_20K])

# display(Markdown('# TC-01 FAR @ 1/100k'))
# tc0_far_100k: pd.DataFrame = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FAR,
#     [FPCBETResults.SecLevel.Target_100K])
# display(tc0_far_100k)
# DoReport(tc0_far_100k)


# display(Markdown('# TC-01 FRR @ 1/100k'))
# tc0_frr_100k: pd.DataFrame = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FRR,
#     [FPCBETResults.SecLevel.Target_100K])
# display(tc0_frr_100k)
# DoReport(tc0_far_100k)
