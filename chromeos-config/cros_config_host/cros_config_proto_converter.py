#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Transforms config from /config/proto/api proto format to platform JSON."""

# pylint: disable=too-many-lines

import argparse
import collections.abc
import glob
import itertools
import json
import pathlib
import pprint
import os
import sys
import re

from typing import List

from collections import namedtuple

from google.protobuf import json_format
from google.protobuf import wrappers_pb2
from lxml import etree

from chromiumos.config.api import component_pb2
from chromiumos.config.api import device_brand_pb2
from chromiumos.config.api import topology_pb2
from chromiumos.config.payload import config_bundle_pb2
from chromiumos.config.api.software import brand_config_pb2
from chromiumos.config.api.software import ui_config_pb2

Config = namedtuple('Config', [
    'program', 'hw_design', 'odm', 'hw_design_config', 'device_brand',
    'device_signer_config', 'oem', 'sw_config', 'brand_config'
])

ConfigFiles = namedtuple('ConfigFiles', [
    'arc_hw_features', 'arc_media_profiles', 'touch_fw', 'dptf_map',
    'camera_map', 'wifi_sar_map'
])

CAMERA_CONFIG_DEST_PATH_TEMPLATE = '/etc/camera/camera_config_{}.json'
CAMERA_CONFIG_SOURCE_PATH_TEMPLATE = (
    'sw_build_config/platform/chromeos-config/camera/camera_config_{}.json')

DPTF_PATH = 'sw_build_config/platform/chromeos-config/thermal'
DPTF_FILE = 'dptf.dv'

TOUCH_PATH = 'sw_build_config/platform/chromeos-config/touch'
WALLPAPER_BASE_PATH = '/usr/share/chromeos-assets/wallpaper'

XML_DECLARATION = b'<?xml version="1.0" encoding="utf-8"?>\n'


def parse_args(argv):
  """Parse the available arguments.

  Invalid arguments or -h cause this function to print a message and exit.

  Args:
    argv: List of string arguments (excluding program name / argv[0])

  Returns:
    argparse.Namespace object containing the attributes.
  """
  parser = argparse.ArgumentParser(
      description='Converts source proto config into platform JSON config.')
  parser.add_argument(
      '-c',
      '--project_configs',
      nargs='+',
      type=str,
      help='Space delimited list of source protobinary project config files.')
  parser.add_argument(
      '-p',
      '--program_config',
      type=str,
      help='Path to the source program-level protobinary file')
  parser.add_argument(
      '-o', '--output', type=str, help='Output file that will be generated')
  return parser.parse_args(argv)


def _upsert(field, target, target_name):
  """Updates or inserts `field` within `target`.

  If `target_name` already exists within `target` an update is performed,
  otherwise, an insert is performed.
  """
  if field or field == 0:
    if target_name in target:
      target[target_name].update(field)
    else:
      target[target_name] = field


def _build_arc(config, config_files):
  build_properties = {
      # TODO(chromium:1126527) - Push this into the overlay itself.
      # This isn't/can't be device specific and shouldn't be configured as such.
      'device': '%s_cheets' % config.program.name.lower(),
      'first-api-level': '28',
      'marketing-name': config.device_brand.brand_name,
      'metrics-tag': config.hw_design.name.lower(),
      'product': config.program.name.lower(),
  }
  if config.oem:
    build_properties['oem'] = config.oem.name
  result = {'build-properties': build_properties}
  config_id = _get_formatted_config_id(config.hw_design_config)
  if config_id in config_files.arc_hw_features:
    result['hardware-features'] = config_files.arc_hw_features[config_id]
  if config_id in config_files.arc_media_profiles:
    result['media-profiles'] = config_files.arc_media_profiles[config_id]
  topology = config.hw_design_config.hardware_topology
  ppi = topology.screen.hardware_feature.screen.panel_properties.pixels_per_in
  # Only set for high resolution displays
  if ppi and ppi > 250:
    result['scale'] = ppi

  return result


def _check_percentage_value(value: float, description: str):
  if not 0 <= value <= 100:
    raise Exception('Value %.1f out of range [0, 100] for %s' %
                    (value, description))


def _check_increasing_sequence(values: [float], description: str):
  for lhs, rhs in zip(values, values[1:]):
    if lhs >= rhs:
      raise Exception(
          'Value %s is not strictly larger than previous value %s for %s' %
          (rhs, lhs, description))


def _check_als_steps(steps: [component_pb2.Component.AlsStep],
                     description: str):
  for idx, step in enumerate(steps):
    _check_percentage_value(step.ac_backlight_percent,
                            '%s[%d].ac_backlight_percent' % (description, idx))
    _check_percentage_value(
        step.battery_backlight_percent,
        '%s[%d].battery_backlight_percent' % (description, idx))

  _check_increasing_sequence([step.ac_backlight_percent for step in steps],
                             '%s.ac_backlight_percent' % description)
  _check_increasing_sequence([step.battery_backlight_percent for step in steps],
                             '%s.battery_backlight_percent' % description)
  _check_increasing_sequence(
      [step.lux_increase_threshold for step in steps[:-1]],
      '%s.lux_increase_threshold' % description)
  _check_increasing_sequence(
      [step.lux_decrease_threshold for step in steps[1:]],
      '%s.lux_decrease_threshold' % description)

  if steps[0].lux_decrease_threshold != -1:
    raise Exception('%s[0].lux_decrease_threshold should be unset, not %d' %
                    (description, steps[0].lux_decrease_threshold))
  if steps[-1].lux_increase_threshold != -1:
    raise Exception('%s[0].lux_decrease_threshold should be unset, not %d' %
                    (description, steps[-1].lux_increase_threshold))


def _format_als_step(als_step: component_pb2.Component.AlsStep) -> str:
  battery_percent = ''
  if als_step.battery_backlight_percent != als_step.ac_backlight_percent:
    battery_percent = ' %s' % _format_power_pref_value(
        als_step.battery_backlight_percent)
  return '%s%s %s %s' % (
      _format_power_pref_value(als_step.ac_backlight_percent),
      battery_percent,
      _format_power_pref_value(als_step.lux_decrease_threshold),
      _format_power_pref_value(als_step.lux_increase_threshold),
  )


def _format_power_pref_value(value) -> str:
  if isinstance(value, str):
    return value
  if isinstance(value, collections.abc.Sequence):
    return '\n'.join(_format_power_pref_value(x) for x in value)
  if isinstance(value, bool):
    return str(int(value))
  if isinstance(
      value,
      (wrappers_pb2.DoubleValue, wrappers_pb2.FloatValue,
       wrappers_pb2.UInt32Value, wrappers_pb2.UInt64Value,
       wrappers_pb2.Int32Value, wrappers_pb2.Int64Value, wrappers_pb2.BoolValue,
       wrappers_pb2.StringValue, wrappers_pb2.BytesValue)):
    return _format_power_pref_value(value.value)
  if isinstance(value, component_pb2.Component.AlsStep):
    return _format_als_step(value)
  return str(value)


def _build_derived_platform_power_prefs(capabilities) -> dict:
  result = {}

  # Falsy values are filtered out, deferring to the equivalent powerd default
  # pref values. Dark resume is inverted; wrap it so False values are forwarded.
  if capabilities.dark_resume:
    result['disable-dark-resume'] = wrappers_pb2.BoolValue(
        value=not capabilities.dark_resume)
  result['suspend-to-idle'] = capabilities.suspend_to_idle
  result['wake-on-dp'] = capabilities.wake_on_dp

  return result


def _build_derived_power_prefs(config: Config) -> dict:
  """Builds a partial 'power' property derived from hardware features."""
  present = topology_pb2.HardwareFeatures.PRESENT
  hw_features = config.hw_design_config.hardware_features

  form_factor = hw_features.form_factor.form_factor
  if (form_factor ==
      topology_pb2.HardwareFeatures.FormFactor.FORM_FACTOR_UNKNOWN):
    return {}

  result = {}

  result['external-display-only'] = form_factor in (
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBIT,
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBOX,
  )

  light_sensor = hw_features.light_sensor
  result['has-ambient-light-sensor'] = (
      light_sensor.lid_lightsensor,
      light_sensor.base_lightsensor).count(present)

  result['has-keyboard-backlight'] = hw_features.keyboard.backlight == present

  if hw_features.keyboard.backlight_user_steps:
    _check_increasing_sequence(hw_features.keyboard.backlight_user_steps,
                               'keyboard.backlight_user_steps')
    if hw_features.keyboard.backlight_user_steps[0] != 0:
      raise Exception(
          'keyboard.backlight_user_steps starts at %.1f instead of 0.0' %
          hw_features.keyboard.backlight_user_steps[0])

    result['keyboard-backlight-user-steps'] = (
        hw_features.keyboard.backlight_user_steps)

  if hw_features.screen.panel_properties.min_visible_backlight_level:
    result['min-visible-backlight-level'] = (
        hw_features.screen.panel_properties.min_visible_backlight_level)

  if hw_features.screen.panel_properties.HasField('turn_off_screen_timeout_ms'):
    result['turn-off-screen-timeout-ms'] = (
        hw_features.screen.panel_properties.turn_off_screen_timeout_ms)

  if light_sensor.lid_lightsensor == present:
    if hw_features.screen.panel_properties.als_steps:
      _check_als_steps(hw_features.screen.panel_properties.als_steps,
                       'hw_features.screen.panel_properties.als_steps')
      result['internal-backlight-als-steps'] = (
          hw_features.screen.panel_properties.als_steps)
  else:
    if hw_features.screen.panel_properties.no_als_battery_brightness:
      _check_percentage_value(
          hw_features.screen.panel_properties.no_als_battery_brightness,
          'screen.panel_properties.no_als_battery_brightness')
      result['internal-backlight-no-als-battery-brightness'] = (
          hw_features.screen.panel_properties.no_als_battery_brightness)

    if hw_features.screen.panel_properties.no_als_ac_brightness:
      _check_percentage_value(
          hw_features.screen.panel_properties.no_als_ac_brightness,
          'screen.panel_properties.no_als_ac_brightness')
      result['internal-backlight-no-als-ac-brightness'] = (
          hw_features.screen.panel_properties.no_als_ac_brightness)

  result.update(
      _build_derived_platform_power_prefs(config.program.platform.capabilities))

  result['usb-min-ac-watts'] = hw_features.power_supply.usb_min_ac_watts

  return dict((k, _format_power_pref_value(v)) for k, v in result.items() if v)


