# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Base impl of the Chrome OS Configuration access library."""

from __future__ import print_function

from collections import namedtuple, OrderedDict
import os

# Represents a single touch firmware file which needs to be installed:
#   source: source filename of firmware file. This is installed in a
#       directory in the root filesystem
#   dest: destination filename of firmware file in the root filesystem. This is
#       in /opt/google/touch/firmware
#   symlink: name of symbolic link to put in LIB_FIRMWARE to point to the touch
#       firmware. This is where Linux finds the firmware at runtime.
TouchFile = namedtuple('TouchFile', ['source', 'dest', 'symlink'])

# Represents a single file which needs to be installed:
#   source: Source filename within ${FILESDIR}
#   dest: Destination filename in the root filesystem
BaseFile = namedtuple('BaseFile', ['source', 'dest'])

# Represents information needed to create firmware for a model:
#   model: Name of model (e.g 'reef'). Also used as the signature ID for signing
#   shared_model: Name of model containing the shared firmware used by this
#       model, or None if this model has its own firmware images
#   key_id: Key ID used to sign firmware for this model (e.g. 'REEF')
#   have_image: True if we need to generate a setvars.sh file for this model.
#       If this is False it indicates that the model will never be detected at
#       run-time since it is a zero-touch whitelabel model. The signature ID
#       will be obtained from the customization_id in VPD when needed. Signing
#       instructions should still be generated for this model.
#   bios_build_target: Build target to use to build the BIOS, or None if none
#   ec_build_target: Build target to use to build the EC, or None if none
#   main_image_uri: URI to use to obtain main firmware image (e.g.
#       'bcs://Caroline.2017.21.1.tbz2')
#   ec_image_uri: URI to use to obtain the EC (Embedded Controller) firmware
#       image
#   pd_image_uri: URI to use to obtain the PD (Power Delivery controller)
#       firmware image
#   extra: List of extra files to include in the firmware update, each a string
#   create_bios_rw_image: True to create a RW BIOS image
#   tools: List of tools to include in the firmware update
#   sig_id: Signature ID to put in the setvars.sh file. This is normally the
#       same as the model, since that is what we use for signature ID. But for
#       zero-touch whitelabel this is 'sig-id-in-customization-id' since we do
#       not know the signature ID until we look up in VPD.
FirmwareInfo = namedtuple('FirmwareInfo', [
    'model', 'shared_model', 'key_id', 'have_image', 'bios_build_target',
    'ec_build_target', 'main_image_uri', 'main_rw_image_uri', 'ec_image_uri',
    'pd_image_uri', 'extra', 'create_bios_rw_image', 'tools', 'sig_id'
])

class PathComponent(object):
  """A component in a directory/file tree

  Properties:
    name: Name this component
    children: Dict of children:
      key: Name of child
      value: PathComponent object for child
  """

  def __init__(self, name):
    self.name = name
    self.children = dict()

  def AddPath(self, path):
    parts = path.split('/', 1)
    part = parts[0]
    rest = parts[1] if len(parts) > 1 else ''
    child = self.children.get(part)
    if not child:
      child = PathComponent(part)
      self.children[part] = child
    if rest:
      child.AddPath(rest)

  def ShowTree(self, base_path, path='', indent=0):
    """Show a tree of file paths

    This shows a component and all its children. Nodes can either be directories
    or files. Each file is shown with its size, or 'missing' if not found.

    Args:
      base_path: Base path where the actual files can be found
      path: Path of this component relative to the root (e.g. 'etc/cras/)
      indent: Indent level we are up to (0 = first)
    """
    path = os.path.join(path, self.name)
    fname = os.path.join(base_path, path)
    if os.path.isdir(fname):
      status = ''
    elif os.path.exists(fname):
      status = os.stat(fname).st_size
    else:
      status = 'missing'
    print('%-10s%s%s%s' % (status, '   ' * indent, str(self.name),
                           self.children and '/' or ''))
    for child in sorted(self.children.keys()):
      self.children[child].ShowTree(base_path, path, indent + 1)


