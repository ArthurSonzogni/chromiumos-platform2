#!/usr/bin/env python3
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# pylint: disable=unused-argument

"""Unit tests for ConfigFS data file generator."""

import functools
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from cros_config_host import configfs


this_dir = os.path.dirname(__file__)


def TestConfigs(*args):
    """Wrapper function for tests which use configs from libcros_config/

    Use like so:
    @TestConfigs('test.json', [any other files you want...])
    def testFoo(self, config_filename, config, output_dir):
      # do something!
      pass
    """

    def _Decorator(method):
        @functools.wraps(method)
        def _Wrapper(self):
            for filename in args:
                with open(
                    os.path.join(this_dir, "../test_data", filename),
                    encoding="utf-8",
                ) as f:
                    config = json.load(f)

                with tempfile.TemporaryDirectory(prefix="test.") as output_dir:
                    squashfs_img = os.path.join(output_dir, "configfs.img")
                    configfs.GenerateConfigFSData(config, squashfs_img)
                    subprocess.run(
                        ["unsquashfs", squashfs_img],
                        check=True,
                        cwd=output_dir,
                        stdout=subprocess.PIPE,
                    )
                    method(self, filename, config, output_dir)

        return _Wrapper

    return _Decorator


class ConfigFSTests(unittest.TestCase):
    """Tests for ConfigFS."""

    def testSerialize(self):
        self.assertEqual(configfs.Serialize(True), b"true")
        self.assertEqual(configfs.Serialize(False), b"false")
        self.assertEqual(configfs.Serialize(10), b"10")
        self.assertEqual(configfs.Serialize("hello💩"), b"hello\xf0\x9f\x92\xa9")
        self.assertEqual(configfs.Serialize(b"\xff\xff\xff"), b"\xff\xff\xff")

    @TestConfigs("test.json", "test_arm.json")
    def testConfigV1FileStructure(self, filename, config, output_dir):
        def _CheckConfigRec(config, path):
            if isinstance(config, dict):
                iterator = config.items()
            elif isinstance(config, list):
                iterator = enumerate(config)
            else:
                self.assertTrue(os.path.isfile(path))
                self.assertEqual(
                    Path(path).read_bytes(),
                    configfs.Serialize(config),
                )
                return
            self.assertTrue(os.path.isdir(path))
            for name, entry in iterator:
                childpath = os.path.join(path, str(name))
                _CheckConfigRec(entry, childpath)

        _CheckConfigRec(config, os.path.join(output_dir, "squashfs-root/v1"))


if __name__ == "__main__":
    unittest.main(module=__name__)
