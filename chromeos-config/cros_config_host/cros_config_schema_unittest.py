#!/usr/bin/env python3
# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=missing-module-docstring,missing-class-docstring

import contextlib
import io
import os
import pathlib
import re
import tempfile
import unittest

from cros_config_host import cros_config_schema
import jsonschema  # pylint: disable=import-error


this_dir = os.path.dirname(__file__)

BASIC_CONFIG = """
reef-9042-fw: &reef-9042-fw
  bcs-overlay: 'overlay-reef-private'
  ec-ro-image: 'Reef_EC.9042.87.1.tbz2'
  main-ro-image: 'Reef.9042.87.1.tbz2'
  main-rw-image: 'Reef.9042.110.0.tbz2'
  build-targets:
    coreboot: 'reef'

chromeos:
  devices:
    - $name: 'basking'
      products:
        - $key-id: 'OEM2'
          $brand-code: 'ASUN'
      skus:
        - $sku-id: 0
          config:
            audio:
              main:
                $card: 'bxtda7219max'
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
                files:
                  - source: "{{$dsp-ini}}"
                    destination: "/etc/cras/{{$dsp-ini}}"
                    $dsp-ini: "{{cras-config-dir}}/dsp.ini"
            brand-code: '{{$brand-code}}'
            identity:
              platform-name: "Reef"
              frid: "Google_Reef"
              sku-id: "{{$sku-id}}"
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
            test-label: 'reef'
"""


class MergeDictionaries(unittest.TestCase):
    def testBaseKeyMerge(self):
        primary = {"a": {"b": 1, "c": 2}}
        overlay = {"a": {"c": 3}, "b": 4}
        cros_config_schema.MergeDictionaries(primary, overlay)
        self.assertEqual({"a": {"b": 1, "c": 3}, "b": 4}, primary)

    def testBaseListAppend(self):
        primary = {"a": {"b": 1, "c": [1, 2]}}
        overlay = {"a": {"c": [3, 4]}}
        cros_config_schema.MergeDictionaries(primary, overlay)
        self.assertEqual({"a": {"b": 1, "c": [1, 2, 3, 4]}}, primary)


class MergeConfigsTests(unittest.TestCase):
    def testMergeConfigs_noMergeWithinFile(self):
        base_yaml_content = """
chromeos:
  devices:
    - $name: 'base_device'
      skus:
        - $sku-id: 0
          config:
            name: 'base_model'
            identity:
              sku-id: "{{$sku-id}}"
              platform-name: "PlatformBase"
"""

        overlay_yaml_content = """
chromeos:
  devices:
    - $name: 'overlay_device_for_A_B'
      skus:
        - $sku-id: 10
          config:
            name: 'overlay_model_A'
            identity:
              sku-id: "{{$sku-id}}"
              custom-label-tag: "TAG_A"
              platform-name: "PlatformOverlay"
        - $sku-id: 10
          config:
            name: 'overlay_model_B'
            identity:
              sku-id: "{{$sku-id}}" # Evaluates to 10
              platform-name: "PlatformOverlay"
"""
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_dir_path = pathlib.Path(temp_dir)
            base_file_path = temp_dir_path / "base.yaml"
            overlay_file_path = temp_dir_path / "overlay.yaml"

            base_file_path.write_text(base_yaml_content, encoding="utf-8")
            overlay_file_path.write_text(overlay_yaml_content, encoding="utf-8")

            merged_result = cros_config_schema.MergeConfigs(
                [str(base_file_path), str(overlay_file_path)]
            )
            final_configs_list = merged_result["chromeos"]["configs"]

            self.assertListEqual(
                final_configs_list,
                [
                    {
                        "identity": {
                            "platform-name": "PlatformBase",
                            "sku-id": 0,
                        },
                        "name": "base_model",
                    },
                    {
                        "identity": {
                            "custom-label-tag": "TAG_A",
                            "platform-name": "PlatformOverlay",
                            "sku-id": 10,
                        },
                        "name": "overlay_model_A",
                    },
                    {
                        "identity": {
                            "platform-name": "PlatformOverlay",
                            "sku-id": 10,
                        },
                        "name": "overlay_model_B",
                    },
                ],
            )