class DeviceConfig(object):
  """Configuration for a unique Device/SKU/Product combination.

  Provides an abstraction layer between DTS/JSON for accessing config for a
  unique Device/SKU/Product instance.
  """

  def GetName(self):
    """Returns the name of the config.

    Returns:
      Name of he config
    """
    pass

  def GetProperties(self, path):
    """Returns a map of properties at the given config path.

    Args:
      path: Path to the config desired.

    Returns:
      A map of properties at the given config path.
    """
    pass

  def GetProperty(self, path, name):
    """Returns the name value at a given path.

    Args:
      path: Path to the config desired.
      name: Property desired.

    Returns:
      Requested value or empty string if not present.
    """
    pass

  def GetFirmwareConfig(self):
    """Returns a map hierarchy of the firmware config."""
    pass

  def GetFirmwareUris(self):
    """Returns a list of (string) firmware URIs.

    Generates and returns a list of firmeware URIs for this device. These URIs
    can be used to pull down remote firmware packages.

    Returns:
      A list of (string) full firmware URIs, or an empty list on failure.
    """
    firmware = self.GetFirmwareConfig()
    if not firmware:
      return []

    if 'bcs-overlay' not in firmware:
      return []
    # Strip "overlay-" from bcs_overlay
    bcs_overlay = firmware['bcs-overlay'][8:]
    ebuild_name = bcs_overlay.split('-')[0]
    valid_images = [p for n, p in firmware.iteritems()
                    if n.endswith('-image') and p.startswith('bcs://')]
    # Strip "bcs://" from bcs_from images (to get the file names only)
    file_names = [p[6:] for p in valid_images]
    uri_format = ('gs://chromeos-binaries/HOME/bcs-{bcs}/overlay-{bcs}/'
                  'chromeos-base/chromeos-firmware-{ebuild_name}/{fname}')
    uris = [uri_format.format(
        bcs=bcs_overlay,
        model=self.GetName(),
        fname=fname,
        ebuild_name=ebuild_name) for fname in file_names]
    return sorted(uris)

  def GetTouchFirmwareFiles(self):
    """Get a list of unique touch firmware files

    Returns:
      List of TouchFile objects representing the touch firmware referenced
        by this model
    """
    pass

  def GetArcFiles(self):
    """Get a list of arc++ files for this device

    Returns:
      List of BaseFile objects representing the arc++ files needed.
    """
    pass

  def GetAudioFiles(self):
    """Get a list of audio files

    Returns:
      List of BaseFile objects representing the audio files referenced
      by this device.
    """
    pass

  def GetThermalFiles(self):
    """Get a list of thermal files

    Returns:
      List of BaseFile objects representing the thermal files referenced
      by this device.
    """
    pass

  def GetFirmwareInfo(self):
    """Gets the FirmewareInfo instance for a given device.

    Returns:
      Returns the FirmwareInfo instance.
    """
    pass

  def GetWallpaperFiles(self):
    """Get a set of wallpaper files used for this model"""
    pass


