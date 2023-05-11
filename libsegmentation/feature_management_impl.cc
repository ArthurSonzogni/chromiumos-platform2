// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/values.h>
#include <google/protobuf/util/json_util.h>
#include <rootdev/rootdev.h>
#include <vboot/vboot_host.h>

#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_impl.h"
#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

namespace segmentation {

// Sysfs file corresponding to VPD state. This will be used to persist device
// info state and read cache device info state.
constexpr const char kVpdSysfsFilePath[] =
    "/sys/firmware/vpd/rw/feature_device_info";

#if USE_FEATURE_MANAGEMENT
// Sysfs file corresponding to VPD state. This will be used to persist device
// info state and read cache device info state.
const char* kDeviceInfoFilePath = kVpdSysfsFilePath;
#else
constexpr char kDeviceInfoFilePath[] = "";

FeatureManagementInterface::FeatureLevel
FeatureManagementImpl::GetFeatureLevel() {
  return FeatureLevel::FEATURE_LEVEL_0;
}

FeatureManagementInterface::ScopeLevel FeatureManagementImpl::GetScopeLevel() {
  return ScopeLevel::SCOPE_LEVEL_0;
}

#endif

FeatureManagementImpl::FeatureManagementImpl()
    : FeatureManagementImpl(base::FilePath(kDeviceInfoFilePath)) {}

FeatureManagementImpl::FeatureManagementImpl(
    const base::FilePath& device_info_file_path)
    : device_info_file_path_(device_info_file_path) {
  persist_via_vpd_ =
      device_info_file_path_ == base::FilePath(kVpdSysfsFilePath);
}

bool FeatureManagementImpl::IsFeatureEnabled(const std::string& name) const {
  // To be continued.
  return false;
}

}  // namespace segmentation
