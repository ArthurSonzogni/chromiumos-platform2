#!/usr/bin/env python3
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A utility for generating classes for structured metrics events.

Takes as input a structured.xml file describing all events and produces a C++
header and implementation file exposing builders for those events.
"""

import argparse
import sys

import codegen
from sync import model
import templates


parser = argparse.ArgumentParser(
    description="Generate structured metrics events"
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
            "structured_events.h",
            file_template=templates.HEADER_FILE_TEMPLATE,
            project_template=templates.HEADER_PROJECT_TEMPLATE,
            event_template=templates.HEADER_EVENT_TEMPLATE,
            pre_metric_template="",
            metric_template=templates.HEADER_METRIC_TEMPLATE,
            array_template=templates.HEADER_ARRAY_LENGTH_TEMPLATE,
            is_header=True,
        ).write_file()

        codegen.Template(
            data,
            args.output,
            "structured_events.cc",
            file_template=templates.IMPL_FILE_TEMPLATE,
            project_template=templates.IMPL_PROJECT_TEMPLATE,
            event_template=templates.IMPL_EVENT_TEMPLATE,
            pre_metric_template="",
            metric_template=templates.IMPL_METRIC_TEMPLATE,
            array_template=templates.IMPL_ARRAY_METRIC_TEMPLATE,
            is_header=False,
        ).write_file()

    return 0


if __name__ == "__main__":
    sys.exit(main())
