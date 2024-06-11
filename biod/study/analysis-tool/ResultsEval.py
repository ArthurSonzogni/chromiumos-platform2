# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# To add a new cell, type '# %%'
# To add a new markdown cell, type '# %% [markdown]'

# %%
# Kaleido is needed for plotly SVG export.
#
# You must use at least version 5.8.0 of plotly to fix go.Figure
# type annotation. See https://github.com/plotly/plotly.py/pull/3425 .
#
#! %pip install pandas numpy matplotlib scipy tqdm ipympl jinja2
#! %pip install 'plotly>=5.8.0' kaleido nbformat
# Djlint is a Jinja2 template formatter.
#! %pip install djlint
# For report_*.py
#! %pip install requests

# NOTE: Sometimes this autoreload/autoimport feature messes up the environment.
#       If you see odd issues, like assert statements failing for TestCases,
#       you need to restart your kernel. This is probably because it redefines
#       the enums, which makes previous save enum values not equal to the new
#       enum value.
#! %load_ext autoreload
#! %autoreload 1

from __future__ import annotations

import os
import pathlib
from typing import Optional

import bootstrap
from experiment import Experiment
from fpc_bet_results import FPCBETResults
import fpsutils
from IPython.display import display
from IPython.display import HTML
from IPython.display import Markdown
import matplotlib
from matplotlib import pyplot as plt
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import plotly
import plotly.express as px
import plotly.figure_factory as ff
import plotly.graph_objs as go
import plotly.tools as tls
from report import Figure
from report import Report
import scipy.stats as st
from scipy.stats import norm
import simulate_fpstudy
from test_case import TestCase
from tqdm.autonotebook import tqdm  # Auto detect notebook or console.


pd.options.plotting.backend = "plotly"
# import tqdm
# import tqdm.notebook as tqdm

#! %aimport bootstrap, fpsutils, report, simulate_fpstudy, experiment, fpc_bet_results

# https://ipython.readthedocs.io/en/stable/interactive/magics.html#magic-matplotlib
# To open matplotlib in interactive mode
# %matplotlib qt5
# %matplotlib notebook
# For VSCode Interactive
# get_ipython().run_line_magic('matplotlib', "widget")
#! %matplotlib widget
# %matplotlib gui
# For VSCode No Interactive
# %matplotlib inline
# %matplotlib --list


def print_far_value(
    val: float, k_comparisons: list[int] = [20, 50, 100, 500]
) -> str:
    """Print the science notation"""
    str = f"{val:.4e}"
    for c in k_comparisons:
        str += f" = {val * 100 * c*1000:.3f}% of 1/{c}k"
    # TODO: Make nice output like "1 in 234k".
    return str


# %%
#
# The expected file layout for the data/ symbolic link to dir is the following:
# - report-100k/  (dir containing the decision metadata)
# - orig-data/    (dir contain the collected samples)
# - analysis/     (dir created by this tool)
rpt = Report(pathlib.Path("data/analysis"))

# %% Load Data

# USE_SIMULATED_DATA = True
USE_SIMULATED_DATA = False
# USE_SIMULATED_PROB = 1/100000.0
USE_SIMULATED_PROB = 1 / 155000.0
# USE_SIMULATED_PROB = 1/175000.0
# USE_SIMULATED_PROB = 1/200000.0

exp: Optional[Experiment] = None
test_cases: list[TestCase] = list()

if USE_SIMULATED_DATA:

    class SimulatedTestCase(TestCase):
        SimulatedDataset = ("Simulated dataset.",)

    # Load simulated data
    print("# Loading Simulated Data")
    sim_far_decisions = simulate_fpstudy.GenerateFARResults(
        num_users=72,
        num_fingers=6,
        num_verify_samples=80,
        user_groups=["A", "B", "C", "D", "E", "F"],
        prob=USE_SIMULATED_PROB,
        verbose=True,
    )
    exp = Experiment(far_decisions=sim_far_decisions)
    test_cases = [SimulatedTestCase.SimulatedDataset]

    exps = {
        test_cases[0]: exp,
    }

    DISPLAY_TC = SimulatedTestCase.SimulatedDataset

