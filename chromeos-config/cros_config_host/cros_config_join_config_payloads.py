#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Join information from config_bundle, model.yaml and HWID.

Takes a generated config_bundle payload, optional public and private model.yaml
 files, and an optional HWID database and merges the data together into a new
set of generated joined data.

Can optionally generate a new ConfigBundle from just the model.yaml and HWID
files.  Simple specify a project name with --project-name/-p and omit
--config-bundle/-c.  At least one of these two options must be specified.
"""

# pylint: disable=too-many-lines

import argparse
import json
import logging
import os
import sys
import tempfile
import yaml

from google.cloud import bigquery

from merge_plugins.merge_hwid import MergeHwid

from common import config_bundle_utils

from checker import io_utils
from chromiumos.build.api import firmware_config_pb2
from chromiumos.config.api import topology_pb2
from chromiumos.config.payload import config_bundle_pb2

# HWID databases use some custom tags, which are mostly legacy as far as I can
# tell, so we'll ignore them explicitly to allow the parser to succeed.
yaml.add_constructor('!re', lambda loader, node: loader.construct_scalar(node))
yaml.add_constructor('!region_field', lambda loader, node: None)
yaml.add_constructor('!region_component', lambda loader, node: None)

# git repo locations
CROS_PLATFORM_REPO = 'https://chromium.googlesource.com/chromiumos/platform2'
CROS_CONFIG_INTERNAL_REPO = 'https://chrome-internal.googlesource.com/chromeos/config-internal'

# DLM/AVL table configurations
DLM_PRODUCTS_TABLE = 'cros-device-lifecycle-manager.prod.products'
DLM_DEVICES_TABLE = 'cros-device-lifecycle-manager.prod.devices'


def load_models(public_path, private_path):
  """Load model.yaml from a public and/or private path."""

  # Have to import this here since we need repos cloned and sys.path set up
  # pylint: disable=import-outside-toplevel, import-error
  from cros_config_host import cros_config_schema
  from libcros_config_host import CrosConfig
  # pylint: enable=import-outside-toplevel, import-error

  if not (public_path or private_path):
    return None

  configs = [config for config in [public_path, private_path] if config]
  with tempfile.TemporaryDirectory() as temp_dir:
    # Convert the model.yaml files into a payload JSON
    config_file = os.path.join(temp_dir, 'config.json')
    cros_config_schema.Main(
        schema=None, config=None, output=config_file, configs=configs)

    # And load the payload json into a CrosConfigJson object
    return CrosConfig(config_file)


def load_hwid(hwid_path):
  """Load a HWID database from the given path."""
  with open(hwid_path) as infile:
    return yaml.load(infile, Loader=yaml.FullLoader)


def non_null_values(items):
  """Unwrap a HWID item block into a dictionary of key => values for non-null values.

  a HWID item block looks like:
    items:
      storage_device:
        status: unsupported
        values:
          class: '0x010101'
          device: '0xff00'
          sectors: '5000000'
          vendor: '0xbeef'
      some_hardware:
         values:
      FAKE_RAM_CHIP:
        values:
          class: '0x010101'
          device: '0xff00'
          sectors: '250000000'
          vendor: '0xabcd'

  We'll iterate over this and break out the 'values' block, make sure it's not
  None, and check whether we should exclude it based on the 'status' field if
  present.
  """

  def _include(val):
    if not val['values']:
      return False

    if 'status' in val and val['status'].lower() == 'unsupported':
      return False
    return True

  return [(key, val['values']) for key, val in items.items() if _include(val)]


def merge_avl_dlm(config_bundle):
  """Merge in additional information from AVL/DLM.

  Args:
    config_bundle: ConfigBundle instance to update

  Returns:
    reference to updated ConfigBundle
  """

  def canonical_name(name):
    """Canonicalize code name for a project."""

    # These rules are from empirical runs with the real project data
    name = name.lower()
    if '_' in name:
      name = name[0:name.find('_')]
    return name

  def merge_form_factor(design, name, device_form_factor):
    """Map from form factor information in DLM to our proto definitions."""
    if not device_form_factor:
      logging.warning("Null form factor for '%s', skipping", design)
      return

    try:
      form_factor_enum = topology_pb2.HardwareFeatures.FormFactor
      form_factor = getattr(form_factor_enum, device_form_factor)

      for design_config in design.configs:
        design_config.hardware_features.form_factor.form_factor = form_factor
    except AttributeError:
      logging.warning(
          "invalid form factor '%s' for '%s'",
          device_form_factor,
          name,
      )

  ##############################################################################
  ## start of function body

  try:
    client = bigquery.Client(project='chromeos-bot')
  except Exception as e:
    logging.warning('Unable to load bigquery client: %s', e)
    return config_bundle

  # canonicalize design names to be compatible with the DLM database
  project_names = [
      canonical_name(design.name) for design in config_bundle.design_list
  ]

  if not project_names:
    logging.info('no designs to populate from DLM, aborting')
    return config_bundle

  # query all projects at once, we'll filter them on our side.
  query = """
    SELECT googleCodeName, deviceFormFactor
      FROM {device_table} devices
      WHERE googleCodeName IN ({projects})
  """.format(
      device_table=DLM_DEVICES_TABLE,
      projects=','.join(["'%s'" % name for name in project_names]),
  )
  logging.info(query)

  # sort through DLM information and update config bundle
  rows = list(client.query(query))
  for design, name in zip(config_bundle.design_list, project_names):
    rows = [row for row in rows if row.get('googleCodeName') == name]

    if len(rows) == 0:
      logging.warning("no results returned for '%s', bad project name?", name)
      continue

    if len(rows) > 1:
      logging.warning(
          "multiple results returned for '%s', cowardly refusing to merge DLM data",
          name,
      )
      continue

    merge_form_factor(design, name, rows[0].get('deviceFormFactor'))

  return config_bundle


def merge_audio_config(sw_config, model):
  """Merge audio configuration from model.yaml into the given sw_config.

  Args:
    sw_config (SoftwareConfig): software config to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  audio_props = model.GetProperties('/audio/main')
  audio_config = sw_config.audio_configs.add()
  audio_config.ucm_suffix = audio_props.get('ucm-suffix', '')


