# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Find chromite and add it to sys.path."""

from pathlib import Path
import sys
from typing import NoReturn, Optional


THIS_FILE = Path(__file__).resolve()

TOP_DIR = THIS_FILE.parent.parent

# Find chromite!  Assume the layout:
# ~/chromiumos/ -- can be anywhere
#  chromite/
#  src/platform2/
SOURCE_ROOT = TOP_DIR.parent.parent
if not (SOURCE_ROOT / "chromite").is_dir():
    # When running in an ebuild, the platform2/ tree is created in a tempdir.
    SOURCE_ROOT = Path("/mnt/host/source")
sys.path.insert(0, str(SOURCE_ROOT))


def test_main(file: str, jobs: Optional[int] = None) -> NoReturn:
    """Run local unittests using chromite's pytest runner."""
    # These are only used by the unittests, so don't import normally.
    from chromite.lib import constants
    from chromite.lib import cros_build_lib
    from chromite.utils import shell_util

    file = Path(file).resolve()
    # The tests don't need the SDK or namespaces, so disable them to speed up.
    cmd = [constants.CHROMITE_DIR / "run_tests", "--no-chroot", "--quickstart"]
    if jobs is not None:
        cmd += [f"-j{jobs}"]
    cmd += [file.name]
    print("Running", shell_util.cmd_to_str(cmd))
    result = cros_build_lib.run(cmd, cwd=file.parent, check=False)
    sys.exit(result.returncode)
