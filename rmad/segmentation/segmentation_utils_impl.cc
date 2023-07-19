// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/segmentation/segmentation_utils_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <google/protobuf/text_format.h>
#include <libsegmentation/feature_management.h>

#include "rmad/constants.h"
#include "rmad/feature_enabled_devices.pb.h"
#include "rmad/system/tpm_manager_client_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/gsc_utils_impl.h"

namespace {

constexpr char kDevicesTextProtoFilePath[] = "devices.textproto";

constexpr char kEmptyBoardIdType[] = "ffffffff";

}  // namespace

namespace rmad {

SegmentationUtilsImpl::SegmentationUtilsImpl()
    : config_dir_path_(kDefaultConfigDirPath),
      feature_enabled_devices_(),
      feature_management_() {
  tpm_manager_client_ = std::make_unique<TpmManagerClientImpl>(GetSystemBus());
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  gsc_utils_ = std::make_unique<GscUtilsImpl>();
  ReadFeatureEnabledDevices();
}

SegmentationUtilsImpl::SegmentationUtilsImpl(
    const base::FilePath& config_dir_path,
    std::unique_ptr<segmentation::FeatureManagementInterface>
        feature_management_interface,
    std::unique_ptr<TpmManagerClient> tpm_manager_client,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<GscUtils> gsc_utils)
    : config_dir_path_(config_dir_path),
      feature_enabled_devices_(),
      feature_management_(std::move(feature_management_interface)),
      tpm_manager_client_(std::move(tpm_manager_client)),
      cros_config_utils_(std::move(cros_config_utils)),
      gsc_utils_(std::move(gsc_utils)) {
  ReadFeatureEnabledDevices();
}

void SegmentationUtilsImpl::ReadFeatureEnabledDevices() {
  if (std::string textproto_str;
      ReadFeatureEnabledDevicesTextProto(&textproto_str)) {
    if (google::protobuf::TextFormat::ParseFromString(
            textproto_str, &feature_enabled_devices_)) {
      DLOG(INFO) << "Successfully get feature enabled device list";
    } else {
      DLOG(ERROR) << "Failed to parse feature enabled device list";
    }
  }
}

bool SegmentationUtilsImpl::ReadFeatureEnabledDevicesTextProto(
    std::string* result) const {
  std::string model_name;
  if (!cros_config_utils_->GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name";
    return false;
  }
  const base::FilePath textproto_file_path =
      config_dir_path_.Append(model_name).Append(kDevicesTextProtoFilePath);
  if (!base::PathExists(textproto_file_path)) {
    // This is expected for projects that don't support features.
    DLOG(INFO) << textproto_file_path.value() << " doesn't exist";
    return false;
  }
  if (!base::ReadFileToString(textproto_file_path, result)) {
    LOG(ERROR) << "Failed to read " << textproto_file_path.value();
    return false;
  }
  return true;
}

bool SegmentationUtilsImpl::IsFeatureEnabled() const {
  // Feature is enabled if any of the RLZ supports the feature.
  return !feature_enabled_devices_.devices().empty();
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