def _build_power(config: Config) -> dict:
  """Builds the 'power' property from cros_config_schema."""
  power_prefs_map = _build_derived_power_prefs(config)
  power_prefs = config.sw_config.power_config.preferences
  power_prefs_map.update(
      (x.replace('_', '-'), power_prefs[x]) for x in power_prefs)
  return power_prefs_map


def _build_ash_flags(config: Config) -> List[str]:
  """Returns a list of Ash flags for config.

  Ash is the window manager and system UI for ChromeOS, see
  https://chromium.googlesource.com/chromium/src/+/HEAD/ash/.
  """
  # pylint: disable=too-many-branches

  # A map from flag name -> value. Value may be None for boolean flags.
  flags = {}

  # Adds a flag name -> value pair to flags map. |value| may be None for boolean
  # flags.
  def _add_flag(name, value=None):
    flags[name] = value

  hw_features = config.hw_design_config.hardware_features
  if hw_features.stylus.stylus == topology_pb2.HardwareFeatures.Stylus.INTERNAL:
    _add_flag('has-internal-stylus')

  fp_loc = hw_features.fingerprint.location
  if fp_loc and fp_loc != topology_pb2.HardwareFeatures.Fingerprint.NOT_PRESENT:
    loc_name = topology_pb2.HardwareFeatures.Fingerprint.Location.Name(fp_loc)
    _add_flag('fingerprint-sensor-location', loc_name.lower().replace('_', '-'))

  wallpaper = config.brand_config.wallpaper
  # If a wallpaper is set, the 'default-wallpaper-is-oem' flag needs to be set.
  # If a wallpaper is not set, the 'default_[large|small].jpg' wallpapers
  # should still be set.
  if wallpaper:
    _add_flag('default-wallpaper-is-oem')
  else:
    wallpaper = 'default'

  for size in ('small', 'large'):
    _add_flag(f'default-wallpaper-{size}',
              f'{WALLPAPER_BASE_PATH}/{wallpaper}_{size}.jpg')

    # For each size, also install 'guest' and 'child' wallpapers.
    for wallpaper_type in ('guest', 'child'):
      _add_flag(f'{wallpaper_type}-wallpaper-{size}',
                f'{WALLPAPER_BASE_PATH}/{wallpaper_type}_{size}.jpg')

  regulatory_label = config.brand_config.regulatory_label
  if regulatory_label:
    _add_flag('regulatory-label-dir', regulatory_label)

  _add_flag('arc-build-properties', {
      'device': '%s_cheets' % config.program.name.lower(),
      'firstApiLevel': '28',
  })

  power_button = hw_features.power_button
  if power_button.edge:
    _add_flag(
        'ash-power-button-position',
        json.dumps({
            'edge':
                topology_pb2.HardwareFeatures.Button.Edge.Name(power_button.edge
                                                              ).lower(),
            # Starlark sometimes represents float literals strangely, e.g. changing
            # 0.9 to 0.899999. Round to two digits here.
            'position':
                round(power_button.position, 2)
        }))

  volume_button = hw_features.volume_button
  if volume_button.edge:
    _add_flag(
        'ash-side-volume-button-position',
        json.dumps({
            'region':
                topology_pb2.HardwareFeatures.Button.Region.Name(
                    volume_button.region).lower(),
            'side':
                topology_pb2.HardwareFeatures.Button.Edge.Name(
                    volume_button.edge).lower(),
        }))

  form_factor = hw_features.form_factor.form_factor
  lid_accel = hw_features.accelerometer.lid_accelerometer
  if form_factor == topology_pb2.HardwareFeatures.FormFactor.CHROMEBASE:
    _add_flag('touchscreen-usable-while-screen-off')
    if lid_accel == topology_pb2.HardwareFeatures.PRESENT:
      _add_flag('supports-clamshell-auto-rotation')

  if config.sw_config.ui_config.extra_web_apps_dir:
    _add_flag('extra-web-apps-dir',
              config.sw_config.ui_config.extra_web_apps_dir)

  if hw_features.microphone_mute_switch.present == topology_pb2.HardwareFeatures.PRESENT:
    _add_flag('enable-microphone-mute-switch-device')

  requisition = config.sw_config.ui_config.requisition
  if (requisition == ui_config_pb2.UiConfig.REQUISITION_MEETHW and
      form_factor == topology_pb2.HardwareFeatures.FormFactor.CHROMEBASE):
    _add_flag('oobe-large-screen-special-scaling')
    _add_flag('enable-virtual-keyboard')

  # This affects a large number of projects, so stage the rollout behind each
  # program updating to configure platform capabilities.
  # TODO(b/218220022, b/195298103): Remove this condition once all programs
  # have opted-in or generated configs are not checked-in.
  if config.program.platform.HasField('capabilities'):
    if form_factor in (topology_pb2.HardwareFeatures.FormFactor.CONVERTIBLE,
                       topology_pb2.HardwareFeatures.FormFactor.DETACHABLE,
                       topology_pb2.HardwareFeatures.FormFactor.CHROMESLATE):
      _add_flag('enable-touchview')

  return sorted([f'--{k}={v}' if v else f'--{k}' for k, v in flags.items()])


def _build_ui(config: Config) -> dict:
  """Builds the 'ui' property from cros_config_schema."""
  result = {'extra-ash-flags': _build_ash_flags(config)}
  help_content_id = config.brand_config.help_content_id
  if help_content_id:
    result['help-content-id'] = help_content_id
  return result


def _build_keyboard(hw_topology):
  if not hw_topology.HasField('keyboard'):
    return None

  keyboard = hw_topology.keyboard.hardware_feature.keyboard
  result = {}
  if keyboard.backlight == topology_pb2.HardwareFeatures.PRESENT:
    result['backlight'] = True
  if keyboard.numeric_pad == topology_pb2.HardwareFeatures.PRESENT:
    result['numpad'] = True

  return result


def _build_bluetooth(config):
  bt_flags = config.sw_config.bluetooth_config.flags
  # Convert to native map (from proto wrapper)
  bt_flags_map = dict(bt_flags)
  result = {}
  if bt_flags_map:
    result['flags'] = bt_flags_map
  return result


def _build_ath10k_config(ath10k_config):
  """Builds the wifi configuration for the ath10k driver.

  Args:
    ath10k_config: Ath10kConfig config.

  Returns:
    wifi configuration for the ath10k driver.
  """
  result = {}

  def power_chain(power):
    return {
        'limit-2g': power.limit_2g,
        'limit-5g': power.limit_5g,
    }

  result['tablet-mode-power-table-ath10k'] = power_chain(
      ath10k_config.tablet_mode_power_table)
  result['non-tablet-mode-power-table-ath10k'] = power_chain(
      ath10k_config.non_tablet_mode_power_table)
  return result


def _build_rtw88_config(rtw88_config):
  """Builds the wifi configuration for the rtw88 driver.

  Args:
    rtw88_config: Rtw88Config config.

  Returns:
    wifi configuration for the rtw88 driver.
  """
  result = {}

  def power_chain(power):
    return {
        'limit-2g': power.limit_2g,
        'limit-5g-1': power.limit_5g_1,
        'limit-5g-3': power.limit_5g_3,
        'limit-5g-4': power.limit_5g_4,
    }

  result['tablet-mode-power-table-rtw'] = power_chain(
      rtw88_config.tablet_mode_power_table)
  result['non-tablet-mode-power-table-rtw'] = power_chain(
      rtw88_config.non_tablet_mode_power_table)

  def offsets(offset):
    return {
        'offset-2g': offset.offset_2g,
        'offset-5g': offset.offset_5g,
    }

  result['geo-offsets-fcc'] = offsets(rtw88_config.offset_fcc)
  result['geo-offsets-eu'] = offsets(rtw88_config.offset_eu)
  result['geo-offsets-rest-of-world'] = offsets(rtw88_config.offset_other)
  return result


def _build_intel_config(config, config_files):
  """Builds the wifi configuration for the intel driver.

  Args:
    config: Config namedtuple
    config_files: Map to look up the generated config files.

  Returns:
    wifi configuration for the intel driver.
  """
  design_name = config.hw_design.name.lower()
  return config_files.wifi_sar_map.get(design_name)


def _build_mtk_config(mtk_config):
  """Builds the wifi configuration for the mtk driver.

  Args:
    mtk_config: MtkConfig config.

  Returns:
    wifi configuration for the mtk driver.
  """
  result = {}

  def power_chain(power):
    return {
        'limit-2g': power.limit_2g,
        'limit-5g-1': power.limit_5g_1,
        'limit-5g-2': power.limit_5g_2,
        'limit-5g-3': power.limit_5g_3,
        'limit-5g-4': power.limit_5g_4,
    }

  if mtk_config.HasField('tablet_mode_power_table'):
    result['tablet-mode-power-table-mtk'] = power_chain(
        mtk_config.tablet_mode_power_table)
  if mtk_config.HasField('non_tablet_mode_power_table'):
    result['non-tablet-mode-power-table-mtk'] = power_chain(
        mtk_config.non_tablet_mode_power_table)

  def geo_power_chain(power):
    return {
        'limit-2g': power.limit_2g,
        'limit-5g': power.limit_5g,
        'offset-2g': power.offset_2g,
        'offset-5g': power.offset_5g,
    }

  if mtk_config.HasField('fcc_power_table'):
    result['fcc-power-table-mtk'] = geo_power_chain(mtk_config.fcc_power_table)
  if mtk_config.HasField('eu_power_table'):
    result['eu-power-table-mtk'] = geo_power_chain(mtk_config.eu_power_table)
  if mtk_config.HasField('other_power_table'):
    result['rest-of-world-power-table-mtk'] = geo_power_chain(
        mtk_config.other_power_table)

  return result


def _build_wifi(config, config_files):
  """Builds the wifi configuration.

  Args:
    config: Config namedtuple
    config_files: Map to look up the generated config files.

  Returns:
    wifi configuration.
  """
  config_field = config.sw_config.wifi_config.WhichOneof('wifi_config')
  if config_field == 'ath10k_config':
    return _build_ath10k_config(config.sw_config.wifi_config.ath10k_config)
  if config_field == 'rtw88_config':
    return _build_rtw88_config(config.sw_config.wifi_config.rtw88_config)
  if config_field == 'intel_config':
    return _build_intel_config(config, config_files)
  if config_field == 'mtk_config':
    return _build_mtk_config(config.sw_config.wifi_config.mtk_config)
  return {}


