#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=missing-docstring,protected-access

import filecmp
import pathlib
import subprocess
import unittest

from chromiumos.config.test import fake_config as fake_config_mod
import cros_config_proto_converter

from chromite.lib import cros_test_lib


THIS_DIR = pathlib.Path(__file__).parent
TEST_DATA_DIR = pathlib.Path("test_data/proto_converter")

PROGRAM_CONFIG_FILE = fake_config_mod.FAKE_PROGRAM_CONFIG
PROJECT_CONFIG_FILE = fake_config_mod.FAKE_PROJECT_CONFIG


def fake_config():
    return cros_config_proto_converter._merge_configs(
        [
            cros_config_proto_converter._read_config(PROGRAM_CONFIG_FILE),
            cros_config_proto_converter._read_config(PROJECT_CONFIG_FILE),
        ]
    )


class ParseArgsTests(unittest.TestCase):
    def test_parse_args(self):
        argv = [
            "-c",
            "config1",
            "config2",
            "-p",
            "program_config",
            "-o",
            "output",
        ]
        args = cros_config_proto_converter.parse_args(argv)
        self.assertEqual(
            args.project_configs,
            [
                "config1",
                "config2",
            ],
        )
        self.assertEqual(args.program_config, "program_config")
        self.assertEqual(args.output, "output")


class MainTest(cros_test_lib.TempDirTestCase):
    def test_full_transform(self):
        output_dir = self.tempdir / "proto_converter"
        output_file = output_dir / "sw_build_config/fake_project.json"
        dtd_path = THIS_DIR / "media_profiles.dtd"
        cros_config_proto_converter.Main(
            project_configs=[PROJECT_CONFIG_FILE],
            program_config=PROGRAM_CONFIG_FILE,
            output=output_file,
            dtd_path=dtd_path,
        )

        # Replace paths which reference the tempdir with the source directory.
        contents = output_file.read_text()
        contents = contents.replace(str(self.tempdir), str(TEST_DATA_DIR))
        output_file.write_text(contents)

        dircmp = filecmp.dircmp(output_dir, TEST_DATA_DIR)
        if (
            dircmp.diff_files
            or dircmp.left_only
            or dircmp.right_only
            or dircmp.funny_files
        ):
            dircmp.report_full_closure()
            msg = ""

            # Add a diff output for convenience / debugging.
            for path in dircmp.diff_files:
                # pylint: disable=subprocess-run-check
                result = subprocess.run(
                    ["diff", "-u", TEST_DATA_DIR / path, self.tempdir / path],
                    stdout=subprocess.PIPE,
                    encoding="utf-8",
                )
                # pylint: enable=subprocess-run-check
                msg += result.stdout

            msg += (
                "\nFake project transform does not match.\n"
                "Please run ./regen.sh and amend your changes if necessary.\n"
            )

            self.fail(msg)


class TransformBuildConfigsTest(unittest.TestCase):
    def test_missing_lookups(self):
        config = fake_config()
        config.ClearField("program_list")

        with self.assertRaisesRegex(Exception, "Failed to lookup Program"):
            cros_config_proto_converter._transform_build_configs(config)

    def test_empty_device_brand(self):
        config = fake_config()
        config.ClearField("device_brand_list")
        # Signer configs tied to device brands, so need to clear that also
        config.program_list[0].ClearField("device_signer_configs")

        self.assertIsNotNone(
            cros_config_proto_converter._transform_build_configs(config)
        )

    def test_missing_sw_config(self):
        config = fake_config()
        config.ClearField("software_configs")

        with self.assertRaisesRegex(Exception, "Software config is required"):
            cros_config_proto_converter._transform_build_configs(config)

    def test_unique_configs_only(self):
        config = fake_config()
        duplicate_config = cros_config_proto_converter._merge_configs(
            [config, fake_config()]
        )

        with self.assertRaisesRegex(Exception, "Multiple software configs"):
            cros_config_proto_converter._transform_build_configs(
                duplicate_config
            )


if __name__ == "__main__":
    unittest.main(module=__name__)
