#!/usr/bin/env python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The unit test suite for the libcros_config_host.py library"""

from __future__ import print_function

from collections import OrderedDict
from contextlib import contextmanager
from io import BytesIO
import copy
import json
import os
import sys
import unittest

from libcros_config_host import CrosConfig
from libcros_config_host_base import BaseFile, TouchFile, FirmwareInfo


YAML_FILE = '../libcros_config/test.yaml'
MODELS = sorted(['some', 'another', 'whitelabel'])
ANOTHER_BUCKET = ('gs://chromeos-binaries/HOME/bcs-another-private/overlay-'
                  'another-private/chromeos-base/chromeos-firmware-another/')
SOME_BUCKET = ('gs://chromeos-binaries/HOME/bcs-some-private/'
               'overlay-some-private/chromeos-base/chromeos-firmware-some/')
SOME_FIRMWARE_FILES = ['Some_EC.1111.11.1.tbz2',
                       'Some.1111.11.1.tbz2',
                       'Some_RW.1111.11.1.tbz2']
ANOTHER_FIRMWARE_FILES = ['Another_EC.1111.11.1.tbz2',
                          'Another.1111.11.1.tbz2',
                          'Another_RW.1111.11.1.tbz2']

LIB_FIRMWARE = '/lib/firmware/'
TOUCH_FIRMWARE = '/opt/google/touch/firmware/'


# Use this to suppress stdout/stderr output:
# with capture_sys_output() as (stdout, stderr)
#   ...do something...
@contextmanager
def capture_sys_output():
  capture_out, capture_err = BytesIO(), BytesIO()
  old_out, old_err = sys.stdout, sys.stderr
  try:
    sys.stdout, sys.stderr = capture_out, capture_err
    yield capture_out, capture_err
  finally:
    sys.stdout, sys.stderr = old_out, old_err

def _FormatNamedTuplesDict(value):
   result = copy.deepcopy(value)
   for key, value in result.iteritems():
     result[key] = value._asdict()

   return json.dumps(result, indent=2)