else:
    # Find Data
    # We expect a symbolic link named `data` to point to the directory that
    # contains the FPC BET reports and raw collection.
    print("# Discover Root Data Directory")
    data_root = pathlib.Path(os.readlink("data"))
    if not data_root.is_dir():
        raise Exception("Data root doesn't exist")
    print(f"Using root {data_root}")

    # BET_REPORT_DIR = str(data_root.joinpath('report2'))
    BET_REPORT_DIR = data_root.joinpath("report-100k")
    COLLECTION_DIR = data_root.joinpath("orig-data")

    # %% Import Data From BET Results

    bet = FPCBETResults(BET_REPORT_DIR)

    # FIXME: Only enable one test case for speed of testing.
    # test_cases = [FPCBETResults.TestCase.TUDisabled]
    test_cases = FPCBETResults.TestCase.all()

    far_decisions = bet.read_files(
        list(
            zip(
                test_cases,
                [FPCBETResults.TableType.FAR_Decision] * len(test_cases),
            )
        )
    )

    frr_decisions = bet.read_files(
        list(
            zip(
                test_cases,
                [FPCBETResults.TableType.FRR_Decision] * len(test_cases),
            )
        )
    )

    exps = {
        test_cases[i]: Experiment(far_decisions=far, frr_decisions=frr)
        for i, (far, frr) in enumerate(zip(far_decisions, frr_decisions))
    }

    for tc in exps:
        exps[tc].add_groups_from_collection_dir(COLLECTION_DIR)

    # exp = Experiment(
    #     far_decisions=far_decisions[0],
    #     frr_decisions=frr_decisions[0],
    # )

    # exp.add_groups_from_collection_dir(COLLECTION_DIR)
    # exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'A']
    # exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'B']
    # exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'C']
    # exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'D']
    # exp._tbl_far_decisions = exp._tbl_far_decisions[exp._tbl_far_decisions['VerifyGroup'] == 'E']

    DISPLAY_TC = FPCBETResults.TestCase.TUDisabled

    exp = exps[DISPLAY_TC]


# %% Generate Report Test cases

rpt_tc = {tc: rpt.test_case_add(str(tc), tc.description()) for tc in test_cases}

# %% Check

description = """\
This shows the number of false accepts as a function of each parameter.
This helps to show if there is some enrollment user, verification user, finger,
or sample that has an unusually high false acceptable rate.

Recall that the results of the FAR experiment is a table of accept/reject
decisions based on the following parameters:

* Enrolled User
* Enrolled Finger
* Verification User
* Verification Finger
* Verification Sample

The Enrolled User and Verification User are further categorized by their
participant groups, Enrolled Group and Verification Group, respectively.
"""
display(
    Markdown(
        f"""\
# Distribution of False Accepts

{description}
"""
    )
)
# rpt.add_text(description)

fa_table = exp.fa_table()
display(fa_table)
# fpsutils.plot_pd_hist_discrete(fa_table, title_prefix='False Accepts')
# rpt.add_plot('Data Distributions')

# %%
#! %%time

# Ultimately, we want to do a histogram over the entire FAR/FRR Decision
# dataset, but doing so directly with the large DataFrame is much too slow
# and will actually hang plotly. We are mimicking the following histogram
# operation:
# go.Histogram(histfunc="sum", x=far['EnrollUser'], y=far['Decision'])
#
# A similar method might be using the following, which runs in about 300ms:
# far[['EnrollUser', 'Decision']].groupby(['EnrollUser']).sum()
#
# The fastest method is by reverse constructing the complete counts table
# by using the pre-aggregated fa_table. This runs in about 66ms, which is
# primarily the time to run exp.user_list().


def fa_count_figure(
    exp: Experiment,
    cols: list[Experiment.TableCol],
    title: str,
    xaxis_title: str,
) -> go.Figure:
    fa_counts = pd.DataFrame({c.value: exp.fa_counts_by(c) for c in cols})
    fa_counts.rename(
        columns={c.value: f"FA Counts by {c.value}" for c in cols}, inplace=True
    )
    non_zero_labels = fa_counts.loc[(fa_counts > 0).any(axis=1)].index
    # non_zero_labels = list(non_zero_counts_enroll.index) + \
    #     list(non_zero_counts_verify.index)
    # non_zero_labels.sort()

    # It is nice to keep the blank space between non-zero bars to be able to
    # identify possible abnormal clusters of high false acceptance users.
    fig = px.bar(
        fa_counts,
        #  pattern_shape='variable', pattern_shape_sequence=['/', '\\'],
        #  text_auto=True,
        #  labels={'EnrollUser': 'FA Counts by EnrollUser',
        #          'VerifyUser': 'FA Counts by VerifyUser'}
        #  orientation='h',
    )
    # fig.update_xaxes(type='category')
    # fig.update_layout(barmode='overlay')
    # fig.update_layout(barmode='group')
    # Reduce opacity to see both histograms
    # fig.update_traces(opacity=0.75)
    # fig.update_traces(opacity=0.50)
    # fig.update_traces(marker_size=10)
    # fig.update_traces(marker_line_color = 'blue', marker_line_width = 0.25)
    fig.update_layout(
        # title_text='False Accepts by User ID of Enroll and Verify',
        title=title,
        # xaxis_title_text='User ID',
        xaxis_title=xaxis_title,
        yaxis_title_text="False Accept Count",
        legend_title="",
        barmode="group",
        # barmode='overlay',
        # bargap=0.2,  # gap between bars of adjacent location coordinates
        # bargroupgap=0.1  # gap between bars of the same location coordinates
        bargap=0.0,
        bargroupgap=0.0,
        xaxis=dict(type="category", tickmode="array", tickvals=non_zero_labels),
    )
    fig.update_layout(
        legend=dict(
            # orientation='h',
            yanchor="top",
            y=0.99,
            xanchor="left",
            x=0.01,
        )
    )
    return fig


