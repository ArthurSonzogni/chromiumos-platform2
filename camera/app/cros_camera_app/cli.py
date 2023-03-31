#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The main command-line interface module for CCA.

Run `cca help` for more information about the supported subcommands.
"""

import argparse
import codecs
import collections
import enum
import functools
import inspect
import logging
import pathlib
import shutil
import sys
from typing import Callable, List, Optional, Type

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


class CLIRunner:
    """Runner to parse command line arguments and dispatch commands."""

    def __init__(self):
        """Initializes the instance."""

        parser = argparse.ArgumentParser(
            description="ChromeOS Camera App (CCA) CLI."
        )
        parser.add_argument(
            "--debug",
            action="store_true",
            help="enable debug logging",
        )
        parser.set_defaults(func=lambda _: parser.print_help())

        self._parser = parser
        self._subparsers = self._parser.add_subparsers(
            title="supported commands"
        )
        self._options = collections.defaultdict(list)

    def command(self, *args, **kwargs):
        """Decorator to register a new command.

        Args:
            *args: The arguments to be forwarded to add_parser().
            **kwargs: The keyword arguments to be forwarded to add_parser().
        """
        cmd_parser = self._subparsers.add_parser(*args, **kwargs)

        def decorator(func: Callable):
            for args, kwargs in self._options[func]:
                cmd_parser.add_argument(*args, **kwargs)

            @functools.wraps(func)
            def wrapper(args):
                # Extract the function parameters from parsed arguments.
                params = inspect.signature(func).parameters
                unwrapped_args = {k: getattr(args, k) for k in params}
                func(**unwrapped_args)

            cmd_parser.set_defaults(func=wrapper)

            return func

        return decorator

    def option(self, *args, **kwargs):
        """Decorator to register an option for the current command.

        Args:
            *args: The arguments to be forwarded to add_argument().
            **kwargs: The keyword arguments to be forwarded to add_argument().
        """

        def decorator(func: Callable):
            self._options[func].append((args, kwargs))
            return func

        return decorator

    def run(self, argv: Optional[List[str]] = None) -> Optional[int]:
        """Parses the arguments and runs the target command.

        Args:
            argv: The command line arguments.

        Returns:
            An optional return code that is suitable to be used with sys.exit().
        """
        args = self._parser.parse_args(argv)
        setup_logging(args.debug)
        return args.func(args)


cli = CLIRunner()


@cli.command(
    "setup",
    help="Setup the DUT",
    description="Setup the DUT to make it ready to be controlled remotely.",
)
def cmd_setup():
    device.setup()


@cli.command(
    "open",
    help="Open CCA",
    description="Open CCA.",
)
@cli.option(
    "--facing",
    help="facing of the camera to be opened",
    action=EnumAction,
    enum_type=app.Facing,
)
@cli.option(
    "--mode",
    help="target capture mode in app",
    action=EnumAction,
    enum_type=app.Mode,
)
def cmd_open(facing: app.Facing, mode: app.Mode):
    # TODO(shik): Wake up the display if it's sleeping.
    cca = app.CameraApp()
    cca.open(facing=facing, mode=mode)


@cli.command(
    "close",
    help="Close CCA",
    description="Close CCA if it's open.",
)
def cmd_close():
    cca = app.CameraApp()
    cca.close()


@cli.command(
    "take-photo",
    help="Take a photo",
    description="Take a photo using CCA.",
)
@cli.option(
    "--facing",
    help="facing of the camera to be captured",
    action=EnumAction,
    enum_type=app.Facing,
)
@cli.option(
    "--output",
    help="output path to save the photo",
    type=pathlib.Path,
)
def cmd_take_photo(facing: app.Facing, output: pathlib.Path):
    # TODO(shik): Provide an option to reuse the existing CCA session and not to
    # close the app afterward.
    cca = app.CameraApp()
    path = cca.take_photo(facing=facing)
    if output:
        shutil.copy2(path, output)
        logging.info("Copied photo from %s to %s", path, output)
    else:
        logging.info("Saved photo at %s", path)


@cli.command(
    "record-video",
    help="Record a video",
    description="Record a video using CCA.",
)
@cli.option(
    "--facing",
    help="facing of the camera to be recorded",
    action=EnumAction,
    enum_type=app.Facing,
)
@cli.option(
    "--duration",
    help="duration in seconds to be recorded",
    type=float,
    default=3,
)
@cli.option(
    "--output",
    help="output path to save the video",
    type=pathlib.Path,
)
def cmd_record_video(facing: app.Facing, duration: float, output: pathlib.Path):
    cca = app.CameraApp()
    path = cca.record_video(facing=facing, duration=duration)
    if output:
        shutil.copy2(path, output)
        logging.info("Copied video from %s to %s", path, output)
    else:
        logging.info("Saved video at %s", path)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    return cli.run(argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
