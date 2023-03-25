#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The main command-line interface module for CCA.

Run `cca help` for more information about the supported subcommands.
"""

import argparse
import codecs
import enum
import logging
import pathlib
import shutil
import sys
from typing import List, Optional, Type

from cros_camera_app import app
from cros_camera_app import device


class EnumAction(argparse.Action):
    """Action that converts between the string choices and Enum for argparse."""

    def __init__(
        self,
        option_strings: str,
        dest: str,
        enum_type: Type[enum.Enum],
        **kwargs,
    ):
        """Initializes the instance.

        Args:
            option_strings: The option strings that trigger this action.
            dest: The name of the attribute to hold the selected enum value.
            enum_type: The enum class to use for argument choices.
            **kwargs: Additional keyword arguments to pass to argparse.Action.
        """
        self._enum_type = enum_type
        kwargs["choices"] = [e.name.lower() for e in enum_type]
        super().__init__(option_strings, dest, **kwargs)

    def __call__(
        self,
        parser: argparse.ArgumentParser,
        namespace: argparse.Namespace,
        values: str,
        option_string=None,
    ):
        """Converts the selected value to an enum and updates the namespace.

        Args:
            parser: The argument parser instance.
            namespace: The namespace to hold the selected enum value.
            values: The selected value as a string.
            option_string: The option string that triggered this action.
        """
        del parser  # unused
        del option_string  # unused
        enum_value = self._enum_type[values.upper()]
        setattr(namespace, self.dest, enum_value)


def cmd_setup(args: argparse.Namespace):
    del args
    device.setup()


# TODO(shik): Wake up the display if it's sleeping.
def cmd_open(args: argparse.Namespace):
    cca = app.CameraApp()
    cca.open(facing=args.facing, mode=args.mode)


def cmd_close(args: argparse.Namespace):
    del args  # unused
    cca = app.CameraApp()
    cca.close()


# TODO(shik): Provide an option to reuse the existing CCA session and not to
# close the app afterward.
def cmd_take_photo(args: argparse.Namespace):
    cca = app.CameraApp()
    path = cca.take_photo(facing=args.facing)
    if args.output:
        shutil.copy2(path, args.output)
        logging.info("Copied photo from %s to %s", path, args.output)
    else:
        logging.info("Saved photo at %s", path)


def cmd_record_video(args: argparse.Namespace):
    cca = app.CameraApp()
    path = cca.record_video(facing=args.facing, duration=args.duration)
    if args.output:
        shutil.copy2(path, args.output)
        logging.info("Copied video from %s to %s", path, args.output)
    else:
        logging.info("Saved video at %s", path)


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

    setup_parser = subparsers.add_parser(
        "setup",
        help="Setup the DUT",
        description="Setup the DUT to make it ready to be controlled remotely.",
    )
    setup_parser.set_defaults(func=cmd_setup)

    open_parser = subparsers.add_parser(
        "open",
        help="Open CCA",
        description="Open CCA.",
    )
    open_parser.add_argument(
        "--facing",
        help="facing of the camera to be opened",
        action=EnumAction,
        enum_type=app.Facing,
    )
    open_parser.add_argument(
        "--mode",
        help="target capture mode in app",
        action=EnumAction,
        enum_type=app.Mode,
    )
    open_parser.set_defaults(func=cmd_open)

    close_parser = subparsers.add_parser(
        "close",
        help="Close CCA",
        description="Close CCA if it's open.",
    )
    close_parser.set_defaults(func=cmd_close)

    take_photo_parser = subparsers.add_parser(
        "take-photo",
        help="Take a photo",
        description="Take a photo using CCA.",
    )
    take_photo_parser.add_argument(
        "--facing",
        help="facing of the camera to be captured",
        action=EnumAction,
        enum_type=app.Facing,
    )
    take_photo_parser.add_argument(
        "--output",
        help="output path to save the photo",
        type=pathlib.Path,
    )
    take_photo_parser.set_defaults(func=cmd_take_photo)

    record_video_parser = subparsers.add_parser(
        "record-video",
        help="Record a video",
        description="Record a video using CCA.",
    )
    record_video_parser.add_argument(
        "--facing",
        help="facing of the camera to be recorded",
        action=EnumAction,
        enum_type=app.Facing,
    )
    record_video_parser.add_argument(
        "--duration",
        help="duration in seconds to be recorded",
        type=float,
        default=3,
    )
    record_video_parser.add_argument(
        "--output",
        help="output path to save the video",
        type=pathlib.Path,
    )
    record_video_parser.set_defaults(func=cmd_record_video)

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