def merge_power_config(sw_config, model):
  """Merge power configuration from model.yaml into the given sw_config.

  Args:
    sw_config (SoftwareConfig): software config to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  power_props = model.GetProperties('/power')
  power_config = sw_config.power_config

  for key, val in power_props.items():
    # We don't support autobrightness yet
    if key == 'autobrightness':
      continue
    power_config.preferences[key] = val


def merge_bluetooth_config(sw_config, model):
  """Merge bluetooth configuration from model.yaml into the given sw_config.

  Args:
    sw_config (SoftwareConfig): software config to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  bt_props = model.GetProperties('/bluetooth')
  bt_config = sw_config.bluetooth_config

  for key, val in bt_props.get('flags', {}).items():
    bt_config.flags[key] = val


def merge_firmware_config(sw_config, model):
  """Merge firmware configuration from model.yaml into the given sw_config.

  Args:
    sw_config (SoftwareConfig): software config to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  fw_props = model.GetProperties('/firmware')

  # Populate firmware config
  fw_config = sw_config.firmware
  fw_config.main_ro_payload.type = firmware_config_pb2.FirmwareType.MAIN
  fw_config.main_ro_payload.firmware_image_name = \
      fw_props.get('main-ro-image', '')

  fw_config.main_rw_payload.type = firmware_config_pb2.FirmwareType.MAIN
  fw_config.main_rw_payload.firmware_image_name = \
      fw_props.get('main-rw-image', '')

  fw_config.ec_ro_payload.type = firmware_config_pb2.FirmwareType.EC
  fw_config.ec_ro_payload.firmware_image_name = \
      fw_props.get('ec-ro-image', '')

  fw_config.pd_ro_payload.type = firmware_config_pb2.FirmwareType.PD
  fw_config.pd_ro_payload.firmware_image_name = \
      fw_props.get('pd-ro-image', '')

  # Populate build config
  build_props = model.GetProperties('/firmware/build-targets')

  build_config = sw_config.firmware_build_config
  build_config.build_targets.bmpblk = build_props.get('bmpblk', '')
  build_config.build_targets.coreboot = build_props.get('coreboot', '')
  build_config.build_targets.depthcharge = build_props.get('depthcharge', '')
  build_config.build_targets.ec = build_props.get('ec', '')
  build_config.build_targets.libpayload = build_props.get('libpayload', '')
  build_config.build_targets.zephyr_ec = build_props.get('zephyr-ec', '')

  for extra in build_props.get('ec-extras', []):
    build_config.build_targets.ec_extras.append(extra)


def merge_camera_config(hw_feat, model):
  """Merge camera config from model.yaml into the given hardware features.

  Args:
    hw_feat (HardwareFeatures): hardware features to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  camera_props = model.GetProperties('/camera')
  del hw_feat, camera_props
  # TODO: Merge camera configuration when it's available