def fr_count_figure(
    exp: Experiment,
    cols: list[Experiment.TableCol],
    title: str,
    xaxis_title: str,
) -> go.Figure:
    fr_counts = pd.DataFrame({c.value: exp.fr_counts_by(c) for c in cols})
    fr_counts.rename(
        columns={c.value: f"FR Counts by {c.value}" for c in cols}, inplace=True
    )
    non_zero_labels = fr_counts.loc[(fr_counts > 0).any(axis=1)].index
    # non_zero_labels = list(non_zero_counts_enroll.index) + \
    #     list(non_zero_counts_verify.index)
    # non_zero_labels.sort()

    fr_tests_total = exp.fr_trials_count()
    fr_category_size = len(fr_counts.index)
    fr_per_category = float(fr_tests_total) / float(fr_category_size)
    percents = (
        np.array(fr_counts.values, dtype=float) / fr_per_category
    ) * 100.0

    # It is nice to keep the blank space between non-zero bars to be able to
    # identify possible abnormal clusters of high false acceptance users.
    # fig = go.Figure()
    # fig.add_trace(go.Bar(x=fr_counts.index, y=fr_counts.values[:, 0]))
    fig: go.Figure = px.bar(
        fr_counts,
        #  pattern_shape='variable', pattern_shape_sequence=['/', '\\'],
        #  text_auto=True,
        #  labels={'EnrollUser': 'FA Counts by EnrollUser',
        #          'VerifyUser': 'FA Counts by VerifyUser'}
        #  orientation='h',
        hover_data={
            "percent": (":.3f", percents[:, 0]),
        },
    )
    # fr_tests_total = exp.fr_trials_count()
    # fr_category_size = len(fr_counts.index)
    # fr_per_category = float(fr_tests_total) / float(fr_category_size)
    # percents = (np.array(fr_counts.values, dtype=float) / fr_per_category) * 100.0
    # fr_percents = pd.Series(percents, index=fr_counts.index, name='Percents')
    # fig.add_trace(go.Bar(x=fr_counts.index, y=percents[:, 0], yaxis='y2'))

    # fig.update_xaxes(type='category')
    # fig.update_layout(barmode='overlay')
    # fig.update_layout(barmode='group')
    # Reduce opacity to see both histograms
    # fig.update_traces(opacity=0.75)
    # fig.update_traces(opacity=0.50)
    # fig.update_traces(marker_size=10)
    # fig.update_traces(marker_line_color = 'blue', marker_line_width = 0.25)
    fig.update_layout(
        # title_text='False Rejects by User ID of Enroll and Verify',
        title=title,
        # xaxis_title_text='User ID',
        xaxis_title=xaxis_title,
        # yaxis_title_text='False Rejects Count',
        legend_title="",
        barmode="group",
        # barmode='overlay',
        # bargap=0.2,  # gap between bars of adjacent location coordinates
        # bargroupgap=0.1  # gap between bars of the same location coordinates
        bargap=0.0,
        bargroupgap=0.0,
        xaxis=dict(type="category", tickmode="array", tickvals=non_zero_labels),
        yaxis=dict(
            title="False Rejects Count",
            showline=True,
        ),
        yaxis2=dict(
            title="False Reject Percent",
            side="right",
            showline=True,
        ),
    )
    fig.update_layout(
        legend=dict(
            # orientation='h',
            yanchor="top",
            y=0.99,
            xanchor="left",
            x=0.01,
        )
    )
    return fig


#### Histograms ####

for tc in test_cases:
    exp = exps[tc]
    section = rpt_tc[tc].add_subsection("hist")

    # A high FA count for an EnrollUser would indicate some template(s) for a given
    # user allows more false accepts from other users.
    # A high FA count for a VerifyUser would indicate that some match attempts
    # with this user's fingers yields more false accepts.

    # User
    fig = fa_count_figure(
        exp,
        [Experiment.TableCol.Enroll_User, Experiment.TableCol.Verify_User],
        "False Accepts by User ID of Enroll and Verify",
        "User ID",
    )
    section.add_figure(
        "FA_by_User", "False Accepts by User ID of Enroll and Verify.", fig
    )
    fig = fr_count_figure(
        exp,
        [Experiment.TableCol.Verify_User],
        "False Rejects by User ID",
        "User ID",
    )
    section.add_figure("FR_by_User", "False Rejects by User ID.", fig)

    # Finger
    fig = fa_count_figure(
        exp,
        [Experiment.TableCol.Enroll_Finger, Experiment.TableCol.Verify_Finger],
        "False Accepts by Finger ID of Enroll and Verify",
        "Finger ID",
    )
    section.add_figure(
        "FA_by_Finger", "False Accepts by Finger ID of Enroll and Verify.", fig
    )
    fig = fr_count_figure(
        exp,
        [Experiment.TableCol.Verify_Finger],
        "False Rejects by Finger ID",
        "Finger ID",
    )
    section.add_figure("FR_by_Finger", "False Rejects by Finger ID.", fig)

    # Sample
    # Keep in mind that different test cases may select different samples
    # for verification.
    fig = fa_count_figure(
        exp,
        [Experiment.TableCol.Verify_Sample],
        "False Accepts by Verify Sample ID",
        "Sample ID",
    )
    section.add_figure(
        "FA_by_Sample", "False Accepts by Verify Sample ID.", fig
    )
    fig = fr_count_figure(
        exp,
        [Experiment.TableCol.Verify_Sample],
        "False Rejects by Sample ID. "
        "Keep in mind that different test cases may use "
        "different samples for verification.",
        "Sample ID",
    )
    fr_counts = exp.fr_counts_by(Experiment.TableCol.Verify_Sample)
    line = st.linregress(fr_counts.index, fr_counts.values)
    line_x = np.array(fr_counts.index)
    line = st.linregress(line_x, np.array(fr_counts.values))
    line_y = line.slope * line_x + line.intercept
    fig.add_trace(go.Line(x=line_x, y=line_y, name="Linear Regression"))
    section.add_figure("FR_by_Sample", "False Rejects by Sample ID.", fig)

    # Group
    fig = fa_count_figure(
        exp,
        [Experiment.TableCol.Enroll_Group, Experiment.TableCol.Verify_Group],
        "False Accepts by Group of Enroll and Verify",
        "Group",
    )
    section.add_figure(
        "FA_by_Group", "False Accepts by Group of Enroll and Verify.", fig
    )
    fig = fr_count_figure(
        exp,
        [Experiment.TableCol.Verify_Group],
        "False Rejects by Group",
        "Group",
    )
    section.add_figure("FR_by_Group", "False Rejects by Group.", fig)

