#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A statistical analysis tool for fingerprint evaluation tool results.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
from typing import Literal, Optional

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


# 1000 samples is 95%
# 5000 samples is 99%
# BOOTSTRAP_SAMPLES = 1000
# BOOTSTRAP_SAMPLES = 5000  # 5000 samples in 2 seconds (128 cores)
BOOTSTRAP_SAMPLES = 100000  # 100000 samples in 16 seconds (128 cores)
CONFIDENCE_PERCENT = 95
FAR_THRESHOLD = 1 / 100000.0
FRR_THRESHOLD = 10 / 100.0


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


class BootstrapFARFRRStats:
    class _Value(float):
        def fmt_far(
            self,
            fmt: Literal["k", "s"] = "k",
            decimal_places: int = 3,
        ) -> str:
            return fpsutils.fmt_far(self, fmt, decimal_places)

        def fmt_frr(self, decimal_places: int = 3) -> str:
            return fpsutils.fmt_frr(self, decimal_places)

    confidence_percent: float
    mean: _Value
    std: _Value
    ci_lower: _Value
    ci_upper: _Value

    def __init__(
        self,
        boot_results: bootstrap.BootstrapResults,
        num_trails: int,
        confidence_percent: float = 95,
    ):
        self.confidence_percent = confidence_percent
        self.mean = self._Value(np.mean(boot_results.samples()) / num_trails)
        self.std = self._Value(np.std(boot_results.samples()) / num_trails)
        boot_ci_lower, boot_ci_upper = boot_results.confidence_interval(
            self.confidence_percent
        )
        self.ci_lower = self._Value(boot_ci_lower / num_trails)
        self.ci_upper = self._Value(boot_ci_upper / num_trails)

    def describe(
        self,
        test_type: Literal["FAR", "FRR"],
        far_fmt: Literal["k", "s"] = "k",
        decimal_places: int = 3,
    ) -> str:
        if test_type not in {"FAR", "FRR"}:
            raise ValueError(f"Invalid test type: {test_type}")

        if test_type == "FAR":
            str_mean = self.mean.fmt_far(far_fmt, decimal_places)
            str_std = self.std.fmt_far(far_fmt, decimal_places)
            str_ci_lower = self.ci_lower.fmt_far(far_fmt, decimal_places)
            str_ci_upper = self.ci_upper.fmt_far(far_fmt, decimal_places)
        else:
            str_mean = self.mean.fmt_frr(decimal_places)
            str_std = self.std.fmt_frr(decimal_places)
            str_ci_lower = self.ci_lower.fmt_frr(decimal_places)
            str_ci_upper = self.ci_upper.fmt_frr(decimal_places)

        desc = f"{test_type} {str_mean} ± {str_std} "
        desc += f"({str_ci_lower}, {str_ci_upper}) at "
        desc += f"{self.confidence_percent}% confidence."
        return desc


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


def bootstrap_figure(
    exp: Experiment,
    boot_results: bootstrap.BootstrapResults,
    test_type: Literal["FAR", "FRR"],
) -> go.Figure:
    """Generate an FAR or FRR bootstrap results histogram."""

    if test_type not in {"FAR", "FRR"}:
        raise ValueError("test_type must be FAR or FRR")

    is_far = test_type == "FAR"
    accepts_or_rejects = "Accepts" if is_far else "Rejects"

    # Showing raw values works because we take a lot of bootstrap samples,
    # which fills in a lot of gaps in a "unique" value (bin_size=1) histogram.
    bins, counts = fpsutils.discrete_hist(boot_results.samples())
    trials = exp.fa_trials_count() if is_far else exp.fr_trials_count()
    df = pd.DataFrame(
        {
            # X-Axes
            f"False {accepts_or_rejects} in Bootstrap Sample": bins,
            test_type: np.array(bins, dtype=float) / trials,
            # Y-Axes
            "Number of Bootstrap Samples Observed": counts,
        }
    )
    fig: go.Figure = px.bar(
        df,
        x=test_type,
        y="Number of Bootstrap Samples Observed",
        hover_data=[f"False {accepts_or_rejects} in Bootstrap Sample"],
        title=f"Frequency of {test_type} in Bootstrap Samples",
    )
    if is_far:
        fig.update_layout(hovermode="x unified")
    else:
        fig.update_layout(
            hovermode="x unified",
            xaxis=dict(tickformat="%"),
            bargap=0.0,
            bargroupgap=0.0,
        )

    boot_stats = BootstrapFARFRRStats(boot_results, trials, CONFIDENCE_PERCENT)

    fig.add_vline(
        x=boot_stats.ci_lower,
        annotation_text=f"lower {boot_stats.confidence_percent}%",
        annotation_position="top right",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )
    fig.add_vline(
        x=boot_stats.ci_upper,
        annotation_text=f"upper {boot_stats.confidence_percent}%",
        annotation_position="top left",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )
    fig.add_vline(
        x=boot_stats.mean,
        annotation_text="mean",
        annotation_position="top left",
        line_width=1,
        line_dash="dash",
        line_color="yellow",
    )

    if is_far:
        # Enable this to compare against 1/50k FAR.
        # Enabling this has the undesirable side effect of shifting
        # focus of the histogram plots to include 1/50k, which might
        # be very far from the mean.
        # fig.add_vline(
        #     x=1 / 50000.0,
        #     annotation_text="1/50",
        #     line_width=2,
        #     line_dash="dash",
        #     line_color="red",
        # )
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
    else:
        # Enable this to compare against 2% FRR.
        # fig.add_vline(
        #     x=2 / 100,
        #     annotation_text="2%",
        #     line_width=2,
        #     line_dash="dash",
        #     line_color="red",
        # )
        fig.add_vline(
            x=10 / 100,
            annotation_text="10%",
            line_width=2,
            line_dash="dash",
            line_color="red",
        )

    return fig


