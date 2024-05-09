#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A utility for generating the structured metrics command-line parser.

Takes as input a structured.xml file describing all events and produces a C++
implementation file which sends a structured metric based on command-line
arguments.
"""

import argparse
import sys

import codegen
from sync import model
import templates_metrics_client


parser = argparse.ArgumentParser(
    description="Generate structured metrics parser for metrics_client"
)
parser.add_argument("--input", help="Path to structured.xml")
parser.add_argument("--output", help="Path to generated files.")


def main():
    args = parser.parse_args()
    with open(args.input, encoding="utf-8") as f:
        data = model.Model(f.read())

        codegen.Template(
            data,
            args.output,
            "metrics_client_structured_events.cc",
            file_template=templates_metrics_client.FILE_TEMPLATE,
            project_template=templates_metrics_client.PROJECT_TEMPLATE,
            event_template=templates_metrics_client.EVENT_TEMPLATE,
            pre_metric_template=templates_metrics_client.PRE_METRIC_TEMPLATE,
            metric_template=templates_metrics_client.METRIC_TEMPLATE,
            array_template=templates_metrics_client.ARRAY_METRIC_TEMPLATE,
            is_header=False,
        ).write_file()

    return 0


if __name__ == "__main__":
    sys.exit(main())