def merge_buttons(hw_feat, model):
  """Merge power/volume button information from model.yaml into hardware features.

  Args:
    hw_feat (HardwareFeatures): hardware features to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  ui_props = model.GetProperties('/ui')
  button = topology_pb2.HardwareFeatures.Button

  if 'power-button' in ui_props:
    edge = ui_props['power-button']['edge']
    hw_feat.power_button.edge = button.Edge.Value(edge.upper())
    hw_feat.power_button.position = float(ui_props['power-button']['position'])

  if 'side-volume-button' in ui_props:
    region = ui_props['side-volume-button']['region']
    hw_feat.volume_button.region = button.Region.Value(region.upper())
    side = ui_props['side-volume-button']['side']
    hw_feat.volume_button.edge = button.Edge.Value(side.upper())


def merge_hardware_props(hw_feat, model):
  """Merge hardware properties from model.yaml into the given hardware features.

  Args:
    hw_feat (HardwareFeatures): hardware features to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  form_factor = topology_pb2.HardwareFeatures.FormFactor
  stylus = topology_pb2.HardwareFeatures.Stylus

  def kw_to_present(config, key):
    if not key in config:
      return topology_pb2.HardwareFeatures.PRESENT_UNKNOWN
    if config[key]:
      return topology_pb2.HardwareFeatures.PRESENT
    return topology_pb2.HardwareFeatures.NOT_PRESENT

  hw_props = model.GetProperties('/hardware-properties')

  hw_feat.accelerometer.base_accelerometer = \
      kw_to_present(hw_props, 'has-base-accelerometer')
  hw_feat.accelerometer.lid_accelerometer = \
      kw_to_present(hw_props, 'has-lid-accelerometer')
  hw_feat.gyroscope.base_gyroscope = \
      kw_to_present(hw_props, 'has-base-gyroscope')
  hw_feat.gyroscope.lid_gyroscope = \
      kw_to_present(hw_props, 'has-lid-gyroscope')
  hw_feat.light_sensor.base_lightsensor = \
      kw_to_present(hw_props, 'has-base-light-sensor')
  hw_feat.light_sensor.lid_lightsensor = \
      kw_to_present(hw_props, 'has-lid-light-sensor')
  hw_feat.magnetometer.base_magnetometer = \
      kw_to_present(hw_props, 'has-base-magnetometer')
  hw_feat.magnetometer.lid_magnetometer = \
      kw_to_present(hw_props, 'has-lid-magnetometer')
  hw_feat.screen.touch_support = \
      kw_to_present(hw_props, 'has-touchscreen')

  hw_feat.form_factor.form_factor = form_factor.FORM_FACTOR_UNKNOWN
  if hw_props.get('is-lid-convertible', False):
    hw_feat.form_factor.form_factor = form_factor.CONVERTIBLE

  stylus_val = hw_props.get('stylus-category', '')
  if not stylus_val:
    hw_feat.stylus.stylus = stylus.STYLUS_UNKNOWN
  if stylus_val == 'none':
    hw_feat.stylus.stylus = stylus.NONE
  if stylus_val == 'internal':
    hw_feat.stylus.stylus = stylus.INTERNAL
  if stylus_val == 'external':
    hw_feat.stylus.stylus = stylus.EXTERNAL


def merge_fingerprint_config(hw_feat, model):
  """Merge fingerprint config from model.yaml into the given hardware features.

  Args:
    hw_feat (HardwareFeatures): hardware features to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    None
  """
  location = topology_pb2.HardwareFeatures.Fingerprint.Location

  fing_prop = model.GetProperties('/fingerprint')
  hw_feat.fingerprint.board = fing_prop.get('board', '')

  sensor_location = fing_prop.get('sensor-location', 'none')
  if sensor_location == 'none':
    sensor_location = 'not-present'
  hw_feat.fingerprint.location = location.Value(sensor_location.upper().replace(
      '-', '_'))