class ParseArgsTests(unittest.TestCase):
    def testParseArgs(self):
        argv = ["-s", "schema", "-c", "config", "-o", "output", "-f", "True"]
        args = cros_config_schema.ParseArgs(argv)
        self.assertEqual(args.schema, "schema")
        self.assertEqual(args.config, "config")
        self.assertEqual(args.output, "output")
        self.assertTrue(args.filter)

    def testParseArgsForConfigs(self):
        argv = ["-o", "output", "-m", "m1", "m2", "m3"]
        args = cros_config_schema.ParseArgs(argv)
        self.assertEqual(args.output, "output")
        self.assertEqual(args.configs, ["m1", "m2", "m3"])


class TransformConfigTests(unittest.TestCase):
    def testBasicTransform(self):
        json_dict = cros_config_schema.TransformConfig(BASIC_CONFIG)
        self.assertEqual(len(json_dict), 1)
        configs = json_dict["chromeos"]["configs"]
        self.assertEqual(1, len(configs))
        model = configs[0]
        self.assertEqual("basking", model["name"])
        self.assertEqual("basking", model["audio"]["main"]["cras-config-dir"])
        # Check multi-level template variable evaluation
        self.assertEqual(
            "/etc/cras/basking/dsp.ini",
            model["audio"]["main"]["files"][0]["destination"],
        )

    def testTransformConfig_NoMatch(self):
        json_dict = cros_config_schema.TransformConfig(
            BASIC_CONFIG, model_filter_regex="abc123"
        )
        self.assertEqual(0, len(json_dict["chromeos"]["configs"]))

    def testTransformConfig_FilterMatch(self):
        scoped_config = """
reef-9042-fw: &reef-9042-fw
  bcs-overlay: 'overlay-reef-private'
  ec-ro-image: 'Reef_EC.9042.87.1.tbz2'
  main-ro-image: 'Reef.9042.87.1.tbz2'
  main-rw-image: 'Reef.9042.110.0.tbz2'
  build-targets:
    coreboot: 'reef'
chromeos:
  devices:
    - $name: 'foo'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
    - $name: 'bar'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
"""

        json_dict = cros_config_schema.TransformConfig(
            scoped_config, model_filter_regex="bar"
        )
        configs = json_dict["chromeos"]["configs"]
        self.assertEqual(1, len(configs))
        model = configs[0]
        self.assertEqual("bar", model["name"])

    def testTemplateVariableScope(self):
        scoped_config = """
audio_common: &audio_common
  main:
    $ucm: "default"
    $cras: "default"
    ucm-suffix: "{{$ucm}}"
    cras-config-dir: "{{$cras}}"
chromeos:
  devices:
    - $name: "some"
      $ucm: "overridden-by-device-scope"
      products:
        - $key-id: 'SOME-KEY'
          $brand-code: 'SOME-BRAND'
          $cras: "overridden-by-product-scope"
      skus:
        - $sku-id: 0
          config:
            audio: *audio_common
            brand-code: '{{$brand-code}}'
            identity:
              platform-name: "Some"
              frid: "Google_Some"
            name: '{{$name}}'
            firmware:
              no-firmware: True
"""
        json_dict = cros_config_schema.TransformConfig(scoped_config)
        config = json_dict["chromeos"]["configs"][0]
        audio_main = config["audio"]["main"]
        self.assertEqual(
            "overridden-by-product-scope", audio_main["cras-config-dir"]
        )
        self.assertEqual("overridden-by-device-scope", audio_main["ucm-suffix"])


