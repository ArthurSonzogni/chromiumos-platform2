#!/usr/bin/env python3
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Bundle the given ELF executable binary and its dependencies together.

This script provides a simple implementation to build a self-extractable
binary that can install the executable binary and its dependent dynamic-linked
libraries to the specific path.
"""

import argparse
import subprocess
import sys
import tempfile
from typing import List

# Hard-coded the absolute path to `lddtree` because the dirname of it might
# not be listed in the environment variable `PATH` when this script is invoked.
_LDDTREE_BIN_PATH = '/mnt/host/source/chromite/bin/lddtree'


def main(argv: List[str]) -> int:
  ap = argparse.ArgumentParser(description=__doc__)
  ap.add_argument(
      '--root-dir',
      required=True,
      help='Path to the root directory containing build dependencies.')
  ap.add_argument('--target-path',
                  required=True,
                  help='Path to the ELF executable binary to bundle.')
  ap.add_argument('--bundle-description',
                  help='The description for the bundled file.')
  ap.add_argument('--output-path',
                  required=True,
                  help='Path to the output bundled file.')
  opts = ap.parse_args(argv)

  with tempfile.TemporaryDirectory() as archive_dir:
    subprocess.check_call([
        _LDDTREE_BIN_PATH, '--copy-to-tree', archive_dir, '--root',
        opts.root_dir, '--no-auto-root', '--generate-wrappers', '--bindir', '/',
        '--verbose', opts.target_path
    ])

    # Build the bundled file.
    subprocess.check_call([
        'makeself', '--license', '/dev/null', '--nox11', '--current', '--bzip2',
        archive_dir, opts.output_path, opts.bundle_description
    ])

  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
