#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test and benchmark the Bootstrap module."""

from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest

import numpy as np
import plotly.express as px
import plotly.figure_factory as ff
import plotly.graph_objects as go
from report_pandoc import Report2


class Test_Report(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.temp_dir_path = Path(self.temp_dir.name)
        print(f"Temp output dir is {self.temp_dir_path}.")
        return super().setUp()

    def tearDown(self) -> None:
        self.temp_dir.cleanup()
        return super().tearDown()

    def test_blank_template(self):
        """Test rendering and writing an empty report."""

        rpt = Report2(self.temp_dir_path)
        rpt.generate()

        print("Temp Dir Contents:")
        os.system(f"ls {self.temp_dir_path}")


def test_report(dir: Path):
    test_cases = [
        "TestCase1",
        "TestCase2-Enabled",
        "TestCase2-Disabled",
        "TestCase3-Enabled",
        "TestCase3-Disabled",
    ]

    rpt = Report2(dir)
    for tc_name in test_cases:
        tc = rpt.test_case_add(
            tc_name,
            description=f"The description for {tc_name}.",
        )
        rng = np.random.default_rng()

        fa_titles: dict[str, str] = {
            # False Accepts
            "FA_by_User": "False Accepts by Enroll User ID and Verify User ID",
            "FA_by_Finger": "False Accepts by Enroll Finger ID and Verify Finger ID",
            "FA_by_Sample": "False Accepts by Verify Sample ID",
            "FA_by_Group": "False Accepts by Enroll Group and Verify Group",
        }
        fr_titles: dict[str, str] = {
            # False Rejects
            "FR_by_User": "False Rejects by User ID",
            "FR_by_Finger": "False Rejects by Finger ID",
            "FR_by_Sample": "False Rejects by Sample ID",
            "FR_by_Group": "False Rejects by Group",
        }

        histograms = tc.add_subsection(
            "hist",
            "Histograms",
        )

        score = tc.add_subsection(
            "score",
            "Bootstrap Samples",
        )

        # FA Histogram Plots
        for name, title in fa_titles.items():
            histograms.add_figure(
                name,
                title,
                px.histogram(
                    rng.choice(100, size=(2000,), replace=True), title=title
                ),
            )

        # FR Histogram Plots
        for name, title in fr_titles.items():
            histograms.add_figure(
                name,
                title,
                px.histogram(
                    rng.choice(100, size=(2000,), replace=True), title=title
                ),
            )

        # FAR Bootstrap
        x1 = np.random.randn(200)
        group_labels = ["Group 1"]
        colors = ["slategray", "magenta"]
        # Create distplot with curve_type set to 'normal'
        fig = ff.create_distplot(
            [x1],
            group_labels,
            bin_size=0.5,
            curve_type="normal",  # override default 'kde'
            # colors=colors,
        )
        fig.update_layout(title_text="Distplot with Normal Distribution")
        # fig.show()
        score.add_figure("FAR_Bootstrap", "FAR Bootstrap Distribution", fig)
        score.add_figure(
            "FRR_Bootstrap", "FRR Bootstrap Distribution", go.Figure(fig)
        )

    rpt.generate(["html"])


if __name__ == "__main__":
    test_report_path = Path("test_report")
    test_report_path.mkdir(parents=True, exist_ok=True)
    print(f"Test report in dir {test_report_path}.")
    test_report(test_report_path)

    # unittest.main()
