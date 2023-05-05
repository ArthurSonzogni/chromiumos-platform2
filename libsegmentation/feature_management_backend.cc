// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/values.h>
#include <google/protobuf/util/json_util.h>
#include <libsegmentation/device_info.pb.h>
#include <libsegmentation/feature_management_impl.h>
#include <rootdev/rootdev.h>
#include <vboot/vboot_host.h>

#include <cstdint>
#include <optional>
#include <string>

#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

namespace segmentation {

namespace {

// Returns the device information parsed from the HW id.
std::optional<libsegmentation::DeviceInfo> GetDeviceInfoFromHwId() {
  libsegmentation::DeviceInfo device_info;
  return device_info;
}

}  // namespace

FeatureManagementInterface::FeatureLevel
FeatureManagementImpl::GetFeatureLevel() {
  if (cached_device_info_) {
    return FeatureManagementUtil::ConvertProtoFeatureLevel(
        cached_device_info_->feature_level());
  }

  if (!CacheDeviceInfo()) {
    return FeatureLevel::FEATURE_LEVEL_UNKNOWN;
  }

  return FeatureManagementUtil::ConvertProtoFeatureLevel(
      cached_device_info_->feature_level());
}

FeatureManagementInterface::ScopeLevel FeatureManagementImpl::GetScopeLevel() {
  if (cached_device_info_) {
    return FeatureManagementUtil::ConvertProtoScopeLevel(
        cached_device_info_->scope_level());
  }

  if (!CacheDeviceInfo()) {
    return ScopeLevel::SCOPE_LEVEL_UNKNOWN;
  }

  return FeatureManagementUtil::ConvertProtoScopeLevel(
      cached_device_info_->scope_level());
}

bool FeatureManagementImpl::CacheDeviceInfo() {
  base::FilePath device_info_file = base::FilePath(device_info_file_path_);
  // If the device info isn't cached, try to read it from the stateful partition
  // file that contains the information. If it's not present in the stateful
  // partition file then read it form the hardware id and write it to the
  // stateful partition for subsequent boots.
  std::optional<libsegmentation::DeviceInfo> device_info_result =
      FeatureManagementUtil::ReadDeviceInfoFromFile(device_info_file);
  if (!device_info_result) {
    device_info_result = GetDeviceInfoFromHwId();
    if (!device_info_result) {
      LOG(ERROR) << "Failed to get device info from the hardware id";
      return false;
    }

    // Persist the device info read from the hardware id to the stateful
    // partition. If we fail to write it then don't cache it. We only cache it
    // if we have the same device info on the stateful partition.
    if (!FeatureManagementUtil::WriteDeviceInfoToFile(
            device_info_result.value(), device_info_file)) {
      LOG(WARNING) << "Failed to persist device info";
      return false;
    }
  }

  // At this point device information is present on stateful. We can cache it.
  cached_device_info_ = device_info_result.value();
  return true;
}

}  // namespace segmentation