def cmd_analyze(opts: argparse.Namespace) -> int:
    """Analyze the FAR and FRR of one test case.

    This directory is expected to contain FAR_decisions.csv and
    FRR_decisions.csv files.
    """
    decisions_dir: pathlib.Path = opts.decisions_dir

    far_decisions_file = decisions_dir / "FAR_decisions.csv"
    frr_decisions_file = decisions_dir / "FRR_decisions.csv"

    exp = Experiment(0, 0, 0)
    exp.add_far_decisions_from_csv(far_decisions_file)
    exp.add_frr_decisions_from_csv(frr_decisions_file)

    far_boot = bootstrap.BootstrapFullFARHierarchy(exp, verbose=True)
    far_boot_results = far_boot.run(
        num_samples=BOOTSTRAP_SAMPLES,
        num_proc=0,
        progress=lambda it, total: tqdm(it, total=total),
    )
    frr_boot = bootstrap.BootstrapFullFRRHierarchy(exp, verbose=True)
    frr_boot_results = frr_boot.run(
        num_samples=BOOTSTRAP_SAMPLES,
        num_proc=0,
        progress=lambda it, total: tqdm(it, total=total),
    )

    far_stats = BootstrapFARFRRStats(
        far_boot_results,
        exp.fa_trials_count(),
        CONFIDENCE_PERCENT,
    )
    frr_stats = BootstrapFARFRRStats(
        frr_boot_results,
        exp.fr_trials_count(),
        CONFIDENCE_PERCENT,
    )

    print("-----------------------------------------")
    print(far_stats.describe("FAR", "k"))
    print(frr_stats.describe("FRR"))

    far_fig = bootstrap_figure(exp, far_boot_results, "FAR")
    frr_fig = bootstrap_figure(exp, frr_boot_results, "FRR")

    far_fig.show()
    frr_fig.show()

    return 0


def cmd_report(opts: argparse.Namespace) -> int:
    """Conduct a full analysis of all test cases and generate a final report."""
    user_groups_csv: Optional[pathlib.Path] = opts.user_groups_csv
    testcases_decisions_dir: pathlib.Path = opts.testcases_decisions_dir
    analysis_dir: pathlib.Path = opts.analysis_dir

    if not user_groups_csv:
        user_groups_csv = testcases_decisions_dir / "User_groups.csv"

    analysis_dir.mkdir(exist_ok=True)
    source_dir = pathlib.Path(__file__).parent
    rpt = Report2(analysis_dir, source_dir / "templates")

    ################# Import Data From BET Results #################

    print("# Read in data")

    bet = FPCBETResults(testcases_decisions_dir)

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

    for tc in exps:
        exps[tc].add_groups_from_csv(user_groups_csv)

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
        fig = bootstrap_figure(exp, boot_results, "FAR")
        far_figures[tc] = fig
        section.add_figure(
            "FAR_Bootstrap",
            "The hierarchical FAR bootstrap sampling histogram.",
            fig,
        )

        far_stats = BootstrapFARFRRStats(
            boot_results,
            exp.fa_trials_count(),
            CONFIDENCE_PERCENT,
        )

        info.set("FAR_Confidence", far_stats.confidence_percent)
        info.set("FAR_Trials", exp.fa_trials_count())
        info.set("FAR_False_Accepts", exp.fa_count())
        info.set("FAR_CI_Lower", far_stats.ci_lower)
        info.set("FAR_CI_Upper", far_stats.ci_upper)
        info.set("FAR_Mean", far_stats.mean)
        info.set("FAR_Std", far_stats.std)
        info.set("FAR_Threshold", f"1/{1 / (FAR_THRESHOLD*1000)}k")
        info.set("FAR_Pass", far_stats.ci_upper < FAR_THRESHOLD)

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
        fig = bootstrap_figure(exp, boot_results, "FRR")
        frr_figures[tc] = fig

        section.add_figure(
            "FRR_Bootstrap",
            "The hierarchical FRR bootstrap sampling histogram.",
            fig,
        )

        frr_stats = BootstrapFARFRRStats(
            boot_results,
            exp.fr_trials_count(),
            CONFIDENCE_PERCENT,
        )

        info.set("FRR_Confidence", frr_stats.confidence_percent)
        info.set("FRR_Trials", exp.fr_trials_count())
        info.set("FRR_False_Accepts", exp.fr_count())
        info.set("FRR_CI_Lower", frr_stats.ci_lower)
        info.set("FRR_CI_Upper", frr_stats.ci_upper)
        info.set("FRR_Mean", frr_stats.mean)
        info.set("FRR_Std", frr_stats.std)
        info.set("FRR_Threshold", f"{FRR_THRESHOLD * 100}%")
        info.set("FRR_Pass", frr_stats.ci_upper < FRR_THRESHOLD)

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