rpt_tc[DISPLAY_TC].display(display)

#################################################################

# %%

### FA_by_User

s1 = {
    "EnrollUser_"
    + tc.name: exps[tc].fa_counts_by(Experiment.TableCol.Enroll_User)
    for tc in test_cases
}
s2 = {
    "VerifyUser_"
    + tc.name: exps[tc].fa_counts_by(Experiment.TableCol.Verify_User)
    for tc in test_cases
}
df = pd.DataFrame(s1 | s2)
# [tc.name for tc in test_cases]
fig = px.bar(df)
fig.update_layout(
    title="False Accepts by User",
    xaxis_title="User",
    yaxis_title_text="Count",
    legend_title="Category + Test Case",
    barmode="group",
    # height=2049,
    # barmode='overlay',
    # bargap=0.2,  # gap between bars of adjacent location coordinates
    # bargroupgap=0.1  # gap between bars of the same location coordinates
    bargap=0.0,
    bargroupgap=0.0,
    xaxis=dict(
        type="category",
        # tickmode='array',
        # tickvals=non_zero_labels,
    ),
)
fig.update_layout(
    legend=dict(
        # orientation='h',
        yanchor="top",
        y=0.99,
        xanchor="left",
        x=0.01,
    )
)
fig.show()
rpt.overall_section().add_figure("FA_by_User", "False Accepts by User", fig)

### FR_by_User

df = pd.DataFrame(
    {
        tc.name: exps[tc].fr_counts_by(Experiment.TableCol.Verify_User)
        for tc in test_cases
    }
)
# [tc.name for tc in test_cases]
fig = px.bar(
    df,
    # orientation='h',
)
fig.update_layout(
    title="False Rejects by User",
    xaxis_title="User",
    yaxis_title_text="Count",
    legend_title="Test Case",
    barmode="group",
    # height=2049,
    # barmode='overlay',
    # bargap=0.2,  # gap between bars of adjacent location coordinates
    # bargroupgap=0.1  # gap between bars of the same location coordinates
    bargap=0.0,
    bargroupgap=0.0,
    xaxis=dict(
        type="category",
        # tickmode='array',
        # tickvals=non_zero_labels,
    ),
)
fig.update_layout(
    legend=dict(
        # orientation='h',
        yanchor="top",
        y=0.99,
        xanchor="left",
        x=0.01,
    )
)
fig.show()
rpt.overall_section().add_figure("FR_by_User", "False Rejects by User", fig)

### FA_by_Sample

df = pd.DataFrame(
    {
        tc.name: exps[tc].fa_counts_by(Experiment.TableCol.Verify_Sample)
        for tc in test_cases
    }
)
# [tc.name for tc in test_cases]
fig = px.bar(
    df,
    # orientation='h',
)
fig.update_layout(
    # title_text='False Rejects by User ID of Enroll and Verify',
    title="False Accepts by Sample",
    xaxis_title="Group",
    yaxis_title_text="Count",
    legend_title="Test Case",
    barmode="group",
    # height=2049,
    # barmode='overlay',
    # bargap=0.2,  # gap between bars of adjacent location coordinates
    # bargroupgap=0.1  # gap between bars of the same location coordinates
    bargap=0.0,
    bargroupgap=0.0,
    # xaxis=dict(type='category',
    #             tickmode='array',
    #             tickvals=non_zero_labels),
    xaxis=dict(
        type="category",
        # tickmode='array',
        # tickvals=non_zero_labels,
    ),
)
# fig.update_layout(
#     # title_text='False Accepts by User ID of Enroll and Verify',
#     title=title,
#     # xaxis_title_text='User ID',
#     xaxis_title=xaxis_title,
#     yaxis_title_text='False Accept Count',
#     legend_title='',
#     barmode='group',
#     # barmode='overlay',
#     # bargap=0.2,  # gap between bars of adjacent location coordinates
#     # bargroupgap=0.1  # gap between bars of the same location coordinates
#     bargap=0.0,
#     bargroupgap=0.0,
#     xaxis=dict(type='category',
#                 tickmode='array',
#                 tickvals=non_zero_labels),
# )
fig.update_layout(
    legend=dict(
        # orientation='h',
        yanchor="top",
        y=0.99,
        xanchor="left",
        x=0.01,
    )
)
fig.show()
rpt.overall_section().add_figure("FA_by_Sample", "False Accepts by Sample", fig)