def _build_health_cached_vpd(health_config):
  if not health_config.HasField('cached_vpd'):
    return None

  cached_vpd = health_config.cached_vpd
  result = {}
  _upsert(cached_vpd.has_sku_number, result, 'has-sku-number')
  return result


def _build_health_battery(health_config):
  if not health_config.HasField('battery'):
    return None

  battery = health_config.battery
  result = {}
  _upsert(battery.has_smart_battery_info, result, 'has-smart-battery-info')
  return result


def _build_health(config: Config):
  """Builds the health configuration.

  Args:
    config: Config namedtuple

  Returns:
    health configuration.
  """
  if not config.sw_config.health_config:
    return None

  health_config = config.sw_config.health_config
  result = {}
  _upsert(_build_health_cached_vpd(health_config), result, 'cached-vpd')
  _upsert(_build_health_battery(health_config), result, 'battery')
  return result


def _build_branding(config: Config):
  """Builds the branding configuration.

  Args:
    config: Config namedtuple

  Returns:
    branding configuration.
  """
  result = {}
  if config.device_brand.export_oem_info and config.oem:
    _upsert(config.oem.name, result, 'oem-name')
  if config.device_brand:
    _upsert(config.device_brand.brand_name, result, 'marketing-name')
  return result


def _build_fingerprint(hw_topology):
  if not hw_topology.HasField('fingerprint'):
    return None

  fp = hw_topology.fingerprint.hardware_feature.fingerprint
  result = {}
  if fp.location != topology_pb2.HardwareFeatures.Fingerprint.NOT_PRESENT:
    location = fp.Location.DESCRIPTOR.values_by_number[fp.location].name
    result['sensor-location'] = location.lower().replace('_', '-')
    if fp.board:
      result['board'] = fp.board
    if fp.ro_version:
      result['ro-version'] = fp.ro_version

  return result


def _build_hps(hw_topology):
  if not hw_topology.HasField('hps'):
    return None

  hps = hw_topology.hps.hardware_feature.hps
  result = {}
  if hps.present == topology_pb2.HardwareFeatures.PRESENT:
    result['has-hps'] = True

  return result


def _build_poe(hw_topology):
  if not hw_topology.HasField('poe'):
    return None

  poe = hw_topology.poe.hardware_feature.poe
  result = {}
  if poe.present == topology_pb2.HardwareFeatures.PRESENT:
    result['has-poe-peripheral-support'] = True

  return result


def _build_hardware_properties(hw_topology):
  if not hw_topology.HasField('form_factor'):
    return None

  form_factor = hw_topology.form_factor.hardware_feature.form_factor.form_factor
  result = {}
  if form_factor in [
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBIT,
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBASE,
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBOX
  ]:
    result['psu-type'] = 'AC_only'
  else:
    result['psu-type'] = 'battery'

  result['has-backlight'] = form_factor not in [
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBIT,
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBOX
  ]

  form_factor_names = {
      topology_pb2.HardwareFeatures.FormFactor.CLAMSHELL: 'CHROMEBOOK',
      topology_pb2.HardwareFeatures.FormFactor.CONVERTIBLE: 'CHROMEBOOK',
      topology_pb2.HardwareFeatures.FormFactor.DETACHABLE: 'CHROMEBOOK',
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBASE: 'CHROMEBASE',
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBOX: 'CHROMEBOX',
      topology_pb2.HardwareFeatures.FormFactor.CHROMEBIT: 'CHROMEBIT',
      topology_pb2.HardwareFeatures.FormFactor.CHROMESLATE: 'CHROMEBOOK',
  }
  if form_factor in form_factor_names:
    result['form-factor'] = form_factor_names[form_factor]

  return result


def _fw_bcs_path(payload):
  if payload and payload.firmware_image_name:
    return 'bcs://%s.%d.%d.%d.tbz2' % (
        payload.firmware_image_name, payload.version.major,
        payload.version.minor, payload.version.patch)

  return None


def _fw_build_target(payload):
  if payload:
    return payload.build_target_name

  return None


def _build_firmware(config):
  """Returns firmware config, or None if no build targets."""
  fw_payload_config = config.sw_config.firmware
  fw_build_config = config.sw_config.firmware_build_config
  main_ro = fw_payload_config.main_ro_payload
  main_rw = fw_payload_config.main_rw_payload
  ec_ro = fw_payload_config.ec_ro_payload
  pd_ro = fw_payload_config.pd_ro_payload

  build_targets = {}

  _upsert(fw_build_config.build_targets.bmpblk, build_targets, 'bmpblk')
  _upsert(fw_build_config.build_targets.depthcharge, build_targets,
          'depthcharge')
  _upsert(fw_build_config.build_targets.coreboot, build_targets, 'coreboot')
  _upsert(fw_build_config.build_targets.ec, build_targets, 'ec')
  _upsert(
      list(fw_build_config.build_targets.ec_extras), build_targets, 'ec_extras')
  _upsert(fw_build_config.build_targets.libpayload, build_targets, 'libpayload')
  _upsert(fw_build_config.build_targets.zephyr_ec, build_targets, 'zephyr-ec')

  if not build_targets:
    return None

  result = {
      'bcs-overlay': 'overlay-%s-private' % config.program.name.lower(),
      'build-targets': build_targets,
  }

  _upsert(main_ro.firmware_image_name.lower(), result, 'image-name')

  _upsert(_fw_bcs_path(main_ro), result, 'main-ro-image')
  _upsert(_fw_bcs_path(main_rw), result, 'main-rw-image')
  _upsert(_fw_bcs_path(ec_ro), result, 'ec-ro-image')
  _upsert(_fw_bcs_path(pd_ro), result, 'pd-ro-image')

  _upsert(
      config.hw_design_config.hardware_features.fw_config.value,
      result,
      'firmware-config',
  )

  return result


def _build_fw_signing(config, whitelabel):
  if config.sw_config.firmware and config.device_signer_config:
    hw_design = config.hw_design.name.lower()
    brand_scan_config = config.brand_config.scan_config
    if brand_scan_config and brand_scan_config.whitelabel_tag:
      signature_id = '%s-%s' % (hw_design, brand_scan_config.whitelabel_tag)
    else:
      signature_id = hw_design

    result = {
        'key-id': config.device_signer_config.key_id,
        'signature-id': signature_id,
    }
    if whitelabel:
      result['sig-id-in-customization-id'] = True
    return result
  return {}


def _file(source, destination):
  return {'destination': str(destination), 'source': str(source)}


def _file_v2(build_path, system_path):
  return {'build-path': build_path, 'system-path': system_path}


class _AudioConfigBuilder:
  _ALSA_PATH = pathlib.PurePath('/usr/share/alsa/ucm')
  _CRAS_PATH = pathlib.PurePath('/etc/cras')
  _SOUND_CARD_INIT_PATH = pathlib.PurePath('/etc/sound_card_init')
  _MODULE_PATH = pathlib.PurePath('/etc/modprobe.d')
  _AUDIO_CONFIG_PATH = 'audio'
  AudioConfigStructure = (
      topology_pb2.HardwareFeatures.Audio.AudioConfigStructure)
  Camera = topology_pb2.HardwareFeatures.Camera

  def __init__(self, config):
    self._config = config

    self._files = []
    self._ucm_suffixes = set()
    self._sound_card_init_confs = set()

  @property
  def _program_audio(self):
    return self._config.program.audio_config

  @property
  def _audio(self):
    return self._hw_features.audio

  def _design_name(self):
    return self._config.hw_design.name.lower()

  @property
  def _hw_features(self):
    return self._config.hw_design_config.hardware_features

  def _build_source_path(self, config_structure, config_path):
    if config_structure == self.AudioConfigStructure.COMMON:
      return pathlib.PurePath('common').joinpath(self._AUDIO_CONFIG_PATH,
                                                 config_path)
    if config_structure == self.AudioConfigStructure.DESIGN:
      return pathlib.PurePath(self._design_name).joinpath(
          self._AUDIO_CONFIG_PATH, config_path)
    return None

  def _count_mics(self, facing):
    return sum(device.microphone_count.value
               for device in self._hw_features.camera.devices
               if device.facing == facing)

  def _build_ucm_suffix(self, card_config):
    ucm_suffix_format = self._program_audio.default_ucm_suffix
    if card_config.HasField('ucm_suffix'):
      ucm_suffix_format = card_config.ucm_suffix.value

    design_for_ucm = self._design_name
    if card_config.ucm_config == self.AudioConfigStructure.COMMON:
      design_for_ucm = ''
    uf_mics = self._count_mics(self.Camera.FACING_FRONT)
    wf_mics = self._count_mics(self.Camera.FACING_BACK)
    mic_details = [(uf_mics, 'uf'), (wf_mics, 'wf')]
    ucm_suffix = ucm_suffix_format.format(
        headset_codec=(topology_pb2.HardwareFeatures.Audio.AudioCodec.Name(
            self._audio.headphone_codec)).lower()
        if self._audio.headphone_codec else '',
        speaker_amp=(topology_pb2.HardwareFeatures.Audio.Amplifier.Name(
            self._audio.speaker_amp)).lower()
        if self._audio.speaker_amp else '',
        design=design_for_ucm,
        camera_count=len(self._hw_features.camera.devices),
        mic_description=''.join('{}{}'.format(*position) if position[0] else ''
                                for position in mic_details),
        total_mic_count=uf_mics + wf_mics,
        user_facing_mic_count=uf_mics,
        world_facing_mic_count=wf_mics,
    )
    return '.'.join(
        [component for component in ucm_suffix.split('.') if component])

  def _build_audio_card(self, card_config):
    ucm_suffix = self._build_ucm_suffix(card_config)
    card_with_suffix = '.'.join([card_config.card_name, ucm_suffix]).strip('.')
    card, _, card_suffix = card_config.card_name.partition('.')
    ucm_suffix = '.'.join([card_suffix, ucm_suffix]).strip('.')
    self._ucm_suffixes.add(ucm_suffix)

    ucm_config_source_directory = self._build_source_path(
        card_config.ucm_config, 'ucm-config')
    self._files.append(
        _file(
            ucm_config_source_directory.joinpath(card_with_suffix, 'HiFi.conf'),
            self._ALSA_PATH.joinpath(card_with_suffix, 'HiFi.conf')))
    self._files.append(
        _file(
            ucm_config_source_directory.joinpath(card_with_suffix,
                                                 f'{card_with_suffix}.conf'),
            self._ALSA_PATH.joinpath(card_with_suffix,
                                     f'{card_with_suffix}.conf')))

    cras_config_source_path = self._build_source_path(card_config.cras_config,
                                                      'cras-config')
    if cras_config_source_path:
      self._files.append(
          _file(
              cras_config_source_path.joinpath(card).with_suffix(
                  '.card_settings'),
              self._CRAS_PATH.joinpath(self._design_name, card)))

    card_init_config_source_path = self._build_source_path(
        card_config.sound_card_init_config, 'sound_card_init')

    if card_init_config_source_path:
      speaker_amp = (
          topology_pb2.HardwareFeatures.Audio.Amplifier.Name(
              self._audio.speaker_amp))
      sound_card_init_conf = f'{self._design_name}.{speaker_amp}.yaml'
      self._files.append(
          _file(
              card_init_config_source_path.joinpath(sound_card_init_conf),
              self._SOUND_CARD_INIT_PATH.joinpath(sound_card_init_conf)))
      self._sound_card_init_confs.add(sound_card_init_conf)
    else:
      self._sound_card_init_confs.add(None)

  @staticmethod
  def _select_from_set(values, description):
    if not values:
      return None
    if len(values) == 1:
      return next(iter(values))
    values -= set([None, ''])
    if len(values) == 1:
      return next(iter(values))
    raise Exception(f'Inconsistent values for "{description}": {values}')

  def build(self):
    """Builds the audio configuration."""
    if not self._hw_features.audio or not self._hw_features.audio.card_configs:
      return {}

    program_name = self._config.program.name.lower()

    for card_config in itertools.chain(self._audio.card_configs,
                                       self._program_audio.card_configs):
      self._build_audio_card(card_config)

    cras_config_source_path = self._build_source_path(self._audio.cras_config,
                                                      'cras-config')
    if cras_config_source_path:
      for filename in ['dsp.ini', 'board.ini']:
        self._files.append(
            _file(
                cras_config_source_path.joinpath(filename),
                self._CRAS_PATH.joinpath(self._design_name, filename)))

    if self._program_audio.has_module_file:
      module_name = f'alsa-{program_name}.conf'
      self._files.append(
          _file(
              self._build_source_path(
                  self.AudioConfigStructure.COMMON,
                  'alsa-module-config').joinpath(module_name),
              self._MODULE_PATH.joinpath(module_name)))

    result = {
        'main': {
            'cras-config-dir': self._design_name,
            'files': self._files,
        }
    }

    ucm_suffix = self._select_from_set(self._ucm_suffixes, 'ucm-suffix')
    if ucm_suffix:
      result['main']['ucm-suffix'] = ucm_suffix

    sound_card_init_conf = self._select_from_set(self._sound_card_init_confs,
                                                 'sound-card-init-conf')
    if sound_card_init_conf:
      result['main']['sound-card-init-conf'] = sound_card_init_conf
      result['main']['speaker-amp'] = (
          topology_pb2.HardwareFeatures.Audio.Amplifier.Name(
              self._audio.speaker_amp))

    return result


