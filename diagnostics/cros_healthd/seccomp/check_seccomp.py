#!/usr/bin/env python3
# # Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Verify syscall in seccomp policy files."""

from collections import namedtuple
from pathlib import Path
import sys

from chromite.lib import commandline


# Lists of syscall which should be in all the policy files.
REQUIRED_SYSCALL = {
    "amd64": [],
    "arm": [],
    "arm64": [],
}

# Lists of syscall which should not be in all the policy files.
DENIED_SYSCALL = {
    "amd64": [],
    "arm": [],
    "arm64": [],
}

SeccompLine = namedtuple("SeccompLine", ["line_number", "line", "value"])


def ParseSeccompAndReturnParseError(arch, filename):
    seccomp = {}
    parse_error = ""
    with open(filename, "r") as f:
        line_buffer = ""
        for idx, current_line in enumerate(f.readlines()):
            line_buffer += current_line
            if line_buffer.endswith("\\\n"):
                line_buffer = line_buffer[:-2]
                continue
            line = line_buffer
            line_buffer = ""

            if line.isspace() or line.startswith("#"):
                continue
            tokens = line.split(":", 1)
            if len(tokens) != 2:
                parse_error += (
                    f'{filename}:{idx + 1}: error: cannot split by ":"\n{line}'
                )
                continue
            if tokens[0] in seccomp:
                parse_error += (
                    f'{filename}:{idx + 1}: error: duplicated syscall"\n{line}'
                )
                parse_error += (
                    f"{filename}:{seccomp[tokens[0]].line_number}: "
                    f'first defined here"\n{seccomp[tokens[0]].line}'
                )
                continue
            seccomp[tokens[0]] = SeccompLine(idx + 1, line, tokens[1])
        if line_buffer:
            parse_error += f'{filename}: unexpected termination "\\"'

    for syscall in REQUIRED_SYSCALL[arch]:
        if syscall not in seccomp:
            parse_error += f"{filename}:missing required seccomp: {syscall}"
            continue
        if seccomp[syscall].value != "1":
            parse_error += (
                f"{filename}:{seccomp[syscall].line_number}: "
                f'the value of required syscall should "1"\n'
                f"{seccomp[syscall].line}"
            )
            continue

    for syscall in DENIED_SYSCALL[arch]:
        if syscall in seccomp:
            parse_error += (
                f"{filename}:{seccomp[syscall].line_number}: "
                f"denied syscall\n"
                f"{seccomp[syscall].line}"
            )
            continue

    return parse_error


def GetParser():
    """Returns an argument parser."""
    parser = commandline.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--arch",
        required=True,
        help="The architecture of the seccomp files.",
    )
    parser.add_argument("--seccomp", required=True, help="Seccomp filename.")
    parser.add_argument(
        "--output",
        required=True,
        help="Output filename for the check result (for gn to check timestamp).",
    )
    return parser


def main(argv):
    parser = GetParser()
    opts = parser.parse_args(argv)
    opts.Freeze()

    parse_error = ParseSeccompAndReturnParseError(opts.arch, opts.seccomp)
    if parse_error:
        sys.stderr.write(parse_error)
        return 1
    Path(opts.output).touch()
    return 0


if __name__ == "__main__":
    commandline.ScriptWrapperMain(lambda _: main)