### FA_by_Finger

s1 = {
    "EnrollFinger_"
    + tc.name: exps[tc].fa_counts_by(Experiment.TableCol.Enroll_Finger)
    for tc in test_cases
}
s2 = {
    "VerifyFinger_"
    + tc.name: exps[tc].fa_counts_by(Experiment.TableCol.Verify_Finger)
    for tc in test_cases
}
df = pd.DataFrame(s1 | s2)
# [tc.name for tc in test_cases]
fig = px.bar(df)
fig.update_layout(
    title="False Accepts by Finger",
    xaxis_title="Finger",
    yaxis_title_text="Count",
    legend_title="Category + Test Case",
    barmode="group",
)
fig.show()
rpt.overall_section().add_figure("FA_by_Finger", "False Accepts by Finger", fig)

### FR_by_Finger

df = pd.DataFrame(
    {
        tc.name: exps[tc].fr_counts_by(Experiment.TableCol.Verify_Finger)
        for tc in test_cases
    }
)
# [tc.name for tc in test_cases]
fig = px.bar(df)
fig.update_layout(
    title="False Rejects by Finger",
    xaxis_title="Finger",
    yaxis_title_text="Count",
    legend_title="Test Case",
    barmode="group",
)
fig.show()
rpt.overall_section().add_figure("FR_by_Finger", "False Rejects by Finger", fig)

### FA_by_Group

s1 = {
    "EnrollGroup_"
    + tc.name: exps[tc].fa_counts_by(Experiment.TableCol.Enroll_Group)
    for tc in test_cases
}
s2 = {
    "VerifyGroup_"
    + tc.name: exps[tc].fa_counts_by(Experiment.TableCol.Verify_Group)
    for tc in test_cases
}
df = pd.DataFrame(s1 | s2)
# [tc.name for tc in test_cases]
fig = px.bar(df)
fig.update_layout(
    title="False Accepts by Group",
    xaxis_title="Group",
    yaxis_title_text="Count",
    legend_title="Category + Test Case",
    barmode="group",
)
fig.show()
rpt.overall_section().add_figure("FA_by_Group", "False Accepts by Group", fig)

### FR_by_Group

df = pd.DataFrame(
    {
        tc.name: exps[tc].fr_counts_by(Experiment.TableCol.Verify_Group)
        for tc in test_cases
    }
)
# [tc.name for tc in test_cases]
fig = px.bar(df)
fig.update_layout(
    # title_text='False Rejects by User ID of Enroll and Verify',
    title="False Rejects by Group",
    xaxis_title="Group",
    yaxis_title_text="Count",
    legend_title="Test Case",
    barmode="group",
)
fig.show()
rpt.overall_section().add_figure("FR_by_Group", "False Rejects by Group", fig)

###

# rpt.generate({
#     # 'pdf',
#     # 'md',
#     'html',
# })

# %% Bootstrap Sampling

# 1000 samples is 95%
# 5000 samples is 99%
# BOOTSTRAP_SAMPLES = 1000
# BOOTSTRAP_SAMPLES = 5000  # 5000 samples in 2 seconds (128 cores)
BOOTSTRAP_SAMPLES = 100000  # 100000 samples in 16 seconds (128 cores)
CONFIDENCE_PERCENT = 95
PARALLEL_BOOTSTRAP = True
FAR_THRESHOLD = 1 / 100000.0
FRR_THRESHOLD = 10 / 100.0

#### FAR ####