def _build_audio(config):
  # pylint: disable=too-many-locals
  if not config.sw_config.audio_configs:
    builder = _AudioConfigBuilder(config)
    return builder.build()

  alsa_path = '/usr/share/alsa/ucm'
  cras_path = '/etc/cras'
  sound_card_init_path = '/etc/sound_card_init'
  design_name = config.hw_design.name.lower()
  program_name = config.program.name.lower()
  files = []
  ucm_suffix = None
  sound_card_init_conf = None
  audio_pb = topology_pb2.HardwareFeatures.Audio
  hw_feature = config.hw_design_config.hardware_features

  for audio in config.sw_config.audio_configs:
    card = audio.card_name
    card_with_suffix = audio.card_name
    if audio.ucm_suffix:
      # TODO: last ucm_suffix wins.
      ucm_suffix = audio.ucm_suffix
      card_with_suffix += '.' + audio.ucm_suffix
    if audio.ucm_file:
      files.append(
          _file(audio.ucm_file,
                '%s/%s/HiFi.conf' % (alsa_path, card_with_suffix)))
    if audio.ucm_master_file:
      files.append(
          _file(
              audio.ucm_master_file, '%s/%s/%s.conf' %
              (alsa_path, card_with_suffix, card_with_suffix)))
    if audio.card_config_file:
      files.append(
          _file(audio.card_config_file,
                '%s/%s/%s' % (cras_path, design_name, card)))
    if audio.dsp_file:
      files.append(
          _file(audio.dsp_file, '%s/%s/dsp.ini' % (cras_path, design_name)))
    if audio.module_file:
      files.append(
          _file(audio.module_file,
                '/etc/modprobe.d/alsa-%s.conf' % program_name))
    if audio.board_file:
      files.append(
          _file(audio.board_file, '%s/%s/board.ini' % (cras_path, design_name)))
    if audio.sound_card_init_file:
      sound_card_init_conf = design_name + '.yaml'
      files.append(
          _file(audio.sound_card_init_file,
                '%s/%s.yaml' % (sound_card_init_path, design_name)))

  result = {
      'main': {
          'cras-config-dir': design_name,
          'files': files,
      }
  }

  if ucm_suffix:
    result['main']['ucm-suffix'] = ucm_suffix
  if sound_card_init_conf:
    result['main']['sound-card-init-conf'] = sound_card_init_conf
    result['main']['speaker-amp'] = audio_pb.Amplifier.Name(
        hw_feature.audio.speaker_amp)

  return result


def _build_camera(hw_topology):
  camera_pb = topology_pb2.HardwareFeatures.Camera
  camera = hw_topology.camera.hardware_feature.camera
  result = {'count': len(camera.devices)}
  if camera.devices:
    result['devices'] = []
    for device in camera.devices:
      interface = {
          camera_pb.INTERFACE_USB: 'usb',
          camera_pb.INTERFACE_MIPI: 'mipi',
      }[device.interface]
      facing = {
          camera_pb.FACING_FRONT: 'front',
          camera_pb.FACING_BACK: 'back',
      }[device.facing]
      orientation = {
          camera_pb.ORIENTATION_0: 0,
          camera_pb.ORIENTATION_90: 90,
          camera_pb.ORIENTATION_180: 180,
          camera_pb.ORIENTATION_270: 270,
      }[device.orientation]
      flags = {
          'support-1080p':
              bool(device.flags & camera_pb.FLAGS_SUPPORT_1080P),
          'support-autofocus':
              bool(device.flags & camera_pb.FLAGS_SUPPORT_AUTOFOCUS),
      }
      dev = {
          'interface': interface,
          'facing': facing,
          'orientation': orientation,
          'flags': flags,
          'ids': list(device.ids),
      }
      if device.privacy_switch != topology_pb2.HardwareFeatures.PRESENT_UNKNOWN:
        dev['has-privacy-switch'] = device.privacy_switch == topology_pb2.HardwareFeatures.PRESENT
      result['devices'].append(dev)
  return result


def _build_identity(hw_scan_config, program, brand_scan_config=None):
  identity = {}
  _upsert(hw_scan_config.firmware_sku, identity, 'sku-id')
  _upsert(hw_scan_config.smbios_name_match, identity, 'smbios-name-match')
  # 'platform-name' is needed to support 'mosys platform name'. Clients should
  # no longer require platform name, but set it here for backwards compatibility.
  if program.mosys_platform_name:
    _upsert(program.mosys_platform_name, identity, 'platform-name')
  else:
    _upsert(program.name, identity, 'platform-name')

  # ARM architecture
  _upsert(hw_scan_config.device_tree_compatible_match, identity,
          'device-tree-compatible-match')

  if brand_scan_config:
    _upsert(brand_scan_config.whitelabel_tag, identity, 'whitelabel-tag')

  return identity


def _lookup(id_value, id_map):
  if not id_value.value:
    return None

  key = id_value.value
  if key in id_map:
    return id_map[id_value.value]
  error = 'Failed to lookup %s with value: %s' % (
      id_value.__class__.__name__.replace('Id', ''), key)
  print(error)
  print('Check the config contents provided:')
  printer = pprint.PrettyPrinter(indent=4)
  printer.pprint(id_map)
  raise Exception(error)


def _build_touch_file_config(config, project_name):
  partners = {x.id.value: x for x in config.partner_list}
  files = []
  for comp in config.components:
    touch = comp.touchscreen
    # Everything is the same for Touch screen/pad, except different fields
    if comp.HasField('touchpad'):
      touch = comp.touchpad
    if touch.product_id:
      vendor = _lookup(comp.manufacturer_id, partners)
      if not vendor:
        raise Exception('Manufacturer must be set for touch device %s' %
                        comp.id.value)

      product_id = touch.product_id
      fw_version = touch.fw_version

      file_name = '%s_%s.bin' % (product_id, fw_version)
      fw_file_path = os.path.join(TOUCH_PATH, vendor.name, file_name)

      if not os.path.exists(fw_file_path):
        raise Exception("Touchscreen fw bin file doesn't exist at: %s" %
                        fw_file_path)

      touch_vendor = vendor.touch_vendor
      sym_link = touch_vendor.symlink_file_format.format(
          vendor_name=vendor.name,
          vendor_id=touch_vendor.vendor_id,
          product_id=product_id,
          fw_version=fw_version,
          product_series=touch.product_series)

      dest = '%s_%s' % (vendor.name, file_name)
      if touch_vendor.destination_file_format:
        dest = touch_vendor.destination_file_format.format(
            vendor_name=vendor.name,
            vendor_id=touch_vendor.vendor_id,
            product_id=product_id,
            fw_version=fw_version,
            product_series=touch.product_series)

      files.append({
          'destination': os.path.join('/opt/google/touch/firmware', dest),
          'source': os.path.join(project_name, fw_file_path),
          'symlink': os.path.join('/lib/firmware', sym_link),
      })

  result = {}
  _upsert(files, result, 'files')
  return result


