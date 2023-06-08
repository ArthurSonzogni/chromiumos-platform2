// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSEGMENTATION_FEATURE_MANAGEMENT_HWID_H_
#define LIBSEGMENTATION_FEATURE_MANAGEMENT_HWID_H_

#include <functional>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

#include "base/files/file.h"
#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_interface.h"

#include "proto/device_selection.pb.h"

namespace segmentation {

using chromiumos::feature_management::api::software::DeviceSelection;
using chromiumos::feature_management::api::software::SelectionBundle;

// An implementation that invokes the corresponding functions provided
// in feature_management_interface.h.
class BRILLO_EXPORT FeatureManagementHwid {
 public:
  using GetDeviceSelectionFn =
      std::function<std::optional<DeviceSelection>(bool)>;
  // Return feature level information based on HWID information
  // by looking at the device selection database.
  static std::optional<DeviceSelection> GetSelectionFromHWID(
      const SelectionBundle& selection_bundle,
      const std::string& hwid,
      bool check_prefix_only);

  static libsegmentation::DeviceInfo GetDeviceInfo(
      GetDeviceSelectionFn get_selection,
      bool is_chassis_x_branded,
      int32_t hw_compliance_version);
};

}  // namespace segmentation

#endif  // LIBSEGMENTATION_FEATURE_MANAGEMENT_HWID_H_
