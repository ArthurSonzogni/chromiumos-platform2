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

#if USE_FEATURE_MANAGEMENT
// File that will house device info on the stateful partition.
constexpr char kDeviceInfoFilePath[] =
    "/mnt/stateful_partition/unencrypted/cache/libsegmentation/property.json";

#else
constexpr char kDeviceInfoFilePath[] = "";

FeatureManagementInterface::FeatureLevel
FeatureManagementImpl::GetFeatureLevel() {
  return FeatureLevel::FEATURE_LEVEL_0;
}
#endif

FeatureManagementImpl::FeatureManagementImpl()
    : FeatureManagementImpl(base::FilePath(kDeviceInfoFilePath)) {}

bool FeatureManagementImpl::IsFeatureEnabled(const std::string& name) const {
  // To be continued.
  return false;
}

}  // namespace segmentation
