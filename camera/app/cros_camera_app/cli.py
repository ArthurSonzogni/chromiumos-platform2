#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The main command-line interface module for CCA.

Run `cca help` for more information about the supported subcommands.
"""

import argparse
import codecs
import logging
import sys
from typing import List, Optional

from cros_camera_app import app


# TODO(shik): Support specifying target mode and facing.
# TODO(shik): Wake up the display if it's sleeping.
def cmd_open(args: argparse.Namespace):
    del args  # unused for now
    cca = app.CameraApp()
    cca.open()


def cmd_close(args: argparse.Namespace):
    del args  # unused
    cca = app.CameraApp()
    cca.close()


def parse_args(argv: Optional[List[str]]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="ChromeOS Camera App (CCA) CLI."
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="enable debug logging",
    )
    parser.set_defaults(func=lambda _: parser.print_help())
    subparsers = parser.add_subparsers()

    open_parser = subparsers.add_parser(
        "open",
        help="Open CCA",
        description="Open CCA.",
    )
    open_parser.set_defaults(func=cmd_open)

    close_parser = subparsers.add_parser(
        "close",
        help="Close CCA",
        description="Close CCA if it's open.",
    )
    close_parser.set_defaults(func=cmd_close)
    return parser.parse_args(argv)


def setup_logging(debug: bool):
    # ChromeOS shell might use C locale instead of UTF-8, which may trigger
    # encoding error when printing non-ASCII characters. Here we enforce stdout
    # and stderr to use UTF-8 encoding.
    if codecs.lookup(sys.stdout.encoding).name != "utf-8":
        # pylint: disable=consider-using-with
        sys.stdout = open(
            sys.stdout.fileno(), mode="w", encoding="utf-8", buffering=1
        )

    if codecs.lookup(sys.stderr.encoding).name != "utf-8":
        # pylint: disable=consider-using-with
        sys.stderr = open(
            sys.stderr.fileno(), mode="w", encoding="utf-8", buffering=1
        )

    log_level = logging.DEBUG if debug else logging.INFO
    log_format = "%(asctime)s - %(levelname)s - %(funcName)s: %(message)s"
    logging.basicConfig(level=log_level, format=log_format)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    args = parse_args(argv)
    setup_logging(args.debug)
    logging.debug("args = %s", args)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