def _build_modem(config):
  """Returns the cellular modem configuration, or None if absent."""
  hw_features = config.hw_design_config.hardware_features
  cellular_support = _any_present([hw_features.cellular.present])
  if not cellular_support:
    return None
  if hw_features.cellular.model:
    firmware_variant = hw_features.cellular.model.lower()
  else:
    firmware_variant = config.hw_design.name.lower()
  result = {'firmware-variant': firmware_variant}
  if hw_features.cellular.attach_apn_required:
    result['attach-apn-required'] = True
  return result


def _sw_config(sw_configs, design_config_id):
  """Returns the correct software config for `design_config_id`.

  Returns the correct software config match for `design_config_id`. If no such
  config or multiple such configs are found an exception is raised.
  """
  sw_config_matches = [
      x for x in sw_configs if x.design_config_id.value == design_config_id
  ]
  if len(sw_config_matches) == 1:
    return sw_config_matches[0]
  if len(sw_config_matches) > 1:
    raise ValueError('Multiple software configs found for: %s' %
                     design_config_id)
  raise ValueError('Software config is required for: %s' % design_config_id)


def _is_whitelabel(brand_configs, device_brands):
  for device_brand in device_brands:
    if device_brand.id.value in brand_configs:
      brand_scan_config = brand_configs[device_brand.id.value].scan_config
      if brand_scan_config and brand_scan_config.whitelabel_tag:
        return True
  return False


def _transform_build_configs(config,
                             config_files=ConfigFiles({}, {}, {}, {}, {}, {})):
  # pylint: disable=too-many-locals,too-many-branches
  partners = {x.id.value: x for x in config.partner_list}
  programs = {x.id.value: x for x in config.program_list}
  sw_configs = list(config.software_configs)
  brand_configs = {x.brand_id.value: x for x in config.brand_configs}

  results = {}
  for hw_design in config.design_list:
    if config.device_brand_list:
      device_brands = [
          x for x in config.device_brand_list
          if x.design_id.value == hw_design.id.value
      ]
    else:
      device_brands = [device_brand_pb2.DeviceBrand()]

    whitelabel = _is_whitelabel(brand_configs, device_brands)

    for device_brand in device_brands:
      # Brand config can be empty since platform JSON config allows it
      brand_config = brand_config_pb2.BrandConfig()
      if device_brand.id.value in brand_configs:
        brand_config = brand_configs[device_brand.id.value]

      for hw_design_config in hw_design.configs:
        sw_config = _sw_config(sw_configs, hw_design_config.id.value)
        program = _lookup(hw_design.program_id, programs)
        signer_configs_by_design = {}
        signer_configs_by_brand = {}
        for signer_config in program.device_signer_configs:
          design_id = signer_config.design_id.value
          brand_id = signer_config.brand_id.value
          if design_id:
            signer_configs_by_design[design_id] = signer_config
          elif brand_id:
            signer_configs_by_brand[brand_id] = signer_config
          else:
            raise Exception('No ID found for signer config: %s' % signer_config)

        device_signer_config = None
        if signer_configs_by_design or signer_configs_by_brand:
          design_id = hw_design.id.value
          brand_id = device_brand.id.value
          if design_id in signer_configs_by_design:
            device_signer_config = signer_configs_by_design[design_id]
          elif brand_id in signer_configs_by_brand:
            device_signer_config = signer_configs_by_brand[brand_id]
          else:
            # Assume that if signer configs are set, every config is setup
            raise Exception('Signer config missing for design: %s, brand: %s' %
                            (design_id, brand_id))

        transformed_config = _transform_build_config(
            Config(
                program=program,
                hw_design=hw_design,
                odm=_lookup(hw_design.odm_id, partners),
                hw_design_config=hw_design_config,
                device_brand=device_brand,
                device_signer_config=device_signer_config,
                oem=_lookup(device_brand.oem_id, partners),
                sw_config=sw_config,
                brand_config=brand_config), config_files, whitelabel)

        config_json = json.dumps(
            transformed_config,
            sort_keys=True,
            indent=2,
            separators=(',', ': '))

        if config_json not in results:
          results[config_json] = transformed_config

  return list(results.values())


def _transform_build_config(config, config_files, whitelabel):
  """Transforms Config instance into target platform JSON schema.

  Args:
    config: Config namedtuple
    config_files: Map to look up the generated config files.
    whitelabel: Whether the config is for a whitelabel design

  Returns:
    Unique config payload based on the platform JSON schema.
  """
  result = {
      'identity':
          _build_identity(config.sw_config.id_scan_config, config.program,
                          config.brand_config.scan_config),
      'name':
          config.hw_design.name.lower(),
  }

  _upsert(_build_arc(config, config_files), result, 'arc')
  _upsert(_build_audio(config), result, 'audio')
  _upsert(_build_bluetooth(config), result, 'bluetooth')
  _upsert(_build_wifi(config, config_files), result, 'wifi')
  _upsert(_build_health(config), result, 'cros-healthd')
  _upsert(_build_branding(config), result, 'branding')
  _upsert(config.brand_config.wallpaper, result, 'wallpaper')
  _upsert(config.brand_config.regulatory_label, result, 'regulatory-label')
  _upsert(config.device_brand.brand_code, result, 'brand-code')
  _upsert(
      _build_camera(config.hw_design_config.hardware_topology), result,
      'camera')
  _upsert(_build_firmware(config), result, 'firmware')
  _upsert(_build_fw_signing(config, whitelabel), result, 'firmware-signing')
  _upsert(
      _build_fingerprint(config.hw_design_config.hardware_topology), result,
      'fingerprint')
  _upsert(_build_ui(config), result, 'ui')
  _upsert(_build_power(config), result, 'power')
  if config_files.camera_map:
    camera_file = config_files.camera_map.get(config.hw_design.name, {})
    _upsert(camera_file, result, 'camera')
  if config_files.dptf_map:
    # Prefer design_config level (sku)
    # Then design level
    # If neither, fall back to project wide config (mapped to empty string)
    design_name = config.hw_design.name.lower()
    design_config_id = config.hw_design_config.id.value.lower()
    design_config_id_path = os.path.join(design_name, design_config_id)
    if design_name in design_config_id:
      design_config_id_path = design_config_id.replace(':', '/')
    if config_files.dptf_map.get(design_config_id_path):
      dptf_file = config_files.dptf_map[design_config_id_path]
    elif config_files.dptf_map.get(design_name):
      dptf_file = config_files.dptf_map[design_name]
    else:
      dptf_file = config_files.dptf_map.get('')
    _upsert(dptf_file, result, 'thermal')
  _upsert(config_files.touch_fw, result, 'touch')
  _upsert(
      _build_hardware_properties(config.hw_design_config.hardware_topology),
      result, 'hardware-properties')
  _upsert(_build_modem(config), result, 'modem')
  _upsert(
      _build_keyboard(config.hw_design_config.hardware_topology), result,
      'keyboard')
  _upsert(_build_hps(config.hw_design_config.hardware_topology), result, 'hps')
  _upsert(
      _build_poe(config.hw_design_config.hardware_topology), result,
      'hardware-properties')

  return result


def write_output(configs, output=None):
  """Writes a list of configs to platform JSON format.

  Args:
    configs: List of config dicts defined in cros_config_schema.yaml
    output: Target file output (if None, prints to stdout)
  """
  json_output = json.dumps({'chromeos': {
      'configs': configs,
  }},
                           sort_keys=True,
                           indent=2,
                           separators=(',', ': '))
  if output:
    with open(output, 'w') as output_stream:
      # Using print function adds proper trailing newline.
      print(json_output, file=output_stream)
  else:
    print(json_output)


def _feature(name, present):
  attrib = {'name': name}
  if present:
    return etree.Element('feature', attrib=attrib)

  return etree.Element('unavailable-feature', attrib=attrib)


def _any_present(features):
  return topology_pb2.HardwareFeatures.PRESENT in features


def _get_formatted_config_id(design_config):
  return design_config.id.value.lower().replace(':', '_')


def _write_file(output_dir, file_name, file_content):
  os.makedirs(output_dir, exist_ok=True)
  output = '{}/{}'.format(output_dir, file_name)
  with open(output, 'wb') as f:
    f.write(file_content)


def _get_arc_camera_features(camera):
  """Gets camera related features for ARC hardware_features.xml from camera

  topology. Check
  https://developer.android.com/reference/android/content/pm/PackageManager#FEATURE_CAMERA
  and CTS android.app.cts.SystemFeaturesTest#testCameraFeatures for the correct
  settings.

  Args:
    camera: A HardwareFeatures.Camera proto message.

  Returns:
    list of camera related ARC features as XML elements.
  """
  camera_pb = topology_pb2.HardwareFeatures.Camera

  count = len(camera.devices)
  has_front_camera = any(
      (d.facing == camera_pb.FACING_FRONT for d in camera.devices))
  has_back_camera = any(
      (d.facing == camera_pb.FACING_BACK for d in camera.devices))
  has_autofocus_back_camera = any((d.facing == camera_pb.FACING_BACK and
                                   d.flags & camera_pb.FLAGS_SUPPORT_AUTOFOCUS
                                   for d in camera.devices))
  # Assumes MIPI cameras support FULL-level.
  # TODO(kamesan): Setting this in project configs when there's an exception.
  has_level_full_camera = any(
      (d.interface == camera_pb.INTERFACE_MIPI for d in camera.devices))

  return [
      _feature('android.hardware.camera', has_back_camera),
      _feature('android.hardware.camera.any', count > 0),
      _feature('android.hardware.camera.autofocus', has_autofocus_back_camera),
      _feature('android.hardware.camera.capability.manual_post_processing',
               has_level_full_camera),
      _feature('android.hardware.camera.capability.manual_sensor',
               has_level_full_camera),
      _feature('android.hardware.camera.front', has_front_camera),
      _feature('android.hardware.camera.level.full', has_level_full_camera),
  ]


