#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Pre-upload hook to check every #include has an actual path to the file.

Certain files (like `snappy.h`) and certain prefixes (like `base`) are excluded,
but all other #include directives must point to a file that exists.
"""

import argparse
from pathlib import Path
import re
import sys
from typing import Iterator, List, Optional, Tuple

# pylint: disable=wrong-import-position
import chromite_init  # pylint: disable=unused-import

from chromite.lib import git


# Sub-directories that need to be verified for #includes correctness.
# Any platform2/project that is ready can be added.
PROJECTS_TO_VERIFY = frozenset({"missive"})

# Suffixes of files that need to be verifies for #includes correctness.
SUFFIXES_TO_VERIFY = frozenset({".h", ".hpp", ".cc", ".cpp", ".cxx", ".c"})

# Header file names that do not need to be verified:
# they refer to the runtime library.
ALLOWED_HEADERS = frozenset(
    {
        "snappy.h",
        "unistd.h",
    }
)

# Prefixes of the files that do not need to be verified:
# they refer to known components rather than the current
# module.
ALLOWED_PREFIXES = frozenset(
    {
        "absl",
        "base",
        "brillo",
        "crypto",
        "chromeos",
        "dbus",
        "dbus_adaptors",
        "google",
        "gmock",
        "gtest",
        "openssl",
        "re2",
    }
)

INCLUDE_RE: re.Pattern = re.compile(r"^#include\s*[\"<](\S*)[\">]")

TOP_DIR = Path(__file__).resolve().parent.parent


def get_lines_from_commit(
    file: Path, commit: Optional[str]
) -> Iterator[Tuple[int, str]]:
    """Reads the file data for |path| either from disk or git |commit|.

    Args:
        file: Path of the file being processed.
        commit: Hash of the commit. If given, only files referred by this commit
                will be checked; otherwise check all files included in the
                command.

    Returns:
        An iterator of (#, line) pairs added to file by the given commit.
    """
    if commit:
        contents = git.GetObjectAtRev(None, f"./{file}", commit)
    else:
        contents = file.read_text(encoding="utf-8")

    return enumerate(contents.splitlines(), start=1)


def check_include_for_full_path(
    file: Path, lines: Iterator[Tuple[int, str]]
) -> Iterator[str]:
    """Checks to make sure every #include .h file has a full path.

    Args:
        file: Path of the file being processed.
        lines: An iterator of [#, line] pairs from the file.

    Yields:
        Error messages reporting incorrect #includes.
    """

    for lineno, line in lines:
        m = INCLUDE_RE.match(line)
        if not m:
            continue

        header = Path(m.group(1))
        if header.suffix != ".h":
            continue
        if header.name in ALLOWED_HEADERS:
            continue
        if header.parts[0] in ALLOWED_PREFIXES:
            continue
        if not header.is_absolute():
            header = TOP_DIR / header
        if tuple(header.suffixes) == (".pb", ".h"):
            # *.pb.h files don't exist in source code, replace the suffix with
            # *.proto
            header = header.with_suffix("").with_suffix(".proto")
            if not header.is_file():
                # Attempt to check `synced/pipeline` location instead.
                header = header.parent / "synced" / "pipeline" / header.name

        if not header.is_file():
            yield f"{file}:{str(lineno)} {m.group(1)}"


def check_files(files: List[Path], commit: Optional[str]) -> Iterator[str]:
    """Check for #include's without full path.

    Args:
        files: A list of file paths.
        commit: Hash of a commit. If given, only files referred by this commit
                will be checked; otherwise check all files included in the
                command.

    Yields:
        Error messages reporting incorrect #includes.
    """
    for file in files:
        if not file.parts[0] in PROJECTS_TO_VERIFY:
            continue
        if not file.suffix in SUFFIXES_TO_VERIFY:
            continue
        lines = get_lines_from_commit(file, commit)
        yield from check_include_for_full_path(file, lines)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--commit",
        help="Hash of commit to check. Only show errors on "
        "lines added in the commit if set.",
    )
    parser.add_argument("files", type=Path, nargs="*")
    opts = parser.parse_args(argv)

    error_count = 0
    for error in check_files(opts.files, opts.commit):
        if error_count == 0:
            print(
                "Some #include(s) in source errors do not have a full path\n",
                file=sys.stderr,
            )
        error_count += 1
        print(
            error,
            file=sys.stderr,
        )

    return error_count


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
