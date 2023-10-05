#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper to handle stdio & env vars."""

import argparse
import contextlib
import os
from pathlib import Path
import subprocess
import sys
from typing import List, Optional


class _EnvAdd(argparse.Action):
    """Add a KEY=VAL to an env dictionary."""

    def __init__(self, option_strings, dest, **kwargs):
        if "nargs" in kwargs:
            raise ValueError(f"nargs is not supported for {dest}")
        super().__init__(option_strings, dest, nargs=1, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        if getattr(namespace, self.dest, None) is None:
            setattr(namespace, self.dest, {})
        if "=" not in values[0]:
            parser.error(
                f"{option_string} must use the form KEY=VALUE, not: {values[0]}"
            )
        key, value = values[0].split("=", 1)
        getattr(namespace, self.dest)[key] = value


class _EnvDel(argparse.Action):
    """Remove a var from an env dictionary."""

    def __init__(self, option_strings, dest, **kwargs):
        if "nargs" in kwargs:
            raise ValueError(f"nargs is not supported for {dest}")
        super().__init__(option_strings, dest, nargs=1, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        if getattr(namespace, self.dest, None) is None:
            setattr(namespace, self.dest, {})
        # Mark the variable for removal so that we can process all the args at
        # the end and make sure we handle `--set-env F=1 --unset-env F`.
        getattr(namespace, self.dest)[values[0]] = None


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

    parser.add_argument(
        "--set-env",
        dest="extra_env",
        action=_EnvAdd,
        metavar="KEY=VAL",
        help="set variable in environment",
    )

    parser.add_argument(
        "--unset-env",
        dest="extra_env",
        action=_EnvDel,
        metavar="VAR",
        help="unset variable in environment",
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
    print(opts.extra_env)

    in_file = None
    out_file = None
    err_file = None
    kwargs = {}

    if opts.extra_env:
        env = os.environ.copy()
        env.update(opts.extra_env)
        for key, value in opts.extra_env.items():
            if value is None:
                env.pop(key)
        kwargs["env"] = env

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