class CrosConfigHostTest(unittest.TestCase):
  def setUp(self):
    self.filepath = os.path.join(os.path.dirname(__file__), YAML_FILE)

  def _assertEqualsNamedTuplesDict(self, expected, result):
    self.assertEqual(
        _FormatNamedTuplesDict(expected), _FormatNamedTuplesDict(result))

  def testGetProperty(self):
    config = CrosConfig(self.filepath)
    another = config.GetConfig('another')
    self.assertEqual(another.GetProperty('/', 'wallpaper'), 'default')
    self.assertFalse(another.GetProperty('/', 'missing'))

  def testModels(self):
    config = CrosConfig(self.filepath)
    models = config.GetModelList()
    for model in MODELS:
      self.assertIn(model, models)

  def testGetFirmwareUris(self):
    config = CrosConfig(self.filepath)
    firmware_uris = config.GetConfig('another').GetFirmwareUris()
    self.assertSequenceEqual(
        firmware_uris,
        sorted([ANOTHER_BUCKET + fname for fname in ANOTHER_FIRMWARE_FILES]))

  def testGetSharedFirmwareUris(self):
    config = CrosConfig(self.filepath)
    firmware_uris = config.GetFirmwareUris()
    expected = sorted(
        [ANOTHER_BUCKET + fname for fname in ANOTHER_FIRMWARE_FILES] +
        [SOME_BUCKET + fname for fname in SOME_FIRMWARE_FILES])
    self.assertSequenceEqual(firmware_uris, expected)

  def testGetArcFiles(self):
    config = CrosConfig(self.filepath)
    arc_files = config.GetArcFiles()
    self.assertEqual(
        arc_files,
        [BaseFile(
            source='some/hardware_features',
            dest='/usr/share/chromeos-config/sbin/some/hardware_features')])

  def testGetBluetoothFiles(self):
    config = CrosConfig(self.filepath)
    bluetooth_files = config.GetBluetoothFiles()
    self.assertEqual(
        bluetooth_files,
        [BaseFile(
            source='some/main.conf',
            dest='/etc/bluetooth/some/main.conf')])

  def testGetThermalFiles(self):
    config = CrosConfig(self.filepath)
    thermal_files = config.GetThermalFiles()
    self.assertEqual(
        thermal_files,
        [BaseFile('another/dptf.dv', '/etc/dptf/another/dptf.dv'),
         BaseFile('some_notouch/dptf.dv', '/etc/dptf/some_notouch/dptf.dv'),
         BaseFile('some_touch/dptf.dv', '/etc/dptf/some_touch/dptf.dv')])

  def testGetFirmwareBuildTargets(self):
    config = CrosConfig(self.filepath)
    self.assertSequenceEqual(config.GetFirmwareBuildTargets('coreboot'),
                             ['another', 'some'])
    os.environ['FW_NAME'] = 'another'
    self.assertSequenceEqual(config.GetFirmwareBuildTargets('coreboot'),
                             ['another'])
    self.assertSequenceEqual(config.GetFirmwareBuildTargets('ec'),
                             ['another', 'another_base', 'another_cr50',
                              'extra1', 'extra2'])
    del os.environ['FW_NAME']

  def testFileTree(self):
    """Test that we can obtain a file tree"""
    config = CrosConfig(self.filepath)
    node = config.GetFileTree()
    self.assertEqual(node.name, '')
    self.assertEqual(sorted(node.children.keys()),
                     ['etc', 'lib', 'opt', 'usr'])
    etc = node.children['etc']
    self.assertEqual(etc.name, 'etc')
    cras = etc.children['cras']
    self.assertEqual(cras.name, 'cras')
    another = cras.children['another']
    self.assertEqual(sorted(another.children.keys()),
                     ['a-card', 'dsp.ini'])

  def testShowTree(self):
    """Test that we can show a file tree"""
    config = CrosConfig(self.filepath)
    tree = config.GetFileTree()
    with capture_sys_output() as (stdout, stderr):
      config.ShowTree('/', tree)
    self.assertEqual(stderr.getvalue(), '')
    lines = [line.strip() for line in stdout.getvalue().splitlines()]
    self.assertEqual(lines[0].split(), ['Size', 'Path'])
    self.assertEqual(lines[1], '/')
    self.assertEqual(lines[2], 'etc/')
    self.assertEqual(lines[3].split(), ['missing', 'cras/'])


  def testFirmwareBuildCombinations(self):
    """Test generating a dict of firmware build combinations."""
    config = CrosConfig(self.filepath)
    expected = OrderedDict(
        [('another', ['another', 'another']),
         ('some', ['some', 'some'])])
    result = config.GetFirmwareBuildCombinations(['coreboot', 'depthcharge'])
    self.assertEqual(result, expected)

    # Should not explode when devices do not specify requested target.
    expected = OrderedDict(
        [('another', ['another_base']),
         ('some', [])])
    result = config.GetFirmwareBuildCombinations(['base'])
    self.assertEqual(result, expected)

    os.environ['FW_NAME'] = 'another'
    expected = OrderedDict([('another', ['another', 'another'])])
    result = config.GetFirmwareBuildCombinations(['coreboot', 'depthcharge'])
    self.assertEqual(result, expected)
    del os.environ['FW_NAME']

  def testGetWallpaper(self):
    """Test that we can access the wallpaper information"""
    config = CrosConfig(self.filepath)
    wallpaper = config.GetWallpaperFiles()
    self.assertEquals(['default',
                       'some',
                       'wallpaper-wl1',
                       'wallpaper-wl2'],
                      wallpaper)

  def testGetTouchFirmwareFiles(self):
    def _GetFile(source, symlink):
      """Helper to return a suitable TouchFile"""
      return TouchFile(source, TOUCH_FIRMWARE + source,
                       LIB_FIRMWARE + symlink)

    config = CrosConfig(self.filepath)
    touch_files = config.GetConfig('another').GetTouchFirmwareFiles()
    # pylint: disable=line-too-long
    self.assertEqual(
        touch_files,
        [TouchFile(source='some_stylus_vendor/another-version.hex',
                   dest='/opt/google/touch/firmware/some_stylus_vendor/another-version.hex',
                   symlink='/lib/firmware/some_stylus_vendor_firmware_ANOTHER.bin'),
         TouchFile(source='some_touch_vendor/some-pid_some-version.bin',
                   dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                   symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin')])
    touch_files = config.GetConfig('some').GetTouchFirmwareFiles()

    # This checks that duplicate processing works correct, since both models
    # have the same wacom firmware
    self.assertEqual(
        touch_files,
        [TouchFile(source='some_stylus_vendor/some-version.hex',
                   dest='/opt/google/touch/firmware/some_stylus_vendor/some-version.hex',
                   symlink='/lib/firmware/some_stylus_vendor_firmware_SOME.bin'),
         TouchFile(source='some_touch_vendor/some-pid_some-version.bin',
                   dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                   symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin'),
         TouchFile(source='some_touch_vendor/some-other-pid_some-other-version.bin',
                   dest='/opt/google/touch/firmware/some_touch_vendor/some-other-pid_some-other-version.bin',
                   symlink='/lib/firmware/some_touch_vendorts_i2c_some-other-pid.bin')])
    touch_files = config.GetTouchFirmwareFiles()
    expected = set(
        [TouchFile(source='some_stylus_vendor/another-version.hex',
                   dest='/opt/google/touch/firmware/some_stylus_vendor/another-version.hex',
                   symlink='/lib/firmware/some_stylus_vendor_firmware_ANOTHER.bin'),
         TouchFile(source='some_stylus_vendor/some-version.hex',
                   dest='/opt/google/touch/firmware/some_stylus_vendor/some-version.hex',
                   symlink='/lib/firmware/some_stylus_vendor_firmware_SOME.bin'),
         TouchFile(source='some_touch_vendor/some-pid_some-version.bin',
                   dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                   symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin'),
         TouchFile(source='some_touch_vendor/some-other-pid_some-other-version.bin',
                   dest='/opt/google/touch/firmware/some_touch_vendor/some-other-pid_some-other-version.bin',
                   symlink='/lib/firmware/some_touch_vendorts_i2c_some-other-pid.bin'),
         TouchFile(source='some_touch_vendor/some-pid_some-version.bin',
                   dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                   symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin')])
    self.assertEqual(set(touch_files), expected)

  def testGetAudioFiles(self):
    config = CrosConfig(self.filepath)
    audio_files = config.GetAudioFiles()
    expected = \
      [BaseFile(source='cras-config/another/dsp.ini',
                dest='/etc/cras/another/dsp.ini'),
       BaseFile(source='cras-config/another/a-card',
                dest='/etc/cras/another/a-card'),
       BaseFile(source='cras-config/some/dsp.ini',
                dest='/etc/cras/some/dsp.ini'),
       BaseFile(source='cras-config/some/a-card',
                dest='/etc/cras/some/a-card'),
       BaseFile(source='topology/another-tplg.bin',
                dest='/lib/firmware/another-tplg.bin'),
       BaseFile(source='topology/some-tplg.bin',
                dest='/lib/firmware/some-tplg.bin'),
       BaseFile(source='ucm-config/a-card.another/HiFi.conf',
                dest='/usr/share/alsa/ucm/a-card.another/HiFi.conf'),
       BaseFile(source='ucm-config/a-card.another/a-card.another.conf',
                dest='/usr/share/alsa/ucm/a-card.another/a-card.another.conf'),
       BaseFile(source='ucm-config/a-card.some/HiFi.conf',
                dest='/usr/share/alsa/ucm/a-card.some/HiFi.conf'),
       BaseFile(source='ucm-config/a-card.some/a-card.some.conf',
                dest='/usr/share/alsa/ucm/a-card.some/a-card.some.conf')]

    self.assertEqual(audio_files, sorted(expected))

  def testFirmware(self):
    """Test access to firmware information"""
    expected = OrderedDict(
        [('another',
          FirmwareInfo(model='another',
                       shared_model='another',
                       key_id='ANOTHER',
                       have_image=True,
                       bios_build_target='another',
                       ec_build_target='another',
                       main_image_uri='bcs://Another.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Another_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Another_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='another',
                       brand_code='')),
         ('some',
          FirmwareInfo(model='some',
                       shared_model='some',
                       key_id='SOME',
                       have_image=True,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='some',
                       brand_code='')),
         ('whitelabel',
          FirmwareInfo(model='whitelabel',
                       shared_model='some',
                       key_id='WHITELABEL1',
                       have_image=True,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='sig-id-in-customization-id',
                       brand_code='')),
         ('whitelabel-whitelabel1',
          FirmwareInfo(model='whitelabel-whitelabel1',
                       shared_model='some',
                       key_id='WHITELABEL1',
                       have_image=False,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='whitelabel-whitelabel1',
                       brand_code='WLBA')),
         ('whitelabel-whitelabel2',
          FirmwareInfo(model='whitelabel-whitelabel2',
                       shared_model='some',
                       key_id='WHITELABEL2',
                       have_image=False,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='whitelabel-whitelabel2',
                       brand_code='WLBB'))])
    result = CrosConfig(self.filepath).GetFirmwareInfo()
    self._assertEqualsNamedTuplesDict(result, expected)


if __name__ == '__main__':
  unittest.main()
