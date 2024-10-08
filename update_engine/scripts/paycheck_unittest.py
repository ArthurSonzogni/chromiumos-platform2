#!/usr/bin/env python
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit testing paycheck.py."""

# This test requires new (Y) and old (X) images, as well as a full payload
# from image Y and a delta payload from Y to X for each partition.
# Payloads are from sample_images/generate_payloads.
#
# The test performs the following:
#
# - It statically applies the full and delta payloads.
#
# - It applies full_payload to yield a new kernel (kern.part) and rootfs
#   (root.part) and compares them to the new image partitions.
#
# - It applies delta_payload to the old image to yield a new kernel and rootfs
#   and compares them to the new image partitions.
#
# Previously test_paycheck.sh. Run with update_payload ebuild.

# Disable check for function names to avoid errors based on old code
# pylint: disable-msg=invalid-name

import filecmp
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class PaycheckTest(unittest.TestCase):
    """Test paycheck functions."""

    @classmethod
    def setUpClass(cls):
        cls._datadir = tempfile.TemporaryDirectory(prefix="update_engine-")
        cls.datadir = Path(cls._datadir.name)

        sample_dir = Path(__file__).resolve().parent.parent / "sample_images"
        for f in ("sample_images.tar.bz2", "sample_payloads.tar.xz"):
            subprocess.run(
                ["tar", "xf", sample_dir / f], cwd=cls.datadir, check=True
            )

        cls._full_payload = os.path.join(cls.datadir, "full_payload.bin")
        cls._delta_payload = os.path.join(cls.datadir, "delta_payload.bin")

        cls._new_kernel = os.path.join(cls.datadir, "disk_ext2_4k.img")
        cls._new_root = os.path.join(cls.datadir, "disk_sqfs_default.img")
        cls._old_kernel = os.path.join(cls.datadir, "disk_ext2_4k_empty.img")
        cls._old_root = os.path.join(cls.datadir, "disk_sqfs_empty.img")

    @classmethod
    def tearDownClass(cls):
        cls._datadir.cleanup()

    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory(prefix="update_engine-")
        self.tmpdir = self._tmpdir.name

        # Temp output files.
        self._kernel_part = os.path.join(self.tmpdir, "kern.part")
        self._root_part = os.path.join(self.tmpdir, "root.part")

    def tearDown(self):
        self._tmpdir.cleanup()

    def checkPayload(self, type_arg, payload):
        """Checks Payload."""
        self.assertEqual(
            0, subprocess.check_call(["./paycheck.py", "-t", type_arg, payload])
        )

    def testFullPayload(self):
        """Checks the full payload statically."""
        self.checkPayload("full", self._full_payload)

    def testDeltaPayload(self):
        """Checks the delta payload statically."""
        self.checkPayload("delta", self._delta_payload)

    def testApplyFullPayload(self):
        """Applies full payloads and compares results to new sample images."""
        self.assertEqual(
            0,
            subprocess.check_call(
                [
                    "./paycheck.py",
                    self._full_payload,
                    "--part_names",
                    "kernel",
                    "root",
                    "--out_dst_part_paths",
                    self._kernel_part,
                    self._root_part,
                ]
            ),
        )

        # Check if generated full image is equal to sample image.
        self.assertTrue(filecmp.cmp(self._kernel_part, self._new_kernel))
        self.assertTrue(filecmp.cmp(self._root_part, self._new_root))

    def testApplyDeltaPayload(self):
        """Applies delta to old image and checks against new sample images."""
        self.assertEqual(
            0,
            subprocess.check_call(
                [
                    "./paycheck.py",
                    self._delta_payload,
                    "--part_names",
                    "kernel",
                    "root",
                    "--src_part_paths",
                    self._old_kernel,
                    self._old_root,
                    "--out_dst_part_paths",
                    self._kernel_part,
                    self._root_part,
                ]
            ),
        )

        self.assertTrue(filecmp.cmp(self._kernel_part, self._new_kernel))
        self.assertTrue(filecmp.cmp(self._root_part, self._new_root))


if __name__ == "__main__":
    unittest.main()
