// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSEGMENTATION_FEATURE_MANAGEMENT_IMPL_H_
#define LIBSEGMENTATION_FEATURE_MANAGEMENT_IMPL_H_

#include <memory>
#include <set>
#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libcrossystem/crossystem.h>
#include <vpd/vpd.h>

#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_interface.h"
#include "proto/device_selection.pb.h"
#include "proto/feature_management.pb.h"

using chromiumos::feature_management::api::software::DeviceSelection;
using chromiumos::feature_management::api::software::FeatureBundle;
using chromiumos::feature_management::api::software::SelectionBundle;

namespace segmentation {

// Temporary file containing the device information and read first,
// for development purposes.
constexpr const char kTempDeviceInfoPath[] =
    "/run/libsegmentation/feature_device_info";

// VPD key name for persisting CBX status.
constexpr char kVpdKeyDeviceInfo[] = "feature_device_info";

// An implementation that invokes the corresponding functions provided
// in feature_management_interface.h.
class BRILLO_EXPORT FeatureManagementImpl : public FeatureManagementInterface {
 public:
  // Default implementation that use the database created by package
  // feature-management-data.
  FeatureManagementImpl();

  FeatureManagementImpl(crossystem::Crossystem* crossystem,
                        vpd::Vpd* vpd,
                        const std::string& feature_db,
                        const std::string& selection_db,
                        const std::string& os_version);

  bool IsFeatureEnabled(const std::string& name) override;

  FeatureLevel GetFeatureLevel() override;
  FeatureLevel GetMaxFeatureLevel() override;
  ScopeLevel GetScopeLevel() override;

  const std::set<std::string> ListFeatures(const FeatureUsage usage) override;

  bool FlashLevels() override;

  // Return feature level information based on HWID information and
  // hardware requirement.
  std::optional<DeviceSelection> GetDeviceInfoFromHwid(bool check_prefix_only);

 private:
  // Internal feature database
  FeatureBundle feature_bundle_;

  // Internal selection database
  SelectionBundle selection_bundle_;

#if USE_FEATURE_MANAGEMENT
  // Reads device info from the stateful partition, if not present reads it from
  // the hardware and then writes it to the stateful partition. After this it
  // tries to cache it to |cached_device_info_|.
  //
  // If we fail to write it to the stateful partition then this function will
  // return false and not set |cached_device_info_|.
  bool CacheDeviceInfo();

  // Check hardware requirement based on feature level
  // Currently for feature level 1 device, we need:
  // - 8GB of RAM
  // - 128GB SSD
  // See go/cros-tiering-prd for reference.
  bool Check_HW_Requirement(const DeviceSelection& selection);

  // Cache valid device information read from the stateful partition.
  std::optional<libsegmentation::DeviceInfo> cached_device_info_;

  // Hashed version of the current chromeos version (CHROMEOS_RELEASE_VERSION)
  uint32_t current_version_hash_;

  // To acccess internal data. Can be overriden, using backend if not.
  crossystem::Crossystem* crossystem_;
  std::unique_ptr<crossystem::Crossystem> crossystem_backend_;

  // To access internal data. Can be overridden, using backend if not.
  vpd::Vpd* vpd_;
  std::unique_ptr<vpd::Vpd> vpd_backend_;

#endif
};

}  // namespace segmentation

#endif  // LIBSEGMENTATION_FEATURE_MANAGEMENT_IMPL_H_
