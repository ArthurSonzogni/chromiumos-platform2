#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Ensure files in missive/proto/synced/ are not modified."""

import argparse
from pathlib import Path
import sys
from typing import List, Optional


TOP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOP_DIR))


def CheckNoSyncedFilesManuallyModified(file_paths: List[str]) -> bool:
    """Check that synced files aren't modified.

    Files in missive/proto/synced are synced from
    Chromium via Copybara and should not be modified manually.

    Args:
        file_paths: Files modified in this commit.

    Returns:
        True if synced files were not modified, False otherwise
    """

    synced_protos_path = "missive/proto/synced"

    for path in file_paths:
        if synced_protos_path in path:
            print(
                f"Cannot upload changes to protos in {synced_protos_path}.\n"
                "Protos must be synced from the Chromium repo.\n"
                "See chromium/src/components/reporting/proto/synced/README in "
                "the Chromium repo for instructions.",
                file=sys.stderr,
            )
            return False
    return True


def get_parser():
    """Return an argument parser."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", help="Files to check.")
    return parser


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    parser = get_parser()
    opts = parser.parse_args(argv)
    file_paths = opts.files
    return 0 if CheckNoSyncedFilesManuallyModified(file_paths) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
