// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSEGMENTATION_FEATURE_MANAGEMENT_UTIL_H_
#define LIBSEGMENTATION_FEATURE_MANAGEMENT_UTIL_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

#include "base/files/file.h"
#include <base/values.h>
#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_interface.h"

namespace segmentation {

// An implementation that invokes the corresponding functions provided
// in feature_management_interface.h.
class BRILLO_EXPORT FeatureManagementUtil {
 public:
  // Reads device info from |file_path|. Returns std::nullopt if the read wasn't
  // successful.
  static std::optional<libsegmentation::DeviceInfo> ReadDeviceInfoFromFile(
      const base::FilePath& file_path);

  // Writes |device_info| as base64 to |file_path|. Returns false if the write
  // isn't successful.
  static bool WriteDeviceInfoToFile(
      const libsegmentation::DeviceInfo& device_info,
      const base::FilePath& file_path);

  // Converts feature level from the internal proto to the external API.
  static FeatureManagementInterface::FeatureLevel ConvertProtoFeatureLevel(
      libsegmentation::DeviceInfo_FeatureLevel feature_level);

  // Converts scope level from the internal proto to the external API.
  static FeatureManagementInterface::ScopeLevel ConvertProtoScopeLevel(
      libsegmentation::DeviceInfo_ScopeLevel scope_level);

  // Implement base8192 decoding used by hwid.
  // Returns a string of '0' and '1' suitable for decoding.
  static std::optional<std::string> DecodeHWID(const std::string& hwid);

  // Return the size of a block device.
  // dev format is '/dev/sda', '/dev/nvme0n1', '/dev/mmcblk0', ...
  // TODO(b:176492189): Move to a common library
  static std::optional<int64_t> GetDiskSpace(const base::FilePath& dev);

  // Find the fixed block device on the device.
  // It may not be the device the rootfs is when we run ChromeOS from a
  // removable device.
  // The block device will be for example /dev/sda, /dev/mmcblk1, ...
  // TODO(b:176492189): Move to a common library
  // root is the usual '/', unless we are unit testing. In that case.
  // root points to a temporary directory set up for testing.
  static std::optional<base::FilePath> GetDefaultRoot(
      const base::FilePath& root);
};

}  // namespace segmentation

#endif  // LIBSEGMENTATION_FEATURE_MANAGEMENT_UTIL_H_