class ValidateConfigSchemaTests(unittest.TestCase):
    def setUp(self):
        self._schema = cros_config_schema.ReadSchema()

    def testBasicSchemaValidation(self):
        jsonschema.validate(
            cros_config_schema.TransformConfig(BASIC_CONFIG), self._schema
        )

    def testMissingRequiredElement(self):
        config = re.sub(r" *cras-config-dir: .*", "", BASIC_CONFIG)
        config = re.sub(r" *volume: .*", "", BASIC_CONFIG)
        try:
            jsonschema.validate(
                cros_config_schema.TransformConfig(config), self._schema
            )
        except jsonschema.ValidationError as err:
            self.assertIn("required", err.__str__())
            self.assertIn("cras-config-dir", err.__str__())

    def testReferencedNonExistentTemplateVariable(self):
        config = re.sub(r" *$card: .*", "", BASIC_CONFIG)
        try:
            jsonschema.validate(
                cros_config_schema.TransformConfig(config), self._schema
            )
        except cros_config_schema.ValidationError as err:
            self.assertIn("Referenced template variable", err.__str__())
            self.assertIn("cras-config-dir", err.__str__())

    def testSkuIdOutOfBound(self):
        config = BASIC_CONFIG.replace("$sku-id: 0", "$sku-id: 0x80000000")
        with self.assertRaises(jsonschema.ValidationError) as ctx:
            jsonschema.validate(
                cros_config_schema.TransformConfig(config), self._schema
            )
        self.assertIn(
            "%i is greater than the maximum" % 0x80000000,
            str(ctx.exception),
        )
        self.assertIn("sku-id", str(ctx.exception))


class ValidateFingerprintSchema(unittest.TestCase):
    def setUp(self):
        self._schema = cros_config_schema.ReadSchema()

    def testROVersion(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "name": "foo",
                        "fingerprint": {
                            "board": "dartmonkey",
                            "ro-version": "123",
                        },
                    },
                ],
            },
        }
        jsonschema.validate(config, self._schema)

    def testROVersionMissingBoardName(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "name": "foo",
                        "fingerprint": {
                            # "ro-version" only allowed if "board" is also
                            # specified.
                            "ro-version": "123"
                        },
                    },
                ],
            },
        }
        with self.assertRaises(jsonschema.exceptions.ValidationError) as ctx:
            jsonschema.validate(config, self._schema)

        self.assertEqual(
            ctx.exception.message, "'board' is a dependency of 'ro-version'"
        )


class ValidateCameraSchema(unittest.TestCase):
    def setUp(self):
        self._schema = cros_config_schema.ReadSchema()

    def testDevices(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "name": "foo",
                        "camera": {
                            "count": 2,
                            "devices": [
                                {
                                    "interface": "usb",
                                    "facing": "front",
                                    "orientation": 180,
                                    "flags": {
                                        "support-1080p": False,
                                        "support-autofocus": False,
                                    },
                                    "ids": ["0123:abcd", "4567:efef"],
                                },
                                {
                                    "interface": "mipi",
                                    "facing": "back",
                                    "orientation": 0,
                                    "flags": {
                                        "support-1080p": True,
                                        "support-autofocus": True,
                                    },
                                },
                            ],
                        },
                    },
                ],
            },
        }
        jsonschema.validate(config, self._schema)

    def testInvalidUsbId(self):
        for invalid_usb_id in ("0123-abcd", "0123:Abcd", "123:abcd"):
            config = {
                "chromeos": {
                    "configs": [
                        {
                            "identity": {"platform-name": "foo", "sku-id": 1},
                            "name": "foo",
                            "camera": {
                                "count": 1,
                                "devices": [
                                    {
                                        "interface": "usb",
                                        "facing": "front",
                                        "orientation": 0,
                                        "flags": {
                                            "support-1080p": False,
                                            "support-autofocus": True,
                                        },
                                        "ids": [invalid_usb_id],
                                    },
                                ],
                            },
                        },
                    ],
                },
            }
            with self.assertRaises(jsonschema.ValidationError) as ctx:
                jsonschema.validate(config, self._schema)
            self.assertIn(
                "%r does not match" % invalid_usb_id, str(ctx.exception)
            )


