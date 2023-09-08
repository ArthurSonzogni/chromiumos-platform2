# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Find chromite and add it to sys.path."""

from pathlib import Path
import sys


TOP_DIR = Path(__file__).resolve().parent.parent

# Find chromite!  Assume the layout:
# ~/chromiumos/ -- can be anywhere
#  chromite/
#  src/platform2/
SOURCE_ROOT = TOP_DIR.parent.parent
if not (SOURCE_ROOT / "chromite").is_dir():
    # When running in an ebuild, the platform2/ tree is created in a tempdir.
    SOURCE_ROOT = Path("/mnt/host/source")
sys.path.insert(0, str(SOURCE_ROOT))
