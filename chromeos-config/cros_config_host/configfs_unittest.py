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
        self.assertEqual(
            configfs.Serialize("hello💩"), b"hello\xf0\x9f\x92\xa9"
        )
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

    def testTarStreamingSquashFSValidity(self):
        """Test SquashFS image properties and permissions with tar streaming."""
        test_config = {
            "chromeos": {
                "configs": [
                    {
                        "name": "test_device",
                        "identity": {
                            "sku-id": 1,
                            "platform-name": "TestPlatform",
                        },
                        "hardware-properties": {"is-lid-convertible": True},
                    }
                ]
            }
        }
        with tempfile.TemporaryDirectory(prefix="test_squashfs.") as temp_dir:
            img_path = os.path.join(temp_dir, "configfs.img")
            configfs.GenerateConfigFSData(test_config, img_path)

            # Test unsquashfs -l (listing)
            list_res = subprocess.run(
                ["unsquashfs", "-l", img_path],
                check=True,
                stdout=subprocess.PIPE,
                text=True,
            )
            self.assertIn(
                "squashfs-root/v1/chromeos/configs/0/name",
                list_res.stdout,
            )

            # Test extraction and permissions
            extract_dir = os.path.join(temp_dir, "extracted")
            subprocess.run(
                ["unsquashfs", "-d", extract_dir, img_path],
                check=True,
                stdout=subprocess.PIPE,
            )
            v1_dir = os.path.join(extract_dir, "v1")
            self.assertTrue(os.path.isdir(v1_dir))
            # Check directory permissions (0o755)
            self.assertEqual(oct(os.stat(v1_dir).st_mode & 0o777), "0o755")
            # Check file content and permissions (0o644)
            name_file = os.path.join(v1_dir, "chromeos/configs/0/name")
            self.assertTrue(os.path.isfile(name_file))
            self.assertEqual(Path(name_file).read_bytes(), b"test_device")
            self.assertEqual(oct(os.stat(name_file).st_mode & 0o777), "0o644")

    @TestConfigs(
        "test.json",
        "test_arm.json",
        "test_build.json",
        "test_import.json",
        "test_merge.json",
    )
    def testConfigFsEquivalence(self, filename, config, output_dir):
        """Verify extracted SquashFS tree against expected configuration."""

        def _VerifyNode(node, path):
            if isinstance(node, dict):
                self.assertTrue(os.path.isdir(path))
                self.assertEqual(oct(os.stat(path).st_mode & 0o777), "0o755")
                for k, v in node.items():
                    _VerifyNode(v, os.path.join(path, str(k)))
            elif isinstance(node, list):
                self.assertTrue(os.path.isdir(path))
                self.assertEqual(oct(os.stat(path).st_mode & 0o777), "0o755")
                for idx, v in enumerate(node):
                    _VerifyNode(v, os.path.join(path, str(idx)))
            else:
                self.assertTrue(os.path.isfile(path))
                self.assertEqual(oct(os.stat(path).st_mode & 0o777), "0o644")
                self.assertEqual(
                    Path(path).read_bytes(), configfs.Serialize(node)
                )

        _VerifyNode(config, os.path.join(output_dir, "squashfs-root/v1"))

    def testHardlinkDeduplication(self):
        """Verify repeated leaf values across SKUs share hardlinked inodes."""
        test_config = {
            "chromeos": {
                "configs": [
                    {"name": "shared_model", "identity": {"sku-id": 1}},
                    {"name": "shared_model", "identity": {"sku-id": 2}},
                    {"name": "shared_model", "identity": {"sku-id": 3}},
                ]
            }
        }
        with tempfile.TemporaryDirectory(prefix="test_dedup.") as temp_dir:
            img_path = os.path.join(temp_dir, "configfs.img")
            configfs.GenerateConfigFSData(test_config, img_path)

            stat_res = subprocess.run(
                ["unsquashfs", "-s", img_path],
                check=True,
                stdout=subprocess.PIPE,
                text=True,
            )
            self.assertIn("Compression zstd", stat_res.stdout)

            extract_dir = os.path.join(temp_dir, "extracted")
            subprocess.run(
                ["unsquashfs", "-d", extract_dir, img_path],
                check=True,
                stdout=subprocess.PIPE,
            )
            name_paths = [
                Path(extract_dir, f"v1/chromeos/configs/{idx}/name")
                for idx in range(3)
            ]
            first_stat = os.lstat(name_paths[0])
            for path in name_paths:
                st = os.lstat(path)
                self.assertFalse(os.path.islink(path))
                self.assertEqual(st.st_mode & 0o777, 0o644)
                self.assertEqual(path.read_bytes(), b"shared_model")
                self.assertEqual(st.st_ino, first_stat.st_ino)
            self.assertEqual(first_stat.st_nlink, 3)

            sku0 = Path(extract_dir, "v1/chromeos/configs/0/identity/sku-id")
            sku1 = Path(extract_dir, "v1/chromeos/configs/1/identity/sku-id")
            self.assertEqual(sku0.read_bytes(), b"1")
            self.assertEqual(sku1.read_bytes(), b"2")
            self.assertNotEqual(os.lstat(sku0).st_ino, os.lstat(sku1).st_ino)


if __name__ == "__main__":
    unittest.main(module=__name__)