def merge_device_brand(config_bundle, design, model, project_name):
  """Merge brand information from model.yaml into specific Design instance.

  The ConfigBundle and Design protos are updated in place with the information
  from model.yaml.

  In general we'll have a 1:1 mapping with Design to Brand information so we
  create a new DeviceBrand and link it to a new Brand_Config value.

  Args:
    config_bundle (ConfigBundle): top level ConfigBundle to update
    design (Design): design in the config bundle to update
    model (CrosConfig): parsed model.yaml information
    project_name (str): name of the device (eg: phaser)

  Returns:
    A reference to the input ConfigBundle updated with data from model
  """

  # pylint: disable=too-many-locals

  whitelabel = model.GetProperties('/identity/whitelabel-tag')
  whitelabel = whitelabel or ''

  # find/create new brand entry for the design
  brand_name = ''
  brand_code = model.GetProperties('/brand-code')
  brand_id = '{}_{}'.format(project_name, brand_code)

  # find/create device brand
  device_brand = None
  for brand in config_bundle.device_brand_list:
    if (brand.design_id == design.id and brand.brand_name == brand_name and
        brand.brand_code == brand_code):
      device_brand = brand
      break

  if not device_brand:
    device_brand = config_bundle.device_brand_list.add()
    device_brand.id.value = brand_id
    device_brand.design_id.MergeFrom(design.id)
    device_brand.brand_name = ''
    device_brand.brand_code = brand_code

  # find/create brand config
  brand_config = None
  for config in config_bundle.brand_configs:
    if (config.brand_id == device_brand.id and
        config.scan_config.whitelabel_tag == whitelabel):
      brand_config = config
      break

  if not brand_config:
    brand_config = config_bundle.brand_configs.add()
    brand_config.brand_id.MergeFrom(device_brand.id)
    brand_config.scan_config.whitelabel_tag = whitelabel

  wallpaper = model.GetWallpaperFiles()
  if wallpaper:
    brand_config.wallpaper = wallpaper.pop()

  regulatory_label = model.GetProperties('/regulatory-label')
  if regulatory_label:
    brand_config.regulatory_label = regulatory_label

  help_tags = [
      '/ui/help-content-id',
      '/identity/customization-id',
      '/name',
  ]

  for tag in help_tags:
    help_content = model.GetProperties(tag)
    if help_content:
      brand_config.help_content_id = help_content
      break

  return config_bundle


def merge_model(config_bundle, design_config, model):
  """Merge model from model.yaml into a specific Design.Config instance.

  The ConfigBundle, and Design.Config are updated in place with
  model.yaml information.

  Args:
    config_bundle (ConfigBundle): top level ConfigBundle to update
    design_config (Design.Config): design config in the config bundle to update
    model (CrosConfig): parsed model.yaml information

  Returns:
    A reference to the input config_bundle updated with data from model
  """

  identity = model.GetProperties('/identity')

  # Merge hardware configuration
  hw_feat = design_config.hardware_features
  merge_fingerprint_config(hw_feat, model)
  merge_hardware_props(hw_feat, model)
  merge_camera_config(hw_feat, model)
  merge_buttons(hw_feat, model)

  # Merge software configuration
  sw_config = None
  for config in config_bundle.software_configs:
    if config.design_config_id == design_config.id:
      sw_config = config
      break

  # Already have software config for this design_config, so don't re-populate
  if sw_config:
    return config_bundle

  sw_config = config_bundle.software_configs.add()
  sw_config.design_config_id.MergeFrom(design_config.id)

  sw_config.id_scan_config.firmware_sku = identity.get('sku-id', 0xFFFFFFFF)

  if 'smbios-name-match' in identity:
    sw_config.id_scan_config.smbios_name_match = identity['smbios-name-match']

  if 'device-tree-compatible-match' in identity:
    sw_config.id_scan_config.device_tree_compatible_match = \
       identity['device-tree-compatible-match']

  merge_firmware_config(sw_config, model)
  merge_bluetooth_config(sw_config, model)
  merge_power_config(sw_config, model)
  merge_audio_config(sw_config, model)

  return config_bundle


