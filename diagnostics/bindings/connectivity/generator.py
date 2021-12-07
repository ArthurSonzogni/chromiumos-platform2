#!/usr/bin/env python3
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generate mojo connectivity test code."""

import json
import logging
import os
import sys

from chromite.lib import commandline


def GetParser():
    """Returns an argument parser."""
    parser = commandline.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--mojo-root', required=True, help='Root of the mojo files.')
    parser.add_argument(
        '--output-dir',
        required=True,
        help='Path for mojo generated code. '
        'Must be the same as the mojo bindings output dir.')
    parser.add_argument(
        '--mojom-file-list', help='Mojom filenames passed as a file.')
    parser.add_argument(
        '--mojoms', default=[], nargs='+', help='Mojom filenames.')
    parser.add_argument(
        '--generator-overrides',
        default=[],
        nargs='+',
        help='The json config of the generator overrides.')
    return parser


def main(argv):
    parser = GetParser()
    opts = parser.parse_args(argv)
    if opts.mojom_file_list:
        with open(opts.mojom_file_list, encoding='utf-8') as f:
            opts.mojoms.extend(f.read().split())

    opts.Freeze()

    if not opts.mojoms:
        raise parser.error('Must list at least one mojom file via --mojoms or '
                           '--mojom-file-list')

    # TODO(chungsheg): Add implementation.
    raise NotImplementedError


if __name__ == '__main__':
    commandline.ScriptWrapperMain(lambda _: main)
