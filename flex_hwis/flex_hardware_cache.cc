// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hardware_cache.h"

#include <map>
#include <string>
#include <vector>

#include <chromeos/constants/flex_hwis.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace flex_hwis {

namespace {

// RepeatedField will be a RepeatedPtrField from a protobuf.
template <typename RepeatedField>
std::string GetNames(const RepeatedField& device_list) {
  std::vector<std::string> names;
  for (auto dev : device_list) {
    names.push_back(dev.name());
  }
  return base::JoinString(names, ", ");
}

template <typename RepeatedField>
std::string GetDrivers(const RepeatedField& device_list) {
  std::vector<std::string> drivers;
  for (auto& dev : device_list) {
    // Named 'driver', singular, but it's a repeated field.
    std::vector<std::string> device_drivers(dev.driver().begin(),
                                            dev.driver().end());
    // Use the slash as a separator to match rubber-chicken-tool.
    drivers.push_back(base::JoinString(device_drivers, "/"));
  }
  return base::JoinString(drivers, ", ");
}

template <typename RepeatedField>
std::string GetIds(const RepeatedField& device_list) {
  std::vector<std::string> ids;
  for (auto dev : device_list) {
    ids.push_back(dev.id());
  }
  return base::JoinString(ids, ", ");
}

std::string BoolToString(bool value) {
  return value ? "true" : "false";
}

}  // namespace

bool WriteCacheToDisk(hwis_proto::Device& data, const base::FilePath& root) {
  std::map<std::string, std::string> file_contents = {
      {kFlexProductNameKey, data.dmi_info().product_name()},
      {kFlexProductVendorKey, data.dmi_info().vendor()},
      {kFlexProductVersionKey, data.dmi_info().product_version()},
      {kFlexTotalMemoryKey, base::NumberToString(data.memory().total_kib())},
      {kFlexBiosVersionKey, data.bios().bios_version()},
      {kFlexSecurebootKey, BoolToString(data.bios().secureboot())},
      {kFlexUefiKey, BoolToString(data.bios().uefi())},
      {kFlexBluetoothDriverKey, GetDrivers(data.bluetooth_adapter())},
      {kFlexBluetoothIdKey, GetIds(data.bluetooth_adapter())},
      {kFlexBluetoothNameKey, GetNames(data.bluetooth_adapter())},
      {kFlexEthernetDriverKey, GetDrivers(data.ethernet_adapter())},
      {kFlexEthernetIdKey, GetIds(data.ethernet_adapter())},
      {kFlexEthernetNameKey, GetNames(data.ethernet_adapter())},
      {kFlexWirelessDriverKey, GetDrivers(data.wireless_adapter())},
      {kFlexWirelessIdKey, GetIds(data.wireless_adapter())},
      {kFlexWirelessNameKey, GetNames(data.wireless_adapter())},
      {kFlexGpuDriverKey, GetDrivers(data.gpu())},
      {kFlexGpuIdKey, GetIds(data.gpu())},
      {kFlexGpuNameKey, GetNames(data.gpu())},
      {kFlexGlVersionKey, data.graphics_info().gl_version()},
      {kFlexGlShadingVersionKey, data.graphics_info().gl_shading_version()},
      {kFlexGlVendorKey, data.graphics_info().gl_vendor()},
      {kFlexGlRendererKey, data.graphics_info().gl_renderer()},
      {kFlexTpmVersionKey, data.tpm().tpm_version()},
      {kFlexTpmSpecLevelKey, base::NumberToString(data.tpm().spec_level())},
      {kFlexTpmManufacturerKey,
       base::NumberToString(data.tpm().manufacturer())},
      {kFlexTpmDidVidKey, data.tpm().did_vid()},
      {kFlexTpmAllowListedKey, BoolToString(data.tpm().tpm_allow_listed())},
      {kFlexTpmOwnedKey, BoolToString(data.tpm().tpm_owned())},
      {kFlexTouchpadStackKey, data.touchpad().stack()},
  };

  std::vector gl_extensions(data.graphics_info().gl_extensions().begin(),
                            data.graphics_info().gl_extensions().end());
  file_contents.insert(
      {kFlexGlExtensionsKey, base::JoinString(gl_extensions, ", ")});

  // If no CPUs, ignore. Otherwise grab the first one, because although there
  // are multiple cpu info structures, the name matches across all of them for
  // the devices we've seen.
  if (!data.cpu().empty()) {
    file_contents.insert({kFlexCpuNameKey, data.cpu(0).name()});
  }

  base::FilePath cache_dir = root.Append(kFlexHardwareCacheDir);
  bool all_succeeded = true;
  for (const auto& [filename, contents] : file_contents) {
    base::FilePath path(cache_dir.Append(filename));
    if (!base::WriteFile(path, contents)) {
      // Don't stop for failures, let's write what we can.
      all_succeeded = false;
      LOG(WARNING) << "Couldn't write: " << filename << " to " << path;
    }
  }

  // Return true only if all writes were successful.
  return all_succeeded;
}

}  // namespace flex_hwis