far_boot_results: dict[TestCase, bootstrap.BootstrapResults] = dict()
frr_boot_results: dict[TestCase, bootstrap.BootstrapResults] = dict()
far_figures: dict[TestCase, go.Figure] = dict()
frr_figures: dict[TestCase, go.Figure] = dict()
for tc in test_cases:
    print(f"Running Test Case {tc}.")
    exp = exps[tc]
    section = rpt_tc[tc].add_subsection("score")
    info = section.add_data("Info")

    #### FAR ####

    # Run FAR bootstrap
    boot = bootstrap.BootstrapFullFARHierarchy(exp, verbose=True)
    # boot = bootstrap.BootstrapFARFlat(exp, verbose=True)
    boot_results = boot.run(
        num_samples=BOOTSTRAP_SAMPLES,
        num_proc=0,
        progress=lambda it, total: tqdm(it, total=total),
    )
    far_boot_results[tc] = boot_results
    # Showing raw values works because we take som many bootstrap samples,
    # which fills in a lot of gaps in a "unique" value (bin_size=1) histogram.
    bins, counts = fpsutils.discrete_hist(boot_results.samples())
    df = pd.DataFrame(
        {
            # X-Axes
            "False Accepts in Bootstrap Sample": bins,
            "FAR": np.array(bins, dtype=float) / exp.fa_trials_count(),
            # Y-Axes
            "Number of Bootstrap Samples Observed": counts,
        }
    )
    fig = px.bar(
        df,
        x="FAR",
        y="Number of Bootstrap Samples Observed",
        hover_data=["False Accepts in Bootstrap Sample"],
        title="Frequency of FAR in Bootstrap Samples",
    )
    fig.update_layout(hovermode="x unified")

    ci_lower, ci_upper = boot_results.confidence_interval()
    frr_ci_lower = ci_lower / exp.fa_trials_count()
    frr_ci_upper = ci_upper / exp.fa_trials_count()
    frr_mean = np.mean(boot_results.samples()) / exp.fa_trials_count()
    frr_std = np.std(boot_results.samples()) / exp.fa_trials_count()
    fig.add_vline(
        x=frr_ci_lower,
        annotation_text=f"lower {CONFIDENCE_PERCENT}%",
        annotation_position="top right",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )
    fig.add_vline(
        x=frr_ci_upper,
        annotation_text=f"upper {CONFIDENCE_PERCENT}%",
        annotation_position="top left",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )
    fig.add_vline(
        x=frr_mean,
        annotation_text="mean",
        annotation_position="top left",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )

    # fig.add_vline(x=1/50000.0, annotation_text='1/50',
    #               line_width=2, line_dash='dash', line_color='red')
    fig.add_vline(
        x=1 / 100000.0,
        annotation_text="1/100k",
        line_width=1,
        line_dash="dash",
        line_color="red",
    )
    fig.add_vline(
        x=1 / 200000.0,
        annotation_text="1/200k",
        line_width=1,
        line_dash="dash",
        line_color="red",
    )
    far_figures[tc] = fig

    section.add_figure(
        "FAR_Bootstrap",
        "The hierarchical FAR bootstrap sampling histogram.",
        fig,
    )

    info.set("FAR_Confidence", CONFIDENCE_PERCENT)
    info.set("FAR_Trials", exp.fa_trials_count())
    info.set("FAR_False_Accepts", exp.fa_count())
    info.set("FAR_CI_Lower", frr_ci_lower)
    info.set("FAR_CI_Upper", frr_ci_upper)
    info.set("FAR_Mean", frr_mean)
    info.set("FAR_Std", frr_std)
    info.set("FAR_Threshold", f"1/{1 / (FAR_THRESHOLD*1000)}k")
    info.set("FAR_Pass", frr_ci_upper < FAR_THRESHOLD)

    #### FRR ####

    # Run FRR bootstrap
    boot = bootstrap.BootstrapFullFRRHierarchy(exp, verbose=True)
    # boot = bootstrap.BootstrapFARFlat(exp, verbose=True)
    boot_results = boot.run(
        num_samples=BOOTSTRAP_SAMPLES,
        num_proc=0,
        progress=lambda it, total: tqdm(it, total=total),
    )
    frr_boot_results[tc] = boot_results

    # Showing raw values works because we take som many bootstrap samples,
    # which fills in a lot of gaps in a "unique" value (bin_size=1) histogram.
    bins, counts = fpsutils.discrete_hist(boot_results.samples())
    df = pd.DataFrame(
        {
            # X-Axes
            "False Accepts in Bootstrap Sample": bins,
            "FRR": np.array(bins, dtype=float) / exp.fr_trials_count(),
            # Y-Axes
            "Number of Bootstrap Samples Observed": counts,
        }
    )
    fig = px.bar(
        df,
        x="FRR",
        y="Number of Bootstrap Samples Observed",
        hover_data=["False Accepts in Bootstrap Sample"],
        title="Frequency of FRR in Bootstrap Samples",
    )
    fig.update_layout(
        hovermode="x unified",
        xaxis=dict(tickformat="%"),
        bargap=0.0,
        bargroupgap=0.0,
    )

    ci_lower, ci_upper = boot_results.confidence_interval()
    frr_ci_lower = ci_lower / exp.fr_trials_count()
    frr_ci_upper = ci_upper / exp.fr_trials_count()
    frr_mean = np.mean(boot_results.samples()) / exp.fr_trials_count()
    frr_std = np.std(boot_results.samples()) / exp.fr_trials_count()
    fig.add_vline(
        x=frr_ci_lower,
        annotation_text=f"lower {CONFIDENCE_PERCENT}%",
        annotation_position="top right",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )
    fig.add_vline(
        x=frr_ci_upper,
        annotation_text=f"upper {CONFIDENCE_PERCENT}%",
        annotation_position="top left",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )
    fig.add_vline(
        x=frr_mean,
        annotation_text="mean",
        annotation_position="top left",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )

    fig.add_vline(
        x=10 / 100,
        annotation_text="10%",
        line_width=2,
        line_dash="dash",
        line_color="red",
    )

    frr_figures[tc] = fig

    section.add_figure(
        "FRR_Bootstrap",
        "The hierarchical FRR bootstrap sampling histogram.",
        fig,
    )

    info.set("FRR_Confidence", CONFIDENCE_PERCENT)
    info.set("FRR_Trials", exp.fr_trials_count())
    info.set("FRR_False_Accepts", exp.fr_count())
    info.set("FRR_CI_Lower", frr_ci_lower)
    info.set("FRR_CI_Upper", frr_ci_upper)
    info.set("FRR_Mean", frr_mean)
    info.set("FRR_Std", frr_std)
    info.set("FRR_Threshold", f"{FRR_THRESHOLD * 100}%")
    info.set("FRR_Pass", frr_ci_upper < FRR_THRESHOLD)

