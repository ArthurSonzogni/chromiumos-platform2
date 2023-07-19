// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_
#define RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_

#include "rmad/segmentation/segmentation_utils.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <libsegmentation/feature_management.h>

#include "rmad/feature_enabled_devices.pb.h"
#include "rmad/system/tpm_manager_client.h"
#include "rmad/utils/cros_config_utils.h"
#include "rmad/utils/gsc_utils.h"

namespace rmad {

class SegmentationUtilsImpl : public SegmentationUtils {
 public:
  SegmentationUtilsImpl();
  // Used to inject |FeatureManagementInterface|, |TpmManagerClient| and
  // |GscUtils| for testing.
  explicit SegmentationUtilsImpl(
      const base::FilePath& config_dir_path,
      std::unique_ptr<segmentation::FeatureManagementInterface>
          feature_management_interface,
      std::unique_ptr<TpmManagerClient> tpm_manager_client,
      std::unique_ptr<CrosConfigUtils> cros_config_utils,
      std::unique_ptr<GscUtils> gsc_utils);
  ~SegmentationUtilsImpl() override = default;

  bool IsFeatureEnabled() const override;
  bool IsFeatureMutable() const override;
  int GetFeatureLevel() const override;
  bool GetFeatureFlags(bool* is_chassis_branded,
                       int* hw_compliance_version) const override;
  bool SetFeatureFlags(bool is_chassis_branded,
                       int hw_compliance_version) override;

 private:
  void ReadFeatureEnabledDevices();
  bool ReadFeatureEnabledDevicesTextProto(std::string* result) const;
  bool IsBoardIdTypeEmpty() const;
  bool IsInitialFactoryMode() const;

  base::FilePath config_dir_path_;
  FeatureEnabledDevices feature_enabled_devices_;
  segmentation::FeatureManagement feature_management_;
  std::unique_ptr<TpmManagerClient> tpm_manager_client_;
  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
  std::unique_ptr<GscUtils> gsc_utils_;
};

}  // namespace rmad

#endif  // RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_
