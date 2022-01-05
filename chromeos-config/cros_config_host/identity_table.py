# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import enum
import struct


STRUCT_VERSION = 2
# version, entry_count
HEADER_FORMAT = '<LL'
# flags, smbios name match, fdt compatible match, sku match,
# whitelabel match, firmware manifest name
ENTRY_FORMAT = '<LLLLLL'


class EntryFlags(enum.Enum):
  """The flags used at the beginning of each entry."""
  HAS_SKU_ID = 1 << 0
  HAS_WHITELABEL = 1 << 1

  # This device uses a customization ID from VPD to match instead of a
  # whitelabel tag. This is deprecated for new devices since 2017, so
  # it should only be set for old pre-unibuild migrations.
  HAS_CUSTOMIZATION_ID = 1 << 2

  # This config requires at least one FDT compatible string to match
  # the given string.
  HAS_FDT_COMPATIBLE = 1 << 3

  # For x86 only: this device has an SMBIOS name to match.
  HAS_SMBIOS_NAME = 1 << 4


def WriteIdentityStruct(config, output_file):
  """Write out the data file needed to provide system identification.

  This data file is used at runtime by cros_configfs to probe the
  identity of the device.  The struct must align with the C code in
  cros_configfs.

  Args:
    config: The configuration dictionary (containing "chromeos").
    output_file: A file-like object to write to, opened in binary mode.
  """
  device_configs = config['chromeos']['configs']
  string_table = []

  # Add a string to the table if it does to exist. Return the number
  # of bytes offset the string will live from the base of the string
  # table.
  def _StringTableIndex(string):
    if string is None:
      return 0

    string = string.lower()
    string = string.encode('utf-8') + b'\000'
    if string not in string_table:
      string_table.append(string)

    index = 0
    for entry in string_table:
      if entry == string:
        return index
      index += len(entry)

  # Write the header of the struct.
  output_file.write(
      struct.pack(HEADER_FORMAT, STRUCT_VERSION, len(device_configs)))

  # Write each of the entry structs.
  for device_config in device_configs:
    identity_info = device_config.get('identity', {})
    firmware_manifest_key = device_config['name']
    flags = 0
    sku_id = 0
    if 'sku-id' in identity_info:
      flags |= EntryFlags.HAS_SKU_ID.value
      sku_id = identity_info['sku-id']

    smbios_name_match = None
    fdt_compatible_match = None
    whitelabel_match = None
    if 'smbios-name-match' in identity_info:
      flags |= EntryFlags.HAS_SMBIOS_NAME.value
      smbios_name_match = identity_info['smbios-name-match']
    if 'device-tree-compatible-match' in identity_info:
      flags |= EntryFlags.HAS_FDT_COMPATIBLE.value
      fdt_compatible_match = identity_info['device-tree-compatible-match']

    if 'customization-id' in identity_info:
      flags |= EntryFlags.HAS_CUSTOMIZATION_ID.value
      whitelabel_match = identity_info['customization-id']
    elif 'whitelabel-tag' in identity_info:
      flags |= EntryFlags.HAS_WHITELABEL.value
      whitelabel_match = identity_info['whitelabel-tag']

    output_file.write(
        struct.pack(ENTRY_FORMAT,
                    flags,
                    _StringTableIndex(smbios_name_match),
                    _StringTableIndex(fdt_compatible_match),
                    sku_id,
                    _StringTableIndex(whitelabel_match),
                    _StringTableIndex(firmware_manifest_key)))

  for entry in string_table:
    output_file.write(entry)