def _generate_arc_hardware_features(hw_features):
  """Generates ARC hardware_features.xml file content.

  Args:
    hw_features: HardwareFeatures proto message.

  Returns:
    bytes of the hardware_features.xml content.
  """
  touchscreen = _any_present([hw_features.screen.touch_support])
  acc = hw_features.accelerometer
  gyro = hw_features.gyroscope
  compass = hw_features.magnetometer
  light_sensor = hw_features.light_sensor
  root = etree.Element('permissions')
  root.extend(
      _get_arc_camera_features(hw_features.camera) + [
          _feature(
              'android.hardware.sensor.accelerometer',
              _any_present([acc.lid_accelerometer, acc.base_accelerometer])),
          _feature('android.hardware.sensor.gyroscope',
                   _any_present([gyro.lid_gyroscope, gyro.base_gyroscope])),
          _feature(
              'android.hardware.sensor.compass',
              _any_present(
                  [compass.lid_magnetometer, compass.base_magnetometer])),
          _feature(
              'android.hardware.sensor.light',
              _any_present([
                  light_sensor.lid_lightsensor, light_sensor.base_lightsensor
              ])),
          _feature('android.hardware.touchscreen', touchscreen),
          _feature('android.hardware.touchscreen.multitouch', touchscreen),
          _feature('android.hardware.touchscreen.multitouch.distinct',
                   touchscreen),
          _feature('android.hardware.touchscreen.multitouch.jazzhand',
                   touchscreen),
      ])
  return XML_DECLARATION + etree.tostring(root, pretty_print=True)


def _generate_arc_media_profiles(hw_features, sw_config):
  """Generates ARC media_profiles.xml file content.

  Args:
    hw_features: HardwareFeatures proto message.
    sw_config: SoftwareConfig proto message.

  Returns:
    bytes of the media_profiles.xml content, or None if |sw_config| disables the
    generation or there's no camera.
  """

  def _gen_camcorder_profiles(camera_id, resolutions):
    elem = etree.Element(
        'CamcorderProfiles', attrib={'cameraId': str(camera_id)})
    for width, height in resolutions:
      elem.extend([
          _gen_encoder_profile(width, height, False),
          _gen_encoder_profile(width, height, True),
      ])
    elem.extend([
        etree.Element('ImageEncoding', attrib={'quality': '90'}),
        etree.Element('ImageEncoding', attrib={'quality': '80'}),
        etree.Element('ImageEncoding', attrib={'quality': '70'}),
        etree.Element('ImageDecoding', attrib={'memCap': '20000000'}),
    ])
    return elem

  def _gen_encoder_profile(width, height, timelapse):
    elem = etree.Element(
        'EncoderProfile',
        attrib={
            'quality': ('timelapse' if timelapse else '') + str(height) + 'p',
            'fileFormat': 'mp4',
            'duration': '60',
        })
    elem.append(
        etree.Element(
            'Video',
            attrib={
                'codec': 'h264',
                'bitRate': '8000000',
                'width': str(width),
                'height': str(height),
                'frameRate': '30',
            }))
    elem.append(
        etree.Element(
            'Audio',
            attrib={
                'codec': 'aac',
                'bitRate': '96000',
                'sampleRate': '44100',
                'channels': '1',
            }))
    return elem

  def _gen_video_encoder_cap(name, min_bit_rate, max_bit_rate):
    return etree.Element(
        'VideoEncoderCap',
        attrib={
            'name': name,
            'enabled': 'true',
            'minBitRate': str(min_bit_rate),
            'maxBitRate': str(max_bit_rate),
            'minFrameWidth': '320',
            'maxFrameWidth': '1920',
            'minFrameHeight': '240',
            'maxFrameHeight': '1080',
            'minFrameRate': '15',
            'maxFrameRate': '30',
        })

  def _gen_audio_encoder_cap(name, min_bit_rate, max_bit_rate, min_sample_rate,
                             max_sample_rate):
    return etree.Element(
        'AudioEncoderCap',
        attrib={
            'name': name,
            'enabled': 'true',
            'minBitRate': str(min_bit_rate),
            'maxBitRate': str(max_bit_rate),
            'minSampleRate': str(min_sample_rate),
            'maxSampleRate': str(max_sample_rate),
            'minChannels': '1',
            'maxChannels': '1',
        })

  camera_config = sw_config.camera_config
  if not camera_config.generate_media_profiles:
    return None

  camera_pb = topology_pb2.HardwareFeatures.Camera
  root = etree.Element('MediaSettings')
  camera_id = 0
  for facing in [camera_pb.FACING_BACK, camera_pb.FACING_FRONT]:
    camera_device = next(
        (d for d in hw_features.camera.devices if d.facing == facing), None)
    if camera_device is None:
      continue
    if camera_config.camcorder_resolutions:
      resolutions = [
          (r.width, r.height) for r in camera_config.camcorder_resolutions
      ]
    else:
      resolutions = [(1280, 720)]
      if camera_device.flags & camera_pb.FLAGS_SUPPORT_1080P:
        resolutions.append((1920, 1080))
    root.append(_gen_camcorder_profiles(camera_id, resolutions))
    camera_id += 1
  # media_profiles.xml should have at least one CamcorderProfiles.
  if camera_id == 0:
    return None

  root.extend([
      etree.Element('EncoderOutputFileFormat', attrib={'name': '3gp'}),
      etree.Element('EncoderOutputFileFormat', attrib={'name': 'mp4'}),
      _gen_video_encoder_cap('h264', 64000, 17000000),
      _gen_video_encoder_cap('h263', 64000, 1000000),
      _gen_video_encoder_cap('m4v', 64000, 2000000),
      _gen_audio_encoder_cap('aac', 758, 288000, 8000, 48000),
      _gen_audio_encoder_cap('heaac', 8000, 64000, 16000, 48000),
      _gen_audio_encoder_cap('aaceld', 16000, 192000, 16000, 48000),
      _gen_audio_encoder_cap('amrwb', 6600, 23050, 16000, 16000),
      _gen_audio_encoder_cap('amrnb', 5525, 12200, 8000, 8000),
      etree.Element(
          'VideoDecoderCap', attrib={
              'name': 'wmv',
              'enabled': 'false'
          }),
      etree.Element(
          'AudioDecoderCap', attrib={
              'name': 'wma',
              'enabled': 'false'
          }),
  ])

  dtd_path = os.path.dirname(__file__)
  dtd = etree.DTD(os.path.join(dtd_path, 'media_profiles.dtd'))
  if not dtd.validate(root):
    raise etree.DTDValidateError(
        'Invalid media_profiles.xml generated:\n{}'.format(dtd.error_log))

  return XML_DECLARATION + etree.tostring(root, pretty_print=True)


def _write_files_by_design_config(configs, output_dir, build_dir, system_dir,
                                  file_name_template, generate_file_content):
  """Writes generated files for each design config.

  Args:
    configs: Source ConfigBundle to process.
    output_dir: Path to the generated output.
    build_dir: Path to the config file from portage's perspective.
    system_dir: Path to the config file in the target device.
    file_name_template: Template string of the config file name including one
      format()-style replacement field for the config id, e.g. 'config_{}.xml'.
    generate_file_content: Function to generate config file content from
      HardwareFeatures and SoftwareConfig proto.

  Returns:
    dict that maps the formatted config id to the correct file.
  """
  # pylint: disable=too-many-arguments,too-many-locals
  result = {}
  configs_by_design = {}
  for hw_design in configs.design_list:
    for design_config in hw_design.configs:
      sw_config = _sw_config(configs.software_configs, design_config.id.value)
      config_content = generate_file_content(design_config.hardware_features,
                                             sw_config)
      if not config_content:
        continue
      design_name = hw_design.name.lower()

      # Constructs the following map:
      # design_name -> config -> design_configs
      # This allows any of the following file naming schemes:
      # - All configs within a design share config (design_name prefix only)
      # - Nobody shares (full design_name and config id prefix needed)
      #
      # Having shared configs when possible makes code reviews easier around
      # the configs and makes debugging easier on the platform side.
      arc_configs = configs_by_design.get(design_name, {})
      design_configs = arc_configs.get(config_content, [])
      design_configs.append(design_config)
      arc_configs[config_content] = design_configs
      configs_by_design[design_name] = arc_configs

  for design_name, unique_configs in configs_by_design.items():
    for file_content, design_configs in unique_configs.items():
      file_name = file_name_template.format(design_name)
      if len(unique_configs) == 1:
        _write_file(output_dir, file_name, file_content)

      for design_config in design_configs:
        config_id = _get_formatted_config_id(design_config)
        if len(unique_configs) > 1:
          file_name = file_name_template.format(config_id)
          _write_file(output_dir, file_name, file_content)
        result[config_id] = _file_v2('{}/{}'.format(build_dir, file_name),
                                     '{}/{}'.format(system_dir, file_name))
  return result


def _write_arc_hardware_feature_files(configs, output_root_dir, build_root_dir):
  return _write_files_by_design_config(
      configs, output_root_dir + '/arc', build_root_dir + '/arc', '/etc',
      'hardware_features_{}.xml',
      lambda hw_features, _: _generate_arc_hardware_features(hw_features))


def _write_arc_media_profile_files(configs, output_root_dir, build_root_dir):
  return _write_files_by_design_config(configs, output_root_dir + '/arc',
                                       build_root_dir + '/arc', '/etc',
                                       'media_profiles_{}.xml',
                                       _generate_arc_media_profiles)


def _read_config(path):
  """Reads a ConfigBundle proto from a json pb file.

  Args:
    path: Path to the file encoding the json pb proto.
  """
  config = config_bundle_pb2.ConfigBundle()
  with open(path, 'r') as f:
    return json_format.Parse(f.read(), config)


def _merge_configs(configs):
  result = config_bundle_pb2.ConfigBundle()
  for config in configs:
    result.MergeFrom(config)

  return result


def _camera_map(configs, project_name):
  """Produces a camera config map for the given configs.

  Produces a map that maps from the design name to the camera config for that
  design.

  Args:
    configs: Source ConfigBundle to process.
    project_name: Name of project processing for.

  Returns:
    map from design name to camera config.
  """
  result = {}
  for design in configs.design_list:
    design_name = design.name
    config_path = CAMERA_CONFIG_SOURCE_PATH_TEMPLATE.format(design_name.lower())
    if os.path.exists(config_path):
      destination = CAMERA_CONFIG_DEST_PATH_TEMPLATE.format(design_name.lower())
      result[design_name] = {
          'config-file':
              _file_v2(os.path.join(project_name, config_path), destination),
      }
  return result