CUSTOM_LABEL_CONFIG = """
chromeos:
  devices:
    - $name: 'customlabel'
      products:
        - $key-id: 'DEFAULT'
          $wallpaper: 'DEFAULT_WALLPAPER'
          $regulatory-label: 'DEFAULT_LABEL'
          $custom-label-tag: ''
          $brand-code: 'DEFAULT_BRAND_CODE'
          $stylus-category: 'none'
          $test-label: 'DEFAULT_TEST_LABEL'
        - $key-id: 'CUSTOM1'
          $wallpaper: 'CUSTOM1_WALLPAPER'
          $regulatory-label: 'CUSTOM1_LABEL'
          $custom-label-tag: 'CUSTOM1_TAG'
          $brand-code: 'CUSTOM1_BRAND_CODE'
          $oem-name: 'CUSTOM1_OEM_NAME'
          $stylus-category: 'none'
          $test-label: 'CUSTOM1_TEST_LABEL'
          $marketing-name: 'BRAND1_MARKETING_NAME1'
          $extra-ash-feature: 'CloudGamingDevice'
        - $key-id: 'CUSTOM2'
          $wallpaper: 'CUSTOM2_WALLPAPER'
          $regulatory-label: 'CUSTOM2_LABEL'
          $custom-label-tag: 'CUSTOM2_TAG'
          $brand-code: 'CUSTOM2_BRAND_CODE'
          $oem-name: 'CUSTOM2_OEM_NAME'
          $stylus-category: 'external'
          $test-label: 'CUSTOM2_TEST_LABEL'
          $marketing-name: 'BRAND2_MARKETING_NAME2'
          $extra-ash-feature: '{{$test-extra-ash-feature}}'
      skus:
        - config:
            identity:
              sku-id: 0
              custom-label-tag: '{{$custom-label-tag}}'
            name: '{{$name}}'
            brand-code: '{{$brand-code}}'
            wallpaper: '{{$wallpaper}}'
            regulatory-label: '{{$regulatory-label}}'
            hardware-properties:
              stylus-category: '{{$stylus-category}}'
            arc:
              build-properties:
                $marketing-name: ''
                marketing-name: '{{$marketing-name}}'
            branding:
              $oem-name: ''
              oem-name: '{{$oem-name}}'
              marketing-name: '{{$marketing-name}}'
            ui:
              ash-enabled-features:
              - CommonFeature
              - '{{$extra-ash-feature}}'
              $extra-ash-feature: ''
              $test-extra-ash-feature: ''
"""

INVALID_CUSTOM_LABEL_CONFIG = """
            # THIS WILL CAUSE THE FAILURE
            test-label: '{{$test-label}}'
"""

INVALID_CUSTOM_LABEL_CONFIG_FEATURE = """
            # THIS WILL CAUSE THE FAILURE
            $test-extra-ash-feature: 'OtherFeature'
"""