def cmd_groups_discover(opts: argparse.Namespace) -> int:
    """Discover the user-group mapping from a raw collection dir structure.

    Write this table out to a CSV file, which typically is called
    User_groups.csv.
    """
    src_collection_dir: pathlib.Path = opts.src_collection_dir
    user_groups_csv: pathlib.Path = opts.user_groups_csv

    exp = Experiment(0, 0, 0)
    exp.add_groups_from_collection_dir(src_collection_dir)
    exp.user_groups_table_to_csv(user_groups_csv)
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers = parser.add_subparsers(
        dest="subcommand", required=True, title="subcommands"
    )

    # Parser for "analyze" subcommand.
    parser_analyze = subparsers.add_parser(
        "analyze",
        help=cmd_analyze.__doc__,
    )
    parser_analyze.set_defaults(func=cmd_analyze)
    parser_analyze.add_argument(
        "decisions_dir",
        type=pathlib.Path,
        help="Directory that holds the matcher decisions for one test case",
    )

    # Parser for "report" subcommand.
    parser_report = subparsers.add_parser("report", help=cmd_report.__doc__)
    parser_report.set_defaults(func=cmd_report)
    parser_report.add_argument(
        "--user-groups-csv",
        type=pathlib.Path,
        help="Path to the user-group mapping CSV file. "
        "(default: <testcases_decisions_dir>/User_groups.csv).",
    )
    parser_report.add_argument(
        "testcases_decisions_dir",
        type=pathlib.Path,
        help="Directory of directories that holds the matcher decisions for each test case",
    )
    parser_report.add_argument(
        "analysis_dir",
        default="analysis",
        type=pathlib.Path,
        help="Directory to output the analysis report",
    )

    # Parser for "groups-discover" subcommand.
    parser_groups_discover = subparsers.add_parser(
        "groups-discover", help=cmd_groups_discover.__doc__
    )
    parser_groups_discover.set_defaults(func=cmd_groups_discover)
    parser_groups_discover.add_argument(
        "src_collection_dir",
        type=pathlib.Path,
        help="Path to raw collection directory where we will learn the "
        "participant groups from",
    )
    parser_groups_discover.add_argument(
        "user_groups_csv",
        type=pathlib.Path,
        default="User_groups.csv",
        help="The path to the User_groups.csv we will write to",
    )

    args = parser.parse_args(argv)

    if args.subcommand == "analyze":
        if not args.decisions_dir.is_dir():
            parser.error("decisions_dir must be a directory")
    elif args.subcommand == "report":
        if args.user_groups_csv and not args.user_groups_csv.is_file():
            parser.error("user-groups-csv must be a CSV file")
        if not args.testcases_decisions_dir.is_dir():
            parser.error("testcases_decisions_dir must be a directory")
        if args.analysis_dir.exists() and not args.analysis_dir.is_dir():
            parser.error("analysis_dir must be a directory")
    elif args.subcommand == "groups-discover":
        if not args.src_collection_dir.is_dir():
            parser.error("src_collection_dir must be a directory")
        if args.user_groups_csv.exists():
            parser.error(
                f"user_groups_csv {args.user_groups_csv} already exists"
            )

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
