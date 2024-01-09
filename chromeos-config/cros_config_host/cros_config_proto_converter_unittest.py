#!/usr/bin/env python3
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test cros_config_proto_converter module."""

# pylint: disable=missing-docstring,protected-access

import pathlib
import unittest

# pylint: disable=import-error
from chromiumos.config.test import fake_config as fake_config_mod
import cros_config_proto_converter


# pylint: enable=import-error


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
    """Test CLI argument parsing."""

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


class TransformBuildConfigsTest(unittest.TestCase):
    """Test _transform_build_configs function."""

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