class ValidateConfigTests(unittest.TestCase):
    def testBasicValidation(self):
        cros_config_schema.ValidateConfig(
            cros_config_schema.TransformConfig(BASIC_CONFIG)
        )

    def testIdentitiesNotUnique(self):
        config = """
reef-9042-fw: &reef-9042-fw
  bcs-overlay: 'overlay-reef-private'
  ec-ro-image: 'Reef_EC.9042.87.1.tbz2'
  main-ro-image: 'Reef.9042.87.1.tbz2'
  main-rw-image: 'Reef.9042.110.0.tbz2'
  build-targets:
    coreboot: 'reef'
chromeos:
  devices:
    - $name: 'astronaut'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
    - $name: 'astronaut'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
"""
        with self.assertRaises(cros_config_schema.ValidationError) as ctx:
            cros_config_schema.ValidateConfig(
                cros_config_schema.TransformConfig(config)
            )
        self.assertIn("Identities are not unique", str(ctx.exception))

    def testFileCollision(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "name": "foo",
                        "audio": {
                            "main": {
                                "files": [
                                    {
                                        "destination": "/etc/cras/foo/dsp",
                                        "source": "foo/audio/cras-config/dsp",
                                    },
                                ],
                            }
                        },
                    },
                    {
                        "identity": {"platform-name": "foo", "sku-id": 2},
                        "name": "bar",
                        "audio": {
                            "main": {
                                "files": [
                                    {
                                        "destination": "/etc/cras/foo/dsp",
                                        "source": "bar/audio/cras-config/dsp",
                                    },
                                ],
                            }
                        },
                    },
                ],
            },
        }
        with self.assertRaises(cros_config_schema.ValidationError) as ctx:
            cros_config_schema.ValidateConfig(config)
        self.assertIn("File collision detected", str(ctx.exception))

    def testCustomLabelWithExternalStylusAndCloudGamingFeature(self):
        config = CUSTOM_LABEL_CONFIG
        cros_config_schema.ValidateConfig(
            cros_config_schema.TransformConfig(config)
        )

    def testCustomLabelWithOtherThanBrandChanges(self):
        config = CUSTOM_LABEL_CONFIG + INVALID_CUSTOM_LABEL_CONFIG
        with self.assertRaises(cros_config_schema.ValidationError) as ctx:
            cros_config_schema.ValidateConfig(
                cros_config_schema.TransformConfig(config)
            )
        self.assertIn("Custom label configs can only", str(ctx.exception))

    def testCustomLabelWithFeatureFlagOtherThanBrandChanges(self):
        config = CUSTOM_LABEL_CONFIG + INVALID_CUSTOM_LABEL_CONFIG_FEATURE
        with self.assertRaises(cros_config_schema.ValidationError) as ctx:
            cros_config_schema.ValidateConfig(
                cros_config_schema.TransformConfig(config)
            )
        self.assertIn("Custom label configs can only", str(ctx.exception))

    def testMultipleFingerprintFirmwareROVersionInvalid(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "fingerprint": {
                            "board": "bloonchipper",
                            "ro-version": "123",
                        },
                    },
                    {
                        "identity": {"platform-name": "foo", "sku-id": 2},
                        "fingerprint": {
                            "board": "bloonchipper",
                            "ro-version": "123",
                        },
                    },
                    # This causes the ValidationError.
                    {
                        "identity": {"platform-name": "foo", "sku-id": 3},
                        "fingerprint": {
                            "board": "bloonchipper",
                            "ro-version": "456",
                        },
                    },
                ],
            },
        }
        with self.assertRaises(cros_config_schema.ValidationError) as ctx:
            cros_config_schema.ValidateConfig(config)

        self.assertRegex(
            str(ctx.exception),
            re.compile(
                "You may not use different fingerprint firmware RO versions "
                "on the same board:.*"
            ),
        )

    def testMultipleFingerprintFirmwareROVersionsValid(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "fingerprint": {
                            "board": "bloonchipper",
                            "ro-version": "123",
                        },
                    },
                    {
                        "identity": {"platform-name": "foo", "sku-id": 2},
                        "fingerprint": {
                            "board": "dartmonkey",
                            "ro-version": "456",
                        },
                    },
                ],
            },
        }
        cros_config_schema.ValidateConfig(config)

    def testFingerprintFirmwareROVersionsValid(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "fingerprint": {"ro-version": "123"},
                    },
                    # This device does not have fingerprint
                    {
                        "identity": {"platform-name": "foo", "sku-id": 2},
                    },
                ],
            },
        }
        cros_config_schema.ValidateConfig(config)

    def testSideVolumeButtonConsistent(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "ui": {
                            "side-volume-button": {
                                "region": "screen",
                                "side": "left",
                            }
                        },
                        "hardware-properties": {"has-side-volume-button": True},
                    },
                    {
                        "identity": {"platform-name": "foo", "sku-id": 2},
                        "hardware-properties": {"has-side-volume-button": True},
                    },
                    {
                        "identity": {"platform-name": "foo", "sku-id": 3},
                    },
                ],
            },
        }
        cros_config_schema.ValidateConfig(config)

    def testSideVolumeButtonInconsistent(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "ui": {
                            "side-volume-button": {
                                "region": "screen",
                                "side": "left",
                            }
                        },
                    },
                ],
            },
        }
        try:
            cros_config_schema.ValidateConfig(config)
        except cros_config_schema.ValidationError as err:
            self.assertIn("has-side-volume-button is not", err.__str__())
        else:
            self.fail("ValidationError not raised")

    def testFeatureDeviceTypeValid(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {
                            "platform-name": "foo",
                            "sku-id": 1,
                            "feature-device-type": "on",
                        },
                    },
                    {
                        "identity": {
                            "sku-id": 1,
                            "platform-name": "foo",
                            "feature-device-type": "legacy",
                        },
                    },
                    {
                        "identity": {
                            "sku-id": 1,
                            "platform-name": "foo",
                        },
                    },
                    {
                        "identity": {
                            "platform-name": "foo",
                            "sku-id": 2,
                        },
                    },
                ],
            },
        }
        cros_config_schema.ValidateConfig(config)

    def testFeatureDeviceTypeInvalid(self):
        configs = [
            [
                {
                    "identity": {
                        "platform-name": "foo",
                        "sku-id": 1,
                        "feature-device-type": "on",
                    },
                },
            ],
            [
                {
                    "identity": {
                        "platform-name": "foo",
                        "sku-id": 1,
                        "feature-device-type": "legacy",
                    },
                },
            ],
            [
                {
                    "identity": {
                        "platform-name": "foo",
                        "sku-id": 1,
                        "feature-device-type": "on",
                    },
                },
                {
                    "identity": {
                        "platform-name": "foo",
                        "sku-id": 1,
                        "feature-device-type": "legacy",
                    },
                },
            ],
        ]
        for config in configs:
            with self.assertRaises(cros_config_schema.ValidationError) as ctx:
                cros_config_schema.ValidateConfig(
                    {
                        "chromeos": {
                            "configs": config
                            + [
                                {
                                    "identity": {
                                        "platform-name": "foo",
                                        "sku-id": 2,
                                    },
                                }
                            ],
                        }
                    }
                )

            self.assertRegex(
                str(ctx.exception),
                re.compile(".*feature-device-type absent identity.*"),
            )

    def testHdmiCecValid(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "hardware-properties": {"has-hdmi": True},
                        "hdmi-cec": {
                            "power-off-displays-on-shutdown": True,
                            "power-on-displays-on-boot": True,
                        },
                    },
                ],
            },
        }
        cros_config_schema.ValidateConfig(config)

    def testHdmiCecInvalid(self):
        config = {
            "chromeos": {
                "configs": [
                    {
                        "identity": {"platform-name": "foo", "sku-id": 1},
                        "hardware-properties": {},
                        "hdmi-cec": {
                            "power-off-displays-on-shutdown": True,
                            "power-on-displays-on-boot": True,
                        },
                    },
                ],
            },
        }
        try:
            cros_config_schema.ValidateConfig(config)
        except cros_config_schema.ValidationError as err:
            self.assertIn("hdmi-cec is present but", err.__str__())
        else:
            self.fail("ValidationError not raised")