class CrosConfigBaseImpl(object):
  """The ChromeOS Configuration API for the host."""

  def GetConfig(self, name):
    """Gets a (DeviceConfig) instance by name.

    Returns:
      (DeviceConfig) instance if found, else None
    """
    for device in self.GetDeviceConfigs():
      if device.GetName() == name:
        return device
    return None

  def GetDeviceConfigs(self):
    """Returns a list of (DeviceConfig) instances.

    Returns:
      A list of (DeviceConfig) instances.
    """
    pass

  def GetFullConfig(self):
    """Returns a full dict of every config returned from every API.

    Returns:
      Dictionary that maps method call onto return config.
    """
    result = {}
    result['GetFirmwareUris'] = self.GetFirmwareUris()
    result['GetTouchFirmwareFiles'] = self.GetTouchFirmwareFiles()
    result['GetArcFiles'] = self.GetArcFiles()
    result['GetAudioFiles'] = self.GetAudioFiles()
    result['GetThermalFiles'] = self.GetThermalFiles()
    result['GetFirmwareInfo'] = self.GetFirmwareInfo()
    result['GetWallpaperFiles'] = self.GetWallpaperFiles()
    return result


  def GetFirmwareUris(self):
    """Returns a list of (string) firmware URIs.

    Generates and returns a list of firmeware URIs for all device. These URIs
    can be used to pull down remote firmware packages.

    Returns:
      A list of (string) full firmware URIs, or an empty list on failure.
    """
    uris = set()
    for device in self.GetDeviceConfigs():
      uris.update(set(device.GetFirmwareUris()))
    return sorted(list(uris))

  def GetTouchFirmwareFiles(self):
    """Get a list of unique touch firmware files for all devices

    These files may come from ${FILESDIR} or from a tar file in BCS.

    Returns:
      List of TouchFile objects representing all the touch firmware referenced
      by all devices
    """
    file_set = set()
    for device in self.GetDeviceConfigs():
      for files in device.GetTouchFirmwareFiles():
        file_set.add(files)

    return sorted(file_set, key=lambda files: files.source)

  def GetBcsUri(self, overlay, path):
    """Form a valid BCS URI for downloading files.

    Args:
      overlay: Name of overlay (e.g. 'reef-private')
      path: Path to file in overlay (e.g. 'chromeos-base/'
        'chromeos-touch-firmware-reef/chromeos-touch-firmware-reef-1.0-r9.tbz2')

    Returns:
      Valid BCS URI to download from
    """
    if not overlay.startswith('overlay'):
      return None
    # Strip "overlay-" from bcs_overlay.
    bcs_overlay = overlay[8:]
    return (
        'gs://chromeos-binaries/HOME/bcs-%(bcs)s/overlay-%(bcs)s/%(path)s' % {
            'bcs': bcs_overlay,
            'path': path
        })

  def GetArcFiles(self):
    """Get a list of unique Arc++ files for all devices

    Returns:
      List of BaseFile objects representing all the arc++ files referenced
      by all devices
    """
    file_set = set()
    for device in self.GetDeviceConfigs():
      for files in device.GetArcFiles():
        file_set.add(files)

    return sorted(file_set, key=lambda files: files.source)

  def GetAudioFiles(self):
    """Get a list of unique audio files for all models

    Returns:
      List of BaseFile objects representing all the audio files referenced
      by all models
    """
    file_set = set()
    for device in self.GetDeviceConfigs():
      for files in device.GetAudioFiles():
        file_set.add(files)

    return sorted(file_set, key=lambda files: files.source)

  def GetFirmwareBuildTargets(self, target_type):
    """Returns a list of all firmware build-targets of the given target type.

    Args:
      target_type: A string type for the build-targets to return

    Returns:
      A list of all build-targets of the given type, for all models.
    """
    firmware_filter = os.getenv('FW_NAME')
    build_targets = []
    for device in self.GetDeviceConfigs():
      device_targets = device.GetProperties('/firmware/build-targets')
      # Skip nodes with no build targets
      if not device_targets:
        continue

      # TODO(teravest): Add a name field and use here instead of coreboot.
      key = device_targets['coreboot']
      if firmware_filter and key != firmware_filter:
        continue
      if target_type in device_targets:
        build_targets.append(device_targets[target_type])
      if target_type == 'ec':
        for ec_extra in ('base', 'cr50'):
          if ec_extra in device_targets:
            build_targets.append(device_targets[ec_extra])
    return sorted(set(build_targets))

  def GetFirmwareBuildCombinations(self, components):
    """Get named firmware build combinations for all devices.

    Args:
      components: List of firmware components to get target combinations for.

    Returns:
      OrderedDict containing firmware combinations
        key: combination name
        value: list of firmware targets for specified types

    Raises:
      ValueError if a collision is encountered for named combinations.
    """
    firmware_filter = os.getenv('FW_NAME')

    combos = OrderedDict()
    for device in self.GetDeviceConfigs():
      device_targets = device.GetProperties('/firmware/build-targets')
      # Skip device_targetss with no build targets
      if not device_targets:
        continue
      targets = [device_targets[c] for c in components]

      # Always name firmware combinations after the 'coreboot' name.
      # TODO(teravest): Add a 'name' field.
      key = device_targets['coreboot']
      if firmware_filter and key != firmware_filter:
        continue

      if key in combos and targets != combos[key]:
        raise ValueError('Colliding firmware combinations found for key %s: '
                         '%s, %s' % (key, targets, combos[key]))
      combos[key] = targets
    return OrderedDict(sorted(combos.iteritems()))

  def GetThermalFiles(self):
    """Get a list of unique thermal files for all models

    Returns:
      List of BaseFile objects representing all the audio files referenced
      by all devices
    """
    file_set = set()
    for device in self.GetDeviceConfigs():
      for files in device.GetThermalFiles():
        file_set.add(files)

    return sorted(file_set, key=lambda files: files.source)

  def ShowTree(self, base_path, tree):
    print('%-10s%s' % ('Size', 'Path'))
    tree.ShowTree(base_path)

  def GetFileTree(self):
    """Get a tree of all files installed by the config

    This looks at all available config that installs files in the root and
    returns them as a tree structure. This can be passed to ShowTree(), which
    is the only feature currently implemented which uses this tree.

    Returns:
        PathComponent object containing the root component
    """
    paths = set()
    for item in self.GetAudioFiles():
      paths.add(item.dest)
    for item in self.GetTouchFirmwareFiles():
      paths.add(item.dest)
      paths.add(item.symlink)
    root = PathComponent('')
    for path in paths:
      root.AddPath(path[1:])

    return root

  def GetModelList(self):
    """Return a list of models

    Returns:
      List of model names, each a string
    """
    return sorted(set([device.GetName() for device in self.GetDeviceConfigs()]))

  def GetFirmwareScript(self):
    """Obtain the packer script to use. Always updater4.sh

    Returns:
      Filename of packer script to use (e.g. 'updater4.sh')
    """
    return 'updater4.sh'

  def GetFirmwareInfo(self):
    firmware_info = OrderedDict()
    for name in self.GetModelList():
      for device in self.GetDeviceConfigs():
        if device.GetName() == name:
          firmware_info.update(device.GetFirmwareInfo())
    return firmware_info

  def GetWallpaperFiles(self):
    """Get a list of wallpaper files used for all models"""
    wallpapers = set()
    for device in self.GetDeviceConfigs():
      wallpapers |= device.GetWallpaperFiles()
    return sorted(wallpapers)