def merge_configs(options):
  # pylint: disable=too-many-locals
  # pylint: disable=too-many-branches
  # pylint: disable=too-many-statements
  """Read and merge configs together, generating new config_bundle output."""

  config_bundle_path = options.config_bundle
  program_name = options.program_name
  project_name = options.project_name
  public_path = options.public_model
  private_path = options.private_model
  hwid_path = options.hwid

  def safe_equal(stra, strb):
    """return True if inputs are equal ignoring case and edge whitespace"""
    return stra.lower().strip() == strb.lower().strip()

  # Set of models to ensure exist in the output.
  ensure_models = set([project_name])

  # generate canonical program ID
  program_id = program_name.lower()

  config_bundle = config_bundle_pb2.ConfigBundle()
  if config_bundle_path:
    # if we're only importing, then save designs to ensure exist
    input_bundle = io_utils.read_config(config_bundle_path)
    if options.import_only:
      for design in input_bundle.design_list:
        if program_name and \
           not safe_equal(design.program_id.value, program_id):
          continue
        ensure_models.add(design.name.lower())

      # Sort to ensure ordering is consistent
      ensure_models = sorted(ensure_models)
      logging.debug('ensuring models: %s', ensure_models)
    else:
      # we're joining payloads so expose full config bundle for merging
      config_bundle = input_bundle

  # ensure that a program entry is added for the manually specified program name
  config_bundle_utils.find_program(config_bundle, program_id, create=True)

  models = load_models(public_path, private_path)

  def find_design(name_program, name_project):
    """Searches config_bundle for a matching design_config.

    Args:
      name_program (str): program name
      name_project (str): project name

    Returns:
      Either found Design for input parameters or new one created and placed
      in the config_bundle.
    """

    # find program
    program = config_bundle_utils.find_program(
        config_bundle,
        name_program.lower(),
    )

    for design in config_bundle.design_list:
      # skip other program designs (shouldn't happen)
      if not safe_equal(program.id.value, design.program_id.value):
        continue

      if safe_equal(name_project, design.name):
        return design

    # no design found, create one
    design = config_bundle.design_list.add()
    design.id.value = name_project.lower()
    design.name = name_project.lower()
    design.program_id.MergeFrom(program.id)
    return design

  def find_design_config(name_program, name_project, sku):
    """Searches config_bundle for a matching design_config.

    Args:
      name_program (str): program name
      name_project (str): project name
      sku (str): specific sku

    Returns:
      Either found Design and Design.Config for input parameters or new ones
      create and placed in the config_bundle.
    """

    design = find_design(name_program, name_project)

    for config in design.configs:
      design_sku = config.id.value.lower().split(':')[-1]
      if safe_equal(design_sku, sku):
        return design, config

    # Create new Design.Config, the board id is encoded according to CBI:
    #   https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/cros_board_info.md
    config = design.configs.add()
    config.id.value = '{}:{}'.format(name_project.lower(), sku)
    return design, config

  ### start of function body

  # GetDeviceConfigs() will return an entry for all combinations of:
  #     (program, project, sku, whitelabel)
  # so we need to be careful not to create duplicate entries.
  for model in models.GetDeviceConfigs() if models else []:
    identity = model.GetProperties('/identity')
    project = model.GetName()
    assert project, 'project name is undefined'

    sku = identity.get('sku-id')
    if not sku:
      # sku not defined, SKUs are by definition < 0x7FFFFFF so we'll use
      # 0x8000000 for the SKU-less case to keep it an integer
      sku = '0x80000000'
      logging.info('found wildcard sku in %s, setting sku-id to "%s"', project,
                   sku)
    sku = str(sku)

    if sku == '255':
      logging.info('skipping unprovisioned sku %s', sku)
      continue

    # ignore other projects
    if not safe_equal(project_name, project):
      continue

    # Lookup design config for this specific device
    design, design_config = find_design_config(program_name, project, sku)

    merge_device_brand(config_bundle, design, model, project_name)
    merge_model(config_bundle, design_config, model)

  # ensure that all our required model names exist.
  for project in ensure_models:
    design = find_design(program_name, project)

  # Merge information from HWID into config bundle
  if hwid_path:
    merger = MergeHwid(hwid_path)
    merger.merge(config_bundle)

    if options.hwid_residual:
      with open(options.hwid_residual, 'w') as outfile:
        json.dump(merger.residual(), outfile, indent=2)

  return config_bundle


