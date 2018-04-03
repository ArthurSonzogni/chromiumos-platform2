#!/usr/bin/env python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

from __future__ import print_function

import os
import unittest

import libcros_config_host_json

this_dir = os.path.dirname(__file__)

class CrosConfigHostJsonTests(unittest.TestCase):

  def testWallpaper(self):
    source = os.path.join(this_dir, '../libcros_config/test.yaml')
    with open(source, 'r') as source_stream:
      libcros_config_host_json.CrosConfigJson(source_stream)


if __name__ == '__main__':
  unittest.main()
