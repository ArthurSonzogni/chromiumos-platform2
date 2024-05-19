#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import argparse
import pathlib
import sys

import bootstrap
from experiment import Experiment
from fpc_bet_results import FPCBETResults
import fpsutils
import numpy as np
import pandas as pd
import plotly.express as px
import plotly.graph_objs as go
from report_pandoc import Report2
import scipy.stats as st
from test_case import TestCase
from tqdm.autonotebook import tqdm  # Auto detect notebook or console.


def print_far_value(
    val: float,
    k_comparisons: list[int] = [20, 50, 100, 500],
) -> str:
    """Print the science notation"""
    str = f"{val:.4e}"
    for c in k_comparisons:
        str += f" = {val * 100 * c*1000:.3f}% of 1/{c}k"
    # TODO: Make nice output like "1 in 234k".
    return str


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


def run(opts: argparse.Namespace) -> int:
    learn_groups_dir: pathlib.Path = opts.learn_groups_dir
    testcase_decisions_dir: pathlib.Path = opts.testcase_decisions_dir
    analysis_dir: pathlib.Path = opts.analysis_dir

    analysis_dir.mkdir(exist_ok=True)
    source_dir = pathlib.Path(__file__).parent
    rpt = Report2(analysis_dir, source_dir / "templates")

    # # Find Data
    # # We expect a symbolic link named `data` to point to the directory that
    # # contains the FPC BET reports and raw collection.
    # print('# Discover Root Data Directory')
    # data_root = pathlib.Path(os.readlink('data'))
    # if not data_root.is_dir():
    #     raise Exception('Data root doesn\'t exist')
    # print(f'Using root {data_root}')

    # # BET_REPORT_DIR = str(data_root.joinpath('report2'))
    # BET_REPORT_DIR = data_root.joinpath('report-100k')
    # COLLECTION_DIR = data_root.joinpath('orig-data')

    ################# Import Data From BET Results #################

    print("# Read in data")

    bet = FPCBETResults(testcase_decisions_dir)

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
        test_cases[i]: Experiment(
            num_verification=80,
            num_fingers=6,
            num_users=72,
            far_decisions=far,
            frr_decisions=frr,
        )
        for i, (far, frr) in enumerate(zip(far_decisions, frr_decisions))
    }

    # Learn group information from the subdirectory structure of the raw collection directory.
    for tc in exps:
        exps[tc].add_groups_from_collection_dir(learn_groups_dir)

    ################# Generate Report Test cases #################

    print("# Setup report test cases")

    rpt_tc = {
        tc: rpt.test_case_add(str(tc), tc.description()) for tc in test_cases
    }

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

    ################# Histograms #################

    print("# Add main histograms to report")

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
            [
                Experiment.TableCol.Enroll_Finger,
                Experiment.TableCol.Verify_Finger,
            ],
            "False Accepts by Finger ID of Enroll and Verify",
            "Finger ID",
        )
        section.add_figure(
            "FA_by_Finger",
            "False Accepts by Finger ID of Enroll and Verify.",
            fig,
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
            [
                Experiment.TableCol.Enroll_Group,
                Experiment.TableCol.Verify_Group,
            ],
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

    # rpt_tc[DISPLAY_TC].display(display)

    print("# Add remaining histograms to report")

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
    # fig.show()
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
    # fig.show()
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
    # fig.show()
    rpt.overall_section().add_figure(
        "FA_by_Sample", "False Accepts by Sample", fig
    )

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
    # fig.show()
    rpt.overall_section().add_figure(
        "FA_by_Finger", "False Accepts by Finger", fig
    )

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
    # fig.show()
    rpt.overall_section().add_figure(
        "FR_by_Finger", "False Rejects by Finger", fig
    )

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
    # fig.show()
    rpt.overall_section().add_figure(
        "FA_by_Group", "False Accepts by Group", fig
    )

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
    # fig.show()
    rpt.overall_section().add_figure(
        "FR_by_Group", "False Rejects by Group", fig
    )

    ################# Bootstrap Sampling #################

    print("# Run bootstrap samples")

    # 1000 samples is 95%
    # 5000 samples is 99%
    # BOOTSTRAP_SAMPLES = 1000
    # BOOTSTRAP_SAMPLES = 5000  # 5000 samples in 2 seconds (128 cores)
    BOOTSTRAP_SAMPLES = 100000  # 100000 samples in 16 seconds (128 cores)
    CONFIDENCE_PERCENT = 95
    PARALLEL_BOOTSTRAP = True
    FAR_THRESHOLD = 1 / 100000.0
    FRR_THRESHOLD = 10 / 100.0

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

    # far_figures[DISPLAY_TC].show()
    # frr_figures[DISPLAY_TC].show()
    # %%
    # rpt_tc[DISPLAY_TC].display(display)

    #### Generate Report ####

    print("# Generate final report")

    rpt.generate(
        {
            # 'pdf',
            # 'md',
            "html",
        }
    )

    print(f"View {(analysis_dir / 'index.html').absolute}")

    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--learn-groups-dir",
        # default="",
        required=True,
        type=pathlib.Path,
        help="Path to raw collection directory where we can learn the participant groups from",
    )
    parser.add_argument(
        "testcase_decisions_dir",
        metavar="testcase-decisions-dir",
        type=pathlib.Path,
        help="Directory that holds the matcher decisions for each test case",
    )
    parser.add_argument(
        "analysis_dir",
        metavar="analysis-dir",
        default="analysis",
        type=pathlib.Path,
        help="Directory to output the analysis report",
    )
    opts = parser.parse_args(argv)

    if not opts.learn_groups_dir.is_dir():
        parser.error("learn-groups-dir must be a directory")
    if not opts.testcase_decisions_dir.is_dir():
        parser.error("testcase-decisions-dir must be a directory")
    if opts.analysis_dir.is_file():
        parser.error("testcase-decisions-dir must be a directory")

    return run(opts)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
