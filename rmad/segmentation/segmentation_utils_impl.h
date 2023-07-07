// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_
#define RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_

#include "rmad/segmentation/segmentation_utils.h"

#include <memory>

#include <libsegmentation/feature_management.h>

#include "rmad/system/tpm_manager_client.h"
#include "rmad/utils/gsc_utils.h"

namespace rmad {

class SegmentationUtilsImpl : public SegmentationUtils {
 public:
  SegmentationUtilsImpl();
  // Used to inject |FeatureManagementInterface|, |TpmManagerClient| and
  // |GscUtils| for testing.
  explicit SegmentationUtilsImpl(
      std::unique_ptr<segmentation::FeatureManagementInterface>
          feature_management_interface,
      std::unique_ptr<TpmManagerClient> tpm_manager_client,
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
  bool IsBoardIdTypeEmpty() const;
  bool IsInitialFactoryMode() const;

  segmentation::FeatureManagement feature_management_;
  std::unique_ptr<TpmManagerClient> tpm_manager_client_;
  std::unique_ptr<GscUtils> gsc_utils_;
};

}  // namespace rmad

#endif  // RMAD_SEGMENTATION_SEGMENTATION_UTILS_IMPL_H_
