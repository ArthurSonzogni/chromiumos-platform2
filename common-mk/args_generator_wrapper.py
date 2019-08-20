#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Python wrapper for programs to generate args.

GN exec_script runs the script with python. For running a program other than
python, use this wrapper.

path/to/args_generator_wrapper.py program arg1 ...
will run the program with the args.

Since the intention of this wrapper is to generate arguments passed to the
shell, the output of the program is unescaped by the shell and spaces in it
are replaced with newlines.
"""

from __future__ import print_function

import subprocess
import sys

import common_utils

# TODO(vapier): Move decode('utf-8') to encoding='utf-8' in check_output once
# .gn uses shebangs.
output = subprocess.check_output(sys.argv[1:]).decode('utf-8').strip()
unescaped = common_utils.parse_shell_args(output)

print('\n'.join(unescaped))