def main(options):
  """Runs the script."""

  def clone_repo(repo, path):
    """Clone a given repo to the given path in the file system."""
    cmd = 'git clone -q --depth=1 --shallow-submodules {repo} {path}'.format(
        repo=repo, path=path)
    print('Creating shallow clone of {repo} ({cmd})'.format(repo=repo, cmd=cmd))
    os.system(cmd)

  def clone_or_use_dep(repo, clone_path, dep_path, sub_path=''):
    """Either use a dependency in-place or clone it and use that.

    If the dependency doesn't exist at dep_path, then clone it into clone_path.
    Either way, add the path/{sub_path} to sys.path.

    Args:
      repo (str): url of the git repo for the dependency
      clone_path (str): where to clone repo if neeeded
      dep_path (str): location on disk the path might be located
      sub_path (str): path inside of repo to add to sys.path

    Returns:
      nothing
    """
    root_path = dep_path
    if not os.path.exists(root_path):
      clone_repo(repo, clone_path)
      root_path = clone_path
    sys.path.append(os.path.join(root_path, sub_path))

  if not (options.config_bundle or options.project_name):
    raise RuntimeError(
        'At least one of {config_opt} or {project_opt} must be specified.'
        .format(
            config_opt='--config-bundle/-c', project_opt='--project-name/-p'))

  with tempfile.TemporaryDirectory(prefix='join_proto_') as temppath:
    this_dir = os.path.realpath(os.path.dirname(__file__))

    clone_or_use_dep(
        CROS_PLATFORM_REPO,
        os.path.join(temppath, 'platform2'),
        os.path.realpath(os.path.join(this_dir, '../../platform2')),
        'chromeos-config',
    )

    clone_or_use_dep(
        CROS_CONFIG_INTERNAL_REPO,
        os.path.join(temppath, 'config-internal'),
        os.path.realpath(os.path.join(this_dir, '../../config-internal')),
    )

    io_utils.write_message_json(
        merge_avl_dlm(merge_configs(options)),
        options.output,
    )


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
      '-o',
      '--output',
      type=str,
      required=True,
      help='output file to write joined ConfigBundle jsonproto to')

  parser.add_argument(
      '-p',
      '--project-name',
      type=str,
      required=True,
      help="""When specified without --config-bundle/-c, this species the project name to
generate ConfigBundle information for from the model.yaml/HWID files.  When
specified with --config-bundle/-c, then only projects with this name will be
updated.""")

  parser.add_argument(
      '--program-name',
      type=str,
      required=True,
      help="""Program name to add to the output ConfigBundle.  This program will be
added to the program_list even if there are no designs present.""")

  parser.add_argument(
      '-c',
      '--config-bundle',
      type=str,
      help="""generated config_bundle payload in jsonpb format
(eg: generated/config.jsonproto).  If not specified, an empty ConfigBundle
instance is used instead.""")

  parser.add_argument(
      '--import-only',
      action='store_true',
      help="""When specified, don't use values from --config-bundle directly.  Instead,
only use the config bundle to propagate models to imported payload.""")

  parser.add_argument(
      '--hwid-residual',
      type=str,
      help='when given, write remaining unparsed HWID information to this file',
  )

  parser.add_argument(
      '--public-model', type=str, help='public model.yaml file to merge')
  parser.add_argument(
      '--private-model', type=str, help='private model.yaml file to merge')
  parser.add_argument('--hwid', type=str, help='HWID database to merge')
  parser.add_argument(
      '-v', '--verbose', help='increase output verbosity', action='store_true')
  parser.add_argument('-l', '--log', type=str, help='set logging level')

  args = parser.parse_args()
  # pylint: disable=invalid-name
  loglevel = logging.INFO if args.verbose else logging.WARNING
  if args.log:
    loglevel = {
        'critical': logging.CRITICAL,
        'error': logging.ERROR,
        'warning': logging.WARNING,
        'info': logging.INFO,
        'debug': logging.DEBUG,
    }.get(args.log.lower())

    if not loglevel:
      logging.error("invalid value for -l/--log '%s'", args.log)

  logging.basicConfig(level=loglevel)
  main(args)
