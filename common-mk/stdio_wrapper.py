#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper of protoc command line to handle stdin and stdout."""

import argparse
import contextlib
from pathlib import Path
import subprocess
import sys
from typing import List, Optional


def get_parser() -> argparse.ArgumentParser:
    """Build the argument parser."""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--stdin", default=None, type=Path, help="file to use as stdin"
    )

    parser.add_argument(
        "--stdout", default=None, type=Path, help="file to use as stdout"
    )

    parser.add_argument(
        "--stderr", default=None, type=Path, help="file to use as stderr"
    )

    parser.add_argument("cmdargs", nargs="+", help="args of command to run")

    return parser


def parse_arguments(argv: List[str]) -> argparse.Namespace:
    """Parse and validate arguments."""
    parser = get_parser()
    opts = parser.parse_args(argv)
    return opts


def main(argv: List[str]) -> Optional[int]:
    opts = parse_arguments(argv)

    in_file = None
    out_file = None
    err_file = None
    kwargs = {}

    with contextlib.ExitStack() as stack:
        if opts.stdin:
            in_file = stack.enter_context(open(opts.stdin, "rb"))
            kwargs["stdin"] = in_file
        if opts.stdout:
            out_file = stack.enter_context(open(opts.stdout, "wb"))
            kwargs["stdout"] = out_file
        if opts.stderr:
            err_file = stack.enter_context(open(opts.stderr, "wb"))
            kwargs["stderr"] = err_file

        subprocess.check_call(opts.cmdargs, **kwargs)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