def _dptf_map(project_name):
  """Produces a dptf map for the given configs.

  Produces a map that maps from design name to the dptf file config for that
  design. It looks for the dptf files at:
      DPTF_PATH + '/' + DPTF_FILE
  for a project wide config, that it maps under the empty string, and at:
      DPTF_PATH + '/' + design_name + '/' + DPTF_FILE
  for design specific configs that it maps under the design name.
  and at:
      DPTF_PATH + '/' + design_name + '/' + design_config_id '/' + DPTF_FILE
  for design config (firmware sku level) specific configs.

  Args:
    project_name: Name of project processing for.

  Returns:
    map from design name or empty string (project wide), to dptf config.
  """
  result = {}
  for file in glob.iglob(
      os.path.join(DPTF_PATH, '**', DPTF_FILE), recursive=True):
    relative_path = os.path.dirname(file).partition(DPTF_PATH)[2].strip('/')
    if relative_path:
      project_dptf_path = os.path.join(project_name, relative_path, DPTF_FILE)
    else:
      project_dptf_path = os.path.join(project_name, DPTF_FILE)
    dptf_file = {
        'dptf-dv':
            project_dptf_path,
        'files': [
            _file(
                os.path.join(project_name, DPTF_PATH, relative_path, DPTF_FILE),
                os.path.join('/etc/dptf', project_dptf_path))
        ]
    }
    result[relative_path] = dptf_file
  return result


def _wifi_sar_map(configs, project_name, output_dir, build_root_dir):
  """Constructs a map from design name to wifi sar config for that design.

  Constructs a map from design name to the wifi sar config for that design.
  In the process a wifi sar hex file is generated that the config points at.
  This mapping is only made for the intel wifi where the generated file is
  provided when building coreboot.

  Args:
    configs: Source ConfigBundle to process.
    project_name: Name of project processing for.
    output_dir: Path to the generated output.
    build_root_dir: Path to the config file from portage's perspective.

  Returns:
    dict that maps the design name onto the wifi config for that design.
  """
  # pylint: disable=too-many-locals
  result = {}
  sw_configs = list(configs.software_configs)
  for hw_design in configs.design_list:
    for hw_design_config in hw_design.configs:
      sw_config = _sw_config(sw_configs, hw_design_config.id.value)
      if sw_config.wifi_config.HasField('intel_config'):
        sar_file_content = _create_intel_sar_file_content(
            sw_config.wifi_config.intel_config)
        design_name = hw_design.name.lower()
        wifi_sar_id = _extract_fw_config_value(
            hw_design_config, hw_design_config.hardware_topology.wifi)
        output_path = os.path.join(output_dir, 'wifi')
        os.makedirs(output_path, exist_ok=True)
        filename = 'wifi_sar_{}.hex'.format(wifi_sar_id)
        output_path = os.path.join(output_path, filename)
        build_path = os.path.join(build_root_dir, 'wifi', filename)
        if os.path.exists(output_path):
          with open(output_path, 'rb') as f:
            if f.read() != sar_file_content:
              raise Exception(
                  'Project {} has conflicting wifi sar file content under '
                  'wifi sar id {}.'.format(project_name, wifi_sar_id))
        else:
          with open(output_path, 'wb') as f:
            f.write(sar_file_content)
        system_path = '/firmware/cbfs-rw-raw/{}/{}'.format(
            design_name, filename)
        result[design_name] = {'sar-file': _file_v2(build_path, system_path)}
  return result


def _extract_fw_config_value(hw_design_config, topology):
  """Extracts the firwmare config value for the given topology.

  Args:
    hw_design_config: Design extracting value from.
    topology: Topology proto to extract the firmware config value for.
  Returns: the extracted value or raises a ValueError if no firmware
    configuration segment with `name` is found.
  """
  mask = topology.hardware_feature.fw_config.mask
  if not mask:
    raise ValueError(
        'No firmware configuration mask found in topology {}'.format(topology))

  fw_config = hw_design_config.hardware_features.fw_config.value
  value = fw_config & mask
  lsb_bit_set = (~mask + 1) & mask
  return value // lsb_bit_set


def hex_8bit(value):
  """Converts 8bit value into bytearray.

  args:
    8bit value

  returns:
    bytearray of size 1
  """

  if value > 0xff or value < 0:
    raise Exception('Sar file 8bit value %s out of range' % value)
  return value.to_bytes(1, 'little')


def hex_16bit(value):
  """Converts 16bit value into bytearray.

  args:
    16bit value

  returns:
    bytearray of size 2
  """

  if value > 0xffff or value < 0:
    raise Exception('Sar file 16bit value %s out of range' % value)
  return value.to_bytes(2, 'little')


def hex_32bit(value):
  """Converts 32bit value into bytearray.

  args:
    32bit value

  returns:
    bytearray of size 4
  """

  if value > 0xffffffff or value < 0:
    raise Exception('Sar file 32bit value %s out of range' % value)
  return value.to_bytes(4, 'little')


def wrds_ewrd_encode(sar_table_config):
  """Creates and returns encoded power tables.

  args:
    sar_table_config: contains power table values configured in config.star

  returns:
    Encoded power tables as bytearray
  """

  def power_table(tpc, revision):
    data = bytearray(0)
    if revision == 0:
      data = (
          hex_8bit(tpc.limit_2g) + hex_8bit(tpc.limit_5g_1) +
          hex_8bit(tpc.limit_5g_2) + hex_8bit(tpc.limit_5g_3) +
          hex_8bit(tpc.limit_5g_4))
    elif revision in (1, 2):
      data = (
          hex_8bit(tpc.limit_2g) + hex_8bit(tpc.limit_5g_1) +
          hex_8bit(tpc.limit_5g_2) + hex_8bit(tpc.limit_5g_3) +
          hex_8bit(tpc.limit_5g_4) + hex_8bit(tpc.limit_5g_5) +
          hex_8bit(tpc.limit_6g_1) + hex_8bit(tpc.limit_6g_2) +
          hex_8bit(tpc.limit_6g_3) + hex_8bit(tpc.limit_6g_4) +
          hex_8bit(tpc.limit_6g_5))
    else:
      raise Exception('ERROR: Invalid power table revision ' % revision)
    return data

  def is_zero_filled(databuffer):
    for byte in databuffer:
      if byte != 0:
        return False
    return True

  sar_table = bytearray(0)
  dsar_table = bytearray(0)
  chain_count = 2
  subbands_count = 0
  dsar_set_count = 1

  if sar_table_config.sar_table_version == 0:
    subbands_count = 5
    sar_table = (
        power_table(sar_table_config.tablet_mode_power_table_a, 0) +
        power_table(sar_table_config.tablet_mode_power_table_b, 0))
    dsar_table = (
        power_table(sar_table_config.non_tablet_mode_power_table_a, 0) +
        power_table(sar_table_config.non_tablet_mode_power_table_b, 0))
  elif sar_table_config.sar_table_version == 1:
    subbands_count = 11
    sar_table = (
        power_table(sar_table_config.tablet_mode_power_table_a, 1) +
        power_table(sar_table_config.tablet_mode_power_table_b, 1))
    dsar_table = (
        power_table(sar_table_config.non_tablet_mode_power_table_a, 1) +
        power_table(sar_table_config.non_tablet_mode_power_table_b, 1))
  elif sar_table_config.sar_table_version == 2:
    subbands_count = 22
    sar_table = (
        power_table(sar_table_config.tablet_mode_power_table_a, 2) +
        power_table(sar_table_config.tablet_mode_power_table_b, 2) +
        power_table(sar_table_config.cdb_tablet_mode_power_table_a, 2) +
        power_table(sar_table_config.cdb_tablet_mode_power_table_b, 2))
    dsar_table = (
        power_table(sar_table_config.non_tablet_mode_power_table_a, 2) +
        power_table(sar_table_config.non_tablet_mode_power_table_b, 2) +
        power_table(sar_table_config.cdb_non_tablet_mode_power_table_a, 2) +
        power_table(sar_table_config.cdb_non_tablet_mode_power_table_b, 2))
  elif sar_table_config.sar_table_version == 0xff:
    return bytearray(0)
  else:
    raise Exception('ERROR: Invalid power table revision ' %
                    sar_table_config.sar_table_version)

  if is_zero_filled(sar_table):
    raise Exception('ERROR: SAR entries are not initialized.')

  if is_zero_filled(dsar_table):
    dsar_set_count = 0
    dsar_table = bytearray(0)

  return (hex_8bit(sar_table_config.sar_table_version) +
          hex_8bit(dsar_set_count) + hex_8bit(chain_count) +
          hex_8bit(subbands_count) + sar_table + dsar_table)


def wgds_encode(wgds_config):
  """Creates and returns encoded geo offset tables.

  args:
    wgds_config: contains offset table values configured in config.star

  returns:
    Encoded geo offset tables as bytearray
  """

  def wgds_offset_table(offsets, revision):
    if revision == 0:
      return (hex_8bit(offsets.max_2g) + hex_8bit(offsets.offset_2g_a) +
              hex_8bit(offsets.offset_2g_b) + hex_8bit(offsets.max_5g) +
              hex_8bit(offsets.offset_5g_a) + hex_8bit(offsets.offset_5g_b))
    if revision in (1, 2):
      return (hex_8bit(offsets.max_2g) + hex_8bit(offsets.offset_2g_a) +
              hex_8bit(offsets.offset_2g_b) + hex_8bit(offsets.max_5g) +
              hex_8bit(offsets.offset_5g_a) + hex_8bit(offsets.offset_5g_b) +
              hex_8bit(offsets.max_6g) + hex_8bit(offsets.offset_6g_a) +
              hex_8bit(offsets.offset_6g_b))
    raise Exception('ERROR: Invalid geo offset table revision ' % revision)

  subbands_count = 0
  offsets_count = 3
  if wgds_config.wgds_version in (0, 1):
    subbands_count = 6
  elif wgds_config.wgds_version in (2, 3):
    subbands_count = 9
  elif wgds_config.wgds_version == 0xff:
    return bytearray(0)
  else:
    raise Exception('ERROR: Invalid geo offset table revision ' %
                    wgds_config.wgds_version)

  return (hex_8bit(wgds_config.wgds_version) + hex_8bit(offsets_count) +
          hex_8bit(subbands_count) +
          wgds_offset_table(wgds_config.offset_fcc, wgds_config.wgds_version) +
          wgds_offset_table(wgds_config.offset_eu, wgds_config.wgds_version) +
          wgds_offset_table(wgds_config.offset_other, wgds_config.wgds_version))


