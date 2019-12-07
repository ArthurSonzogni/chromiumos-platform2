#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

from __future__ import print_function

import os
import sys
import tempfile
import unittest

# pylint: disable=wrong-import-position
this_dir = os.path.dirname(__file__)
sys.path.insert(0, this_dir)
import generate_schema_doc
sys.path.pop(0)


class SchemaTests(unittest.TestCase):

  def testActualSchemaAgainstReadme(self):
    output = tempfile.mktemp()
    generate_schema_doc.Main(
        os.path.join(this_dir, 'cros_config_schema.yaml'),
        output)
    with open(output, 'rb') as output_stream:
      output_lines = output_stream.read().decode('utf-8').splitlines()
      with open(os.path.join(this_dir, '../README.md'), 'rb') as readme_stream:
        readme_lines = readme_stream.read().decode('utf-8').splitlines()
        readme_schema_lines = []
        in_section = False
        for line in readme_lines:
          if 'begin_definitions' in line:
            in_section = True

          if in_section:
            readme_schema_lines.append(line)

          if 'end_definitions' in line:
            break

        self.assertEqual(
            output_lines,
            readme_schema_lines,
            'Schema does not match README.\n'
            'Please run: python -m cros_config_host.generate_schema_doc '
            '-o README.md')

    os.remove(output)


if __name__ == '__main__':
  unittest.main()