class FilterBuildElements(unittest.TestCase):
    def testBasicFilterBuildElements(self):
        json_dict = cros_config_schema.FilterBuildElements(
            cros_config_schema.TransformConfig(BASIC_CONFIG), ["/firmware"]
        )
        self.assertNotIn("firmware", json_dict["chromeos"]["configs"][0])


class GetValidSchemaProperties(unittest.TestCase):
    def testGetValidSchemaProperties(self):
        schema_props = cros_config_schema.GetValidSchemaProperties()
        self.assertIn("cras-config-dir", schema_props["/audio/main"])
        self.assertIn("key-id", schema_props["/firmware-signing"])
        self.assertIn("files", schema_props["/audio/main"])
        self.assertIn("has-touchscreen", schema_props["/hardware-properties"])
        self.assertIn("count", schema_props["/camera"])


class SchemaContentsTests(unittest.TestCase):
    def testSchemaPropertyNames(self):
        """Validate that all property names use hyphen-case"""

        def _GetPropertyNames(obj, key_name):
            if key_name == "properties":
                yield from obj.keys()

            if isinstance(obj, dict):
                for key, value in obj.items():
                    yield from _GetPropertyNames(value, key)
            elif isinstance(obj, list):
                for item in obj:
                    yield from _GetPropertyNames(item, None)

        schema = cros_config_schema.ReadSchema()
        property_name_pattern = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
        for property_name in _GetPropertyNames(schema, None):
            self.assertRegex(
                property_name,
                property_name_pattern,
                "All property names must use hyphen-case.",
            )


class MainTests(unittest.TestCase):
    def testIdentityTableOut(self):
        base_path = os.path.join(this_dir, "../test_data")
        output = io.BytesIO()
        for fname in ("test.yaml", "test_arm.yaml"):
            with contextlib.redirect_stdout(io.StringIO()):
                cros_config_schema.Main(
                    None,
                    None,
                    None,
                    configs=[os.path.join(base_path, fname)],
                    identity_table_out=output,
                )
            # crosid unittests go in depth with testing the file
            # contents/format.  We just check that we put some good looking
            # data there (greater than 32 bytes is required).
        self.assertGreater(len(output.getvalue()), 32)


if __name__ == "__main__":
    unittest.main(module=__name__)