far_figures[DISPLAY_TC].show()
frr_figures[DISPLAY_TC].show()
# %%
rpt_tc[DISPLAY_TC].display(display)

# %%

# fr_counts = pd.DataFrame({c.value: exp.fr_counts_by(c) for c in cols})
# fr_counts.rename(
#     columns={c.value: f'FR Counts by {c.value}' for c in cols}, inplace=True)
# non_zero_labels = fr_counts.loc[(fr_counts > 0).any(axis=1)].index


# fig = ff.create_distplot(
#     [exps[tc].fr_counts_by(Experiment.TableCol.Enroll_Group)
#      for tc in test_cases],
#     [tc.name for tc in test_cases],
#     bin_size=1,
#     curve_type='normal',
#     show_curve=True,
# )
# fig.show()

# fig = ff.create_distplot(
#     [far_boot_results[tc].samples() for tc in test_cases],
#     [tc.name for tc in test_cases],
#     bin_size=10,
#     curve_type='normal',
#     show_curve=True,
# )
# fig.show()
# fig.write_html('big_image.html')

# %%

rpt.generate(
    {
        # 'pdf',
        # 'md',
        "html",
    }
)

# %%
# Upload to x20 website

# https://hesling.users.x20web.corp.google.com/www/fpstudy-demo/output.pdf
# https://hesling.users.x20web.corp.google.com/www/fpstudy-demo/index.html
os.system("rm -rf -v /google/data/rw/users/he/hesling/www/fpstudy-demo")
os.system(
    "cp -r -v data/analysis /google/data/rw/users/he/hesling/www/fpstudy-demo"
)


# %%
# Upload to g3docs for bloonchipper

# /google/src/cloud/hesling/fpc1025-qual/google3
# https://cider.corp.google.com/?ws=hesling/fpc1025-qual

g3docs_path = (
    "/google/src/cloud/hesling/fpc1025-qual/company/teams/"
    "chromeos-fingerprint/development/firmware/testing/Qualification_Results"
    "/bloonchipper/"
    "fpstudy"
)
os.system(f"cp -r -v data/analysis {g3docs_path}")
#############################################################
#############################################################
#############################################################
#############################################################
#############################################################
#############################################################
#############################################################
#############################################################
#############################################################

# %%

# 1000 samples is 95%
# 5000 samples is 99%
# BOOTSTRAP_SAMPLES = 1000
# BOOTSTRAP_SAMPLES = 5000  # 5000 samples in 2 seconds (128 cores)
BOOTSTRAP_SAMPLES = 100000  # 100000 samples in 16 seconds (128 cores)
# CONFIDENCE_PERCENT = 95
CONFIDENCE_PERCENT = 99
PARALLEL_BOOTSTRAP = True

# %% Run FAR bootstrap

boot = bootstrap.BootstrapFullFARHierarchy(exp, verbose=True)
# boot = bootstrap.BootstrapFARFlat(exp, verbose=True)
boot_results = boot.run(
    num_samples=BOOTSTRAP_SAMPLES,
    num_proc=0,
    progress=lambda it, total: tqdm(it, total=total),
)

df = pd.DataFrame({"Sample Means": boot_results.samples()}, dtype=int)
print(df)
print(df.describe())


# %%
display(
    Markdown(
        """
# Raw Bootstrap Samples
"""
    )
)

print("Number Bootstrap Samples:", len(boot_results.samples()))
df = pd.DataFrame({"Bootstrap Counts": boot_results.samples()}, dtype=int)
print(df.describe())

plt.figure()
plt.title(f"Histogram of {len(boot_results.samples())} Bootstrap Samples")
# fpsutils.plt_discrete_hist(boot_means)
fpsutils.plt_discrete_hist2(boot_results.samples())
plt.xlabel("Observed Bootstrap Sample Count")
plt.ylabel("Frequency of Observation")
plt.show()

# Showing raw values works because we take som many bootstrap samples,
# which fills in a lot of gaps in a "unique" value (bin_size=1) histogram.
bins, counts = fpsutils.discrete_hist(boot_results.samples())

fig = px.bar(x=bins, y=counts)
fig.show()

df = pd.DataFrame(
    {
        # X-Axes
        "False Accepts in Bootstrap Sample": bins,
        "FAR": np.array(bins, dtype=float) / exp.fa_trials_count(),
        # Y-Axes
        "Number of Bootstrap Samples Observed": counts,
    }
)
fig = px.bar(
    df,
    x="FAR",
    y="Number of Bootstrap Samples Observed",
    hover_data=["False Accepts in Bootstrap Sample"],
    title="Frequency of FAR in Bootstrap Samples",
)
fig.update_layout(hovermode="x unified")

