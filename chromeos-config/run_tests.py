#!/usr/bin/env vpython3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper to call pytest.  All arguments are passed directly to pytest."""

import os
from pathlib import Path
import site
import subprocess
import sys
from typing import List, Optional

import pytest


HERE = Path(__file__).resolve().parent
BOXSTER_PYTHON_PATH = HERE.parent.parent / "config" / "python"
site.addsitedir(HERE)
site.addsitedir(BOXSTER_PYTHON_PATH)


def main(argv: Optional[List[str]]) -> int:
    """The main function."""

    # Ensure the cipd dependencies and put it in PATH.
    cache_dir = Path(
        os.environ.get("XDG_CACHE_HOME") or (Path.home() / ".cache")
    )
    cipd_root = cache_dir / "chromeos-config" / "cipd-root"
    subprocess.run(
        [
            "cipd",
            "ensure",
            "-root",
            cipd_root,
            "-ensure-file",
            HERE / "cipd_manifest.txt",
        ],
        check=True,
    )

    # Note: we strip PATH except for the cipd_root, that way we ensure we don't
    # start unexpectedly depending on any host tools.
    os.environ["PATH"] = f"{cipd_root}/bin"

    # We do no argument parsing.  All arguments are passed directly to pytest.
    return pytest.main(argv)


if __name__ == "__main__":
    main(sys.argv[1:])
