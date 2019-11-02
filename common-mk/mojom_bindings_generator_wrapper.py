#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Thin wrapper of Mojo's mojom_bindings_generator.py.

To generate C++ files from mojom, it is necessary to run
mojom_bindings_generator.py twice (with and without
--generate_non_variant_code) for each mojom file,
or three times ("--generate_message_ids" in addition) if libchrome is
newer than 576279.

However, gni's "rule" does not support multiple "action"s. So, instead,
use this simple python wrapper.

Usage:
  python mojom_bindings_generator_wrapper.py ${libbase_ver} \
    ${MOJOM_BINDINGS_GENERATOR} \
    [... and more args/flags to be passed to the mojom_bindings_generator.py]

TODO(crbug.com/909719): Clean up libbase_ver handling after uprev.
"""

from __future__ import print_function

import subprocess
import sys


def main(argv):
  subprocess.check_call(argv[2:])
  subprocess.check_call(argv[2:] + ['--generate_non_variant_code'])
  if argv[1] == '576279':
    subprocess.check_call(argv[2:] + ['--generate_non_variant_code',
                                      '--generate_message_ids'])


if __name__ == '__main__':
  main(sys.argv)
