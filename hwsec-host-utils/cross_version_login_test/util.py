# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The utilities for preparation of cross version data"""

import logging
from pathlib import Path
import subprocess


def init_logging(debug: bool):
    log_level = logging.DEBUG if debug else logging.INFO
    logging.basicConfig(
        level=log_level, format="%(asctime)s %(levelname)s: %(message)s"
    )


def chromiumos_src() -> Path:
    # This assume that __file__ resides in
    # src/platform2/hwsec-host-utils/cross_version_loging_test
    src_path = Path(__file__).resolve().parents[3]
    if src_path.name != "src":
        raise RuntimeError("Failed to find ChromiumOS src directory.")
    return src_path


def check_run(*args: str, **kwargs) -> str:
    """Runs the given command and returns the stdout; throws on failure."""
    try:
        logging.debug("Running command: %s", args)
        result = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
            **kwargs,
        )
        return result.stdout
    except subprocess.CalledProcessError as exc:
        # Print the output to aid debugging (the exception message doesn't
        # include the output).
        output = exc.output.decode("utf-8")
        logging.error("Command %s printed:\n%s", args, output)
        raise