def antgain_encode(ant_gain_config):
  """Creates and returns encoded antenna gain tables.

  args:
    ant_gain_config: contains antenna gain values configured in config.star

  returns:
    Encoded antenna gain tables as bytearray
  """

  def antgain_table(gains, revision):
    if revision == 0:
      return (hex_8bit(gains.ant_gain_2g) + hex_8bit(gains.ant_gain_5g_1) +
              hex_8bit(gains.ant_gain_5g_2) + hex_8bit(gains.ant_gain_5g_3) +
              hex_8bit(gains.ant_gain_5g_4))
    if revision in (1, 2):
      return (hex_8bit(gains.ant_gain_2g) + hex_8bit(gains.ant_gain_5g_1) +
              hex_8bit(gains.ant_gain_5g_2) + hex_8bit(gains.ant_gain_5g_3) +
              hex_8bit(gains.ant_gain_5g_4) + hex_8bit(gains.ant_gain_5g_5) +
              hex_8bit(gains.ant_gain_6g_1) + hex_8bit(gains.ant_gain_6g_2) +
              hex_8bit(gains.ant_gain_6g_3) + hex_8bit(gains.ant_gain_6g_4) +
              hex_8bit(gains.ant_gain_6g_5))
    raise Exception('ERROR: Invalid antenna gain table revision ' % revision)

  chain_count = 2
  bands_count = 0
  if ant_gain_config.ant_table_version == 0:
    bands_count = 5
  elif ant_gain_config.ant_table_version == 1 or ant_gain_config.ant_table_version == 2:
    bands_count = 11
  else:
    return bytearray(0)
  return (hex_8bit(ant_gain_config.ant_table_version) +
          hex_8bit(ant_gain_config.ant_mode_ppag) + hex_8bit(chain_count) +
          hex_8bit(bands_count) +
          antgain_table(ant_gain_config.ant_gain_table_a,
                        ant_gain_config.ant_table_version) +
          antgain_table(ant_gain_config.ant_gain_table_b,
                        ant_gain_config.ant_table_version))


def wtas_encode(wtas_config):
  """Creates and returns encoded time average sar tables.

  args:
    wtas_encode: contains time average sar values configured in config.star

  returns:
    Encoded time average sar tables as bytearray
  """

  if wtas_config.tas_list_size > 16:
    raise Exception('Invalid deny list size ' % wtas_config.tas_list_size)

  if wtas_config.sar_avg_version == 0xffff:
    return bytearray(0)

  if wtas_config.sar_avg_version in (0, 1):
    return (hex_8bit(wtas_config.sar_avg_version) +
            hex_8bit(wtas_config.tas_selection) +
            hex_8bit(wtas_config.tas_list_size) +
            hex_16bit(wtas_config.deny_list_entry_1) +
            hex_16bit(wtas_config.deny_list_entry_2) +
            hex_16bit(wtas_config.deny_list_entry_3) +
            hex_16bit(wtas_config.deny_list_entry_4) +
            hex_16bit(wtas_config.deny_list_entry_5) +
            hex_16bit(wtas_config.deny_list_entry_6) +
            hex_16bit(wtas_config.deny_list_entry_7) +
            hex_16bit(wtas_config.deny_list_entry_8) +
            hex_16bit(wtas_config.deny_list_entry_9) +
            hex_16bit(wtas_config.deny_list_entry_10) +
            hex_16bit(wtas_config.deny_list_entry_11) +
            hex_16bit(wtas_config.deny_list_entry_12) +
            hex_16bit(wtas_config.deny_list_entry_13) +
            hex_16bit(wtas_config.deny_list_entry_14) +
            hex_16bit(wtas_config.deny_list_entry_15) +
            hex_16bit(wtas_config.deny_list_entry_16))

  raise Exception('Invalid time average table revision ' %
                  wtas_config.sar_avg_version)


def dsm_encode(dsm_config):
  """Creates and returns device specific method return values.

  args:
    dsm_config: contains device specific method return values configured in
    config.star

  returns:
    Encoded device specific method return values as bytearray
  """

  def enable_supported_functions(dsm_config):
    supported_functions = 0
    mask = 0x2
    if dsm_config.disable_active_sdr_channels >= 0:
      supported_functions |= mask
    mask = mask << 1
    if dsm_config.support_indonesia_5g_band >= 0:
      supported_functions |= mask
    mask = mask << 1
    if dsm_config.support_ultra_high_band >= 0:
      supported_functions |= mask
    mask = mask << 1
    if dsm_config.regulatory_configurations >= 0:
      supported_functions |= mask
    mask = mask << 1
    if dsm_config.uart_configurations >= 0:
      supported_functions |= mask
    mask = mask << 1
    if dsm_config.enablement_11ax >= 0:
      supported_functions |= mask
    mask = mask << 1
    if dsm_config.unii_4 >= 0:
      supported_functions |= mask
    return supported_functions

  def dsm_value(value):
    if value < 0:
      return hex_32bit(0)
    return value.to_bytes(4, 'little')

  supported_functions = enable_supported_functions(dsm_config)
  if supported_functions == 0:
    return bytearray(0)
  return (dsm_value(supported_functions) +
          dsm_value(dsm_config.disable_active_sdr_channels) +
          dsm_value(dsm_config.support_indonesia_5g_band) +
          dsm_value(dsm_config.support_ultra_high_band) +
          dsm_value(dsm_config.regulatory_configurations) +
          dsm_value(dsm_config.uart_configurations) +
          dsm_value(dsm_config.enablement_11ax) + dsm_value(dsm_config.unii_4))


def _create_intel_sar_file_content(intel_config):
  """creates and returns the intel sar file content for the given config.

  creates and returns the sar file content that is used with intel drivers
  only.

  args:
    intel_config: intelconfig config.

  returns:
    sar file content for the given config, see:
    https://chromeos.google.com/partner/dlm/docs/connectivity/wifidyntxpower.html
  """

  # Encode the SAR data in following format
  #
  # +------------------------------------------------------------+
  # | Field     | Size     | Description                         |
  # +------------------------------------------------------------+
  # | Marker    | 4 bytes  | "$SAR"                              |
  # +------------------------------------------------------------+
  # | Version   | 1 byte   | Current version = 1                 |
  # +------------------------------------------------------------+
  # | SAR table | 2 bytes  | Offset of SAR table from start of   |
  # | offset    |          | the header                          |
  # +------------------------------------------------------------+
  # | WGDS      | 2 bytes  | Offset of WGDS table from start of  |
  # | offset    |          | the header                          |
  # +------------------------------------------------------------+
  # | Ant table | 2 bytes  | Offset of Antenna table from start  |
  # | offset    |          | of the header                       |
  # +------------------------------------------------------------+
  # | DSM offset| 2 bytes  | Offset of DSM from start of the     |
  # |           |          | header                              |
  # +------------------------------------------------------------+
  # | Data      | n bytes  | Data for the different tables       |
  # +------------------------------------------------------------+

  def encode_data(data, header, payload, offset):
    payload += data
    if len(data) > 0:
      header += hex_16bit(offset)
      offset += len(data)
    else:
      header += hex_16bit(0)
    return header, payload, offset

  sar_configs = 5
  marker = '$SAR'.encode()
  header = bytearray(0)
  header += hex_8bit(1)  # hex file version

  payload = bytearray(0)
  offset = len(marker) + len(header) + (sar_configs * 2)

  data = wrds_ewrd_encode(intel_config.sar_table)
  header, payload, offset = encode_data(data, header, payload, offset)

  data = wgds_encode(intel_config.wgds_table)
  header, payload, offset = encode_data(data, header, payload, offset)

  data = antgain_encode(intel_config.ant_table)
  header, payload, offset = encode_data(data, header, payload, offset)

  data = wtas_encode(intel_config.wtas_table)
  header, payload, offset = encode_data(data, header, payload, offset)

  data = dsm_encode(intel_config.dsm)
  header, payload, offset = encode_data(data, header, payload, offset)

  return marker + header + payload


def Main(project_configs, program_config, output):  # pylint: disable=invalid-name
  """Transforms source proto config into platform JSON.

  Args:
    project_configs: List of source project configs to transform.
    program_config: Program config for the given set of projects.
    output: Output file that will be generated by the transform.
  """
  # pylint: disable=too-many-locals
  configs = _merge_configs([_read_config(program_config)] +
                           [_read_config(config) for config in project_configs])
  touch_fw = {}
  camera_map = {}
  dptf_map = {}
  wifi_sar_map = {}
  output_dir = os.path.dirname(output)
  build_root_dir = output_dir
  if 'sw_build_config' in output_dir:
    full_path = os.path.realpath(output)
    project_name = re.match(r'.*/([\w-]*)/(public_)?sw_build_config/.*',
                            full_path).groups(1)[0]
    # Projects don't know about each other until they are integrated into the
    # build system.  When this happens, the files need to be able to co-exist
    # without any collisions.  This prefixes the project name (which is how
    # portage maps in the project), so project files co-exist and can be
    # installed together.
    # This is necessary to allow projects to share files at the program level
    # without having portage file installation collisions.
    build_root_dir = os.path.join(project_name, output_dir)

    camera_map = _camera_map(configs, project_name)
    dptf_map = _dptf_map(project_name)
    wifi_sar_map = _wifi_sar_map(configs, project_name, output_dir,
                                 build_root_dir)

  if os.path.exists(TOUCH_PATH):
    touch_fw = _build_touch_file_config(configs, project_name)
  arc_hw_feature_files = _write_arc_hardware_feature_files(
      configs, output_dir, build_root_dir)
  arc_media_profile_files = _write_arc_media_profile_files(
      configs, output_dir, build_root_dir)
  config_files = ConfigFiles(
      arc_hw_features=arc_hw_feature_files,
      arc_media_profiles=arc_media_profile_files,
      touch_fw=touch_fw,
      dptf_map=dptf_map,
      camera_map=camera_map,
      wifi_sar_map=wifi_sar_map)
  write_output(_transform_build_configs(configs, config_files), output)


def main(argv=None):
  """Main program which parses args and runs

  Args:
    argv: List of command line arguments, if None uses sys.argv.
  """
  if argv is None:
    argv = sys.argv[1:]
  opts = parse_args(argv)
  Main(opts.project_configs, opts.program_config, opts.output)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
