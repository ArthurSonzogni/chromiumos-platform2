# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import enum
import struct


STRUCT_VERSION = 0
# version, identity_type, entry_count, 4 bytes reserved
HEADER_FORMAT = '<LLL4x'
# flags, model match, sku match, whitelabel match
ENTRY_FORMAT = '<LLLL'


class IdentityType(enum.Enum):
  """The type of identity provided by the identity data file."""
  X86 = 0
  ARM = 1


class EntryFlags(enum.Enum):
  """The flags used at the beginning of each entry."""
  HAS_SKU_ID = 1 << 0
  HAS_WHITELABEL = 1 << 1

  # This device uses a customization ID from VPD to match instead of a
  # whitelabel tag. This is deprecated for new devices since 2017, so
  # it should only be set for old pre-unibuild migrations.
  HAS_CUSTOMIZATION_ID = 1 << 2

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

  # Detecting x86 vs. ARM is rather annoying given the JSON-like
  # schema.  This implementation checks if any config has an identity
  # dict containing a device-tree-compatible-match or firmware-name,
  # and correspondingly sets the identity type to ARM, otherwise X86.
  if any('device-tree-compatible-match' in c.get('identity', {})
         or 'firmware-name' in c.get('identity', {})
         for c in device_configs):
    identity_type = IdentityType.ARM
  else:
    identity_type = IdentityType.X86

  # Write the header of the struct, containing version and identity
  # type (x86 vs. ARM).
  output_file.write(
      struct.pack(HEADER_FORMAT, STRUCT_VERSION, identity_type.value,
                  len(device_configs)))

  # Write each of the entry structs.
  for device_config in device_configs:
    identity_info = device_config.get('identity', {})
    flags = 0
    sku_id = 0
    if 'sku-id' in identity_info:
      flags |= EntryFlags.HAS_SKU_ID.value
      sku_id = identity_info['sku-id']

    model_match = None
    whitelabel_match = None
    if identity_type is IdentityType.X86:
      if 'smbios-name-match' in identity_info:
        flags |= EntryFlags.HAS_SMBIOS_NAME.value
        model_match = identity_info['smbios-name-match']
    elif identity_type is IdentityType.ARM:
      model_match = identity_info['device-tree-compatible-match']

    if 'customization-id' in identity_info:
      flags |= EntryFlags.HAS_CUSTOMIZATION_ID.value
      whitelabel_match = identity_info['customization-id']
    elif 'whitelabel-tag' in identity_info:
      flags |= EntryFlags.HAS_WHITELABEL.value
      whitelabel_match = identity_info['whitelabel-tag']

    output_file.write(
        struct.pack(ENTRY_FORMAT,
                    flags,
                    _StringTableIndex(model_match),
                    sku_id,
                    _StringTableIndex(whitelabel_match)))

  for entry in string_table:
    output_file.write(entry)