ci_lower, ci_upper = boot_results.confidence_interval()
frr_ci_lower = ci_lower / exp.fa_trials_count()
frr_ci_upper = ci_upper / exp.fa_trials_count()
frr_mean = np.mean(boot_results.samples()) / exp.fa_trials_count()
fig.add_vline(
    x=frr_ci_lower,
    annotation_text=f"lower {CONFIDENCE_PERCENT}%",
    annotation_position="top right",
    line_width=1,
    line_dash="dash",
    line_color="yellow",
)
fig.add_vline(
    x=frr_ci_upper,
    annotation_text=f"upper {CONFIDENCE_PERCENT}%",
    annotation_position="top left",
    line_width=1,
    line_dash="dash",
    line_color="yellow",
)
fig.add_vline(
    x=frr_mean,
    annotation_text="mean",
    annotation_position="top left",
    line_width=1,
    line_dash="dash",
    line_color="yellow",
)

# fig.add_vline(x=1/50000.0, annotation_text='1/50',
#               line_width=2, line_dash='dash', line_color='red')
fig.add_vline(
    x=1 / 100000.0,
    annotation_text="1/100k",
    line_width=1,
    line_dash="dash",
    line_color="red",
)
fig.add_vline(
    x=1 / 200000.0,
    annotation_text="1/200k",
    line_width=1,
    line_dash="dash",
    line_color="red",
)
fig.show()

# %%

display(
    Markdown(
        """
# FAR Bootstrap Analysis
"""
    )
)

print(f"Total Cross Matches: {exp.fa_trials_count()}")
print(f"Total Cross False Accepts: {exp.fa_count()}")
boot_mean = np.mean(boot_results.samples())  # bootstrapped sample means
print("Mean:", boot_mean)
boot_std = np.std(boot_results.samples())  # bootstrapped std
print("Std:", boot_std)

ci_percent_lower = (100 - CONFIDENCE_PERCENT) / 2
ci_percent_upper = 100 - ci_percent_lower
# 95% C.I.
boot_limits = np.percentile(
    boot_results.samples(), [ci_percent_lower, ci_percent_upper]
)
print("Limits:", boot_limits)
limit_diff = boot_limits[1] - boot_limits[0]
print("Limits Diff:", limit_diff)


#################

boot_means_ratio = np.divide(boot_results.samples(), exp.fa_trials_count())

# far_limits = np.array([
#     boot_limits[0] / float(exp.FARMatches()),
#     boot_limits[1] / float(exp.FARMatches()),
# ])
far_limits = np.divide(boot_limits, exp.fa_trials_count())
frr_mean = np.divide(boot_mean, exp.fa_trials_count())
frr_std = np.divide(boot_std, exp.fa_trials_count())

plt.figure()
# fpsutils.plt_discrete_hist(np.divide(boot_means, exp.FARMatches()))

# Fit a normal distribution to the data:
# mean and standard deviation
mu, std = norm.fit(boot_means_ratio)
# Plot the histogram.
bins = 25
# bins=50
plt.hist(boot_means_ratio, bins=bins, density=True, alpha=0.6, color="b")

# Plot the PDF.
xmin, xmax = plt.xlim()
x = np.linspace(xmin, xmax, 100)
p = norm.pdf(x, mu, std)

plt.plot(x, p, "k", linewidth=2)
plt.title(f"Fit Values: {mu} and {std}")

plt.axvline(x=far_limits[0], color="blue")
plt.axvline(x=far_limits[1], color="blue")
plt.axvline(x=1 / 100000.0, color="red")
plt.text(1 / 100000.0, 100000, "1/100k", rotation=90, color="red")
plt.axvline(x=1 / 200000.0, color="red")
plt.text(1 / 200000.0, 100000, "1/200k", rotation=90, color="red")

plt.show()

print("FAR Mean:", frr_mean)
print("FAR Std:", frr_std)

print(f"FAR Limits: {far_limits} <= 1/100k = {far_limits <= 1/100000.0}")
print(
    "FAR Limits:",
    [
        print_far_value(far_limits[0], [100]),
        print_far_value(far_limits[1], [100]),
    ],
)
print(f"FAR Limits 1/{1/far_limits}")
print(f"FAR Limits {far_limits <= 1/100000.0}")

# rpt.add_text(f'FAR Mean: {far_mean}')
# rpt.add_text(f'FAR Std: {far_std}')
# rpt.add_text(
#     f'FAR Limits: {far_limits} <= 1/100k = {far_limits <= 1/100000.0}')
# rpt.add_text(
#     f'FAR Limits: {[print_far_value(far_limits[0], [100]),print_far_value(far_limits[1], [100])]}')
# rpt.add_text(f'FAR Limits 1/{1/far_limits}')
# rpt.add_text(f'FAR Limits {far_limits <= 1/100000.0}')


###############################################################################
# %%

rpt.generate({"pdf", "md", "html"})

# %%
