// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/segmentation/segmentation_utils_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <libsegmentation/feature_management.h>

#include "rmad/system/tpm_manager_client_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/gsc_utils_impl.h"

namespace {

constexpr char kEmptyBoardIdType[] = "ffffffff";

}  // namespace

namespace rmad {

SegmentationUtilsImpl::SegmentationUtilsImpl() : feature_management_() {
  tpm_manager_client_ = std::make_unique<TpmManagerClientImpl>(GetSystemBus());
  gsc_utils_ = std::make_unique<GscUtilsImpl>();
}

SegmentationUtilsImpl::SegmentationUtilsImpl(
    std::unique_ptr<segmentation::FeatureManagementInterface>
        feature_management_interface,
    std::unique_ptr<TpmManagerClient> tpm_manager_client,
    std::unique_ptr<GscUtils> gsc_utils)
    : feature_management_(std::move(feature_management_interface)),
      tpm_manager_client_(std::move(tpm_manager_client)),
      gsc_utils_(std::move(gsc_utils)) {}

bool SegmentationUtilsImpl::IsFeatureEnabled() const {
  // TODO(chenghan): Get the allowlist from DLM payload.
  return false;
}

bool SegmentationUtilsImpl::IsFeatureMutable() const {
  // If anything goes wrong, assume feature is immutable to prevent someone
  // attempting to set the feature flags.
  GscVersion gsc_version;
  if (!tpm_manager_client_->GetGscVersion(&gsc_version)) {
    LOG(ERROR) << "Failed to get GSC version";
    return false;
  }
  // Condition is different for Cr50 and Ti50.
  switch (gsc_version) {
    case GscVersion::GSC_VERSION_NOT_GSC:
      return false;
    case GscVersion::GSC_VERSION_CR50:
      return IsBoardIdTypeEmpty();
    case GscVersion::GSC_VERSION_TI50:
      return IsInitialFactoryMode();
  }

  return false;
}

int SegmentationUtilsImpl::GetFeatureLevel() const {
  return feature_management_.GetFeatureLevel();
}

bool SegmentationUtilsImpl::GetFeatureFlags(bool* is_chassis_branded,
                                            int* hw_compliance_version) const {
  return gsc_utils_->GetFactoryConfig(is_chassis_branded,
                                      hw_compliance_version);
}

bool SegmentationUtilsImpl::SetFeatureFlags(bool is_chassis_branded,
                                            int hw_compliance_version) {
  return gsc_utils_->SetFactoryConfig(is_chassis_branded,
                                      hw_compliance_version);
}

bool SegmentationUtilsImpl::IsBoardIdTypeEmpty() const {
  std::string board_id_type;
  if (!gsc_utils_->GetBoardIdType(&board_id_type)) {
    LOG(ERROR) << "Failed to get board ID type";
    return false;
  }
  return board_id_type == kEmptyBoardIdType;
}

bool SegmentationUtilsImpl::IsInitialFactoryMode() const {
  return gsc_utils_->IsInitialFactoryModeEnabled();
}

}  // namespace rmad
