// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <google/protobuf/repeated_field.h>
#include <libsegmentation/feature_management.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/segmentation/fake_segmentation_utils.h"
#include "rmad/segmentation/segmentation_utils_impl.h"
#include "rmad/utils/cbi_utils_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/regions_utils_impl.h"
#include "rmad/utils/vpd_utils_impl.h"
#include "rmad/utils/write_protect_utils_impl.h"

namespace {

template <typename T>
bool IsRepeatedFieldSame(const T& list1, const T& list2) {
  int size = list1.size();
  if (size != list2.size()) {
    return false;
  }
  for (int i = 0; i < size; i++) {
    if (list1.at(i) != list2.at(i)) {
      return false;
    }
  }
  return true;
}

}  // namespace

namespace rmad {

UpdateDeviceInfoStateHandler::UpdateDeviceInfoStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(kDefaultWorkingDirPath) {
  cbi_utils_ = std::make_unique<CbiUtilsImpl>();
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  write_protect_utils_ = std::make_unique<WriteProtectUtilsImpl>();
  regions_utils_ = std::make_unique<RegionsUtilsImpl>();
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
  if (base::PathExists(GetFakeFeaturesInputFilePath())) {
    segmentation_utils_ = CreateFakeSegmentationUtils();
  } else {
    segmentation_utils_ = std::make_unique<SegmentationUtilsImpl>();
  }
}

UpdateDeviceInfoStateHandler::UpdateDeviceInfoStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    const base::FilePath& working_dir_path,
    std::unique_ptr<CbiUtils> cbi_utils,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<WriteProtectUtils> write_protect_utils,
    std::unique_ptr<RegionsUtils> regions_utils,
    std::unique_ptr<VpdUtils> vpd_utils,
    std::unique_ptr<SegmentationUtils> segmentation_utils)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      cbi_utils_(std::move(cbi_utils)),
      cros_config_utils_(std::move(cros_config_utils)),
      write_protect_utils_(std::move(write_protect_utils)),
      regions_utils_(std::move(regions_utils)),
      vpd_utils_(std::move(vpd_utils)),
      segmentation_utils_(std::move(segmentation_utils)) {}

RmadErrorCode UpdateDeviceInfoStateHandler::InitializeState() {
  CHECK(cbi_utils_);
  CHECK(cros_config_utils_);
  CHECK(write_protect_utils_);
  CHECK(regions_utils_);
  CHECK(vpd_utils_);
  CHECK(segmentation_utils_);

  // Make sure HWWP is off before initializing the state.
  if (bool hwwp_enabled;
      !write_protect_utils_->GetHardwareWriteProtectionStatus(&hwwp_enabled) ||
      hwwp_enabled) {
    return RMAD_ERROR_WP_ENABLED;
  }

  // Get rmad config first.
  if (!cros_config_utils_->GetRmadConfig(&rmad_config_)) {
    LOG(ERROR) << "Failed to get RMA config from cros_config";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  auto update_dev_info = std::make_unique<UpdateDeviceInfoState>();

  std::string serial_number;
  std::string region;
  uint64_t sku_id;
  bool is_custom_label_exist;
  std::string custom_label_tag;
  std::string dram_part_number;
  UpdateDeviceInfoState::FeatureLevel feature_level;

  std::vector<std::string> region_list;
  std::vector<uint64_t> sku_id_list;
  std::vector<std::string> custom_label_tag_list;

  // We reserve -1 for unmatched indexes.
  int32_t region_index = -1;
  int32_t sku_index = -1;
  int32_t custom_label_index = -1;

  bool mlb_repair;

  // We can allow incorrect device info in VPD and CBI before writing.
  if (!vpd_utils_->GetSerialNumber(&serial_number)) {
    LOG(WARNING) << "Failed to get original serial number from vpd.";
  }
  if (!vpd_utils_->GetRegion(&region)) {
    LOG(WARNING) << "Failed to get original region from vpd.";
  }
  if (!cros_config_utils_->GetSkuId(&sku_id)) {
    // If the device uses CBI, SKU might not be set on the board.
    LOG(WARNING) << "Failed to get original sku from cros_config.";
    if (!rmad_config_.has_cbi) {
      // Set |sku_id| as -1 to represent we failed to get the SKU. This value
      // must be overridden afterward and never left as -1 when communicate with
      // Chrome.
      sku_id = -1;
    }
  }
  // For backward compatibility, we should use cros_config to get the
  // custom-label, which already handles it.
  is_custom_label_exist =
      cros_config_utils_->GetCustomLabelTag(&custom_label_tag);
  if (rmad_config_.has_cbi && !cbi_utils_->GetDramPartNum(&dram_part_number)) {
    LOG(WARNING) << "Failed to get original dram part number from cbi.";
  }

  if (!regions_utils_->GetRegionList(&region_list)) {
    LOG(ERROR) << "Failed to get the list of possible regions to initialize "
                  "the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (auto it = std::find(region_list.begin(), region_list.end(), region);
      it != region_list.end()) {
    region_index = std::distance(region_list.begin(), it);
  }

  if (!cros_config_utils_->GetSkuIdList(&sku_id_list)) {
    LOG(ERROR) << "Failed to get the list of possible sku-ids to initialize "
                  "the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (!rmad_config_.has_cbi) {
    if (sku_id == -1) {
      if (!sku_id_list.empty()) {
        LOG(ERROR) << "The model has a list of SKUs but the strapping pins are "
                      "not set properly.";
        return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
      }
      // If the device uses strapping pins but fails to get the SKU, we will set
      // it to 0 as a placeholder.
      sku_id = 0;
    }
    // If the device doesn't have CBI, make the current SKU ID the only option
    // so the user cannot change it.
    sku_id_list.clear();
    sku_id_list.push_back(sku_id);
  }
  if (auto it = std::find(sku_id_list.begin(), sku_id_list.end(), sku_id);
      it != sku_id_list.end()) {
    sku_index = std::distance(sku_id_list.begin(), it);
  }

  if (!cros_config_utils_->GetCustomLabelTagList(&custom_label_tag_list)) {
    LOG(ERROR) << "Failed to get the list of possible custom-label-tags "
                  "to initialize the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (is_custom_label_exist) {
    if (auto it = std::find(custom_label_tag_list.begin(),
                            custom_label_tag_list.end(), custom_label_tag);
        it != custom_label_tag_list.end()) {
      custom_label_index = std::distance(custom_label_tag_list.begin(), it);
    }
    if (custom_label_index == -1) {
      LOG(WARNING) << "We found an unmatched custom-label-tag in vpd.";
      vpd_utils_->RemoveCustomLabelTag();
    }
  }
  if (segmentation_utils_->IsFeatureEnabled()) {
    if (segmentation_utils_->IsFeatureMutable()) {
      // If feature is mutable, we don't know the final feature level.
      feature_level = UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_UNKNOWN;
    } else {
      switch (segmentation_utils_->GetFeatureLevel()) {
        case 0:
          feature_level = UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_0;
          break;
        case 1:
          feature_level = UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_1;
          break;
        default:
          feature_level = UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_0;
          NOTREACHED_NORETURN();
      }
    }
  } else {
    feature_level = UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_UNSUPPORTED;
  }

  if (!json_store_->GetValue(kMlbRepair, &mlb_repair)) {
    LOG(ERROR) << "Failed to get the mainboard repair status "
                  "to initialize the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  update_dev_info->set_original_serial_number(serial_number);
  update_dev_info->set_original_region_index(region_index);
  update_dev_info->set_original_sku_index(sku_index);
  update_dev_info->set_original_whitelabel_index(custom_label_index);
  update_dev_info->set_original_dram_part_number(dram_part_number);
  update_dev_info->set_original_custom_label_index(custom_label_index);
  update_dev_info->set_original_feature_level(feature_level);

  for (auto region_option : region_list) {
    update_dev_info->add_region_list(region_option);
  }

  for (auto sku_option : sku_id_list) {
    update_dev_info->add_sku_list(sku_option);
  }

  for (auto custom_label_option : custom_label_tag_list) {
    update_dev_info->add_whitelabel_list(custom_label_option);
    update_dev_info->add_custom_label_list(custom_label_option);
  }

  update_dev_info->set_mlb_repair(mlb_repair);

  state_.set_allocated_update_device_info(update_dev_info.release());
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
UpdateDeviceInfoStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_update_device_info()) {
    LOG(ERROR) << "RmadState missing |update device info| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  if (!VerifyReadOnly(state.update_device_info())) {
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  }

  if (!WriteDeviceInfo(state.update_device_info())) {
    vpd_utils_->ClearRoVpdCache();
    vpd_utils_->ClearRwVpdCache();
    if (bool hwwp_enabled;
        !write_protect_utils_->GetHardwareWriteProtectionStatus(
            &hwwp_enabled) ||
        hwwp_enabled) {
      return NextStateCaseWrapper(RMAD_ERROR_WP_ENABLED);
    }
    return NextStateCaseWrapper(RMAD_ERROR_CANNOT_WRITE);
  }

  state_ = state;

  return NextStateCaseWrapper(RmadState::StateCase::kProvisionDevice);
}

bool UpdateDeviceInfoStateHandler::VerifyReadOnly(
    const UpdateDeviceInfoState& device_info) {
  auto original_device_info = state_.update_device_info();
  if (original_device_info.original_serial_number() !=
      device_info.original_serial_number()) {
    LOG(ERROR) << "The read-only |original serial number| of "
                  "|update device info| is changed.";
    return false;
  }
  if (original_device_info.original_region_index() !=
      device_info.original_region_index()) {
    LOG(ERROR) << "The read-only |original region index| of "
                  "|update device info| is changed.";
    return false;
  }
  if (original_device_info.original_sku_index() !=
      device_info.original_sku_index()) {
    LOG(ERROR) << "The read-only |original sku index| of "
                  "|update device info| is changed.";
    return false;
  }
  if (original_device_info.original_whitelabel_index() !=
      device_info.original_whitelabel_index()) {
    LOG(ERROR) << "The read-only |original custom-label-tag index| of "
                  "|update device info| is changed.";
    return false;
  }
  if (original_device_info.original_dram_part_number() !=
      device_info.original_dram_part_number()) {
    LOG(ERROR) << "The read-only |original dram part number| of "
                  "|update device info| is changed.";
    return false;
  }
  if (original_device_info.original_custom_label_index() !=
      device_info.original_custom_label_index()) {
    LOG(ERROR) << "The read-only |original custom-label-tag index| of "
                  "|update device info| is changed.";
    return false;
  }
  if (original_device_info.original_feature_level() !=
      device_info.original_feature_level()) {
    LOG(ERROR) << "The read-only |original feature level| of "
                  "|update device info| is changed.";
  }

  if (!IsRepeatedFieldSame(original_device_info.region_list(),
                           device_info.region_list())) {
    LOG(ERROR) << "The read-only |region list| of |update device info| "
                  "is changed.";
    return false;
  }

  if (!IsRepeatedFieldSame(original_device_info.sku_list(),
                           device_info.sku_list())) {
    LOG(ERROR) << "The read-only |sku list| of |update device info| "
                  "is changed.";
    return false;
  }

  if (!IsRepeatedFieldSame(original_device_info.whitelabel_list(),
                           device_info.whitelabel_list())) {
    LOG(ERROR) << "The read-only legacy |custom-label-tag list| of "
                  "|update device info| is changed.";
    return false;
  }
  if (!IsRepeatedFieldSame(original_device_info.custom_label_list(),
                           device_info.custom_label_list())) {
    LOG(ERROR)
        << "The read-only |custom-label-tag list| of |update device info| "
           "is changed.";
    return false;
  }

  if (original_device_info.mlb_repair() != device_info.mlb_repair()) {
    LOG(ERROR) << "The read-only |mlb repair| of |update device info| "
                  "is changed.";
    return false;
  }

  if (device_info.region_index() < 0 ||
      device_info.region_index() >= device_info.region_list_size()) {
    LOG(ERROR) << "It is a wrong |region index| of |region list|.";
    return false;
  }

  if (device_info.sku_index() < 0 ||
      device_info.sku_index() >= device_info.sku_list_size()) {
    LOG(ERROR) << "It is a wrong |sku index| of |sku list|.";
    return false;
  }

  // At least one of |custom_label_index| and |whitelabel_index| should be
  // valid.
  if ((device_info.custom_label_index() < 0 ||
       device_info.custom_label_index() >=
           device_info.custom_label_list_size()) &&
      (device_info.whitelabel_index() < 0 ||
       device_info.whitelabel_index() >= device_info.whitelabel_list_size())) {
    LOG(ERROR)
        << "It is a wrong |custom-label-tag index| of |custom-label-tag list|.";
    return false;
  }

  return true;
}

bool UpdateDeviceInfoStateHandler::WriteDeviceInfo(
    const UpdateDeviceInfoState& device_info) {
  if (device_info.serial_number() != device_info.original_serial_number() &&
      !vpd_utils_->SetSerialNumber(device_info.serial_number())) {
    LOG(ERROR) << "Failed to save |serial number| to vpd cache.";
    return false;
  }

  if (device_info.region_index() != device_info.original_region_index() &&
      !vpd_utils_->SetRegion(
          device_info.region_list(device_info.region_index()))) {
    LOG(ERROR) << "Failed to save region to vpd cache.";
    return false;
  }

  if (rmad_config_.has_cbi &&
      device_info.sku_index() != device_info.original_sku_index() &&
      !cbi_utils_->SetSkuId(device_info.sku_list(device_info.sku_index()))) {
    LOG(ERROR) << "Failed to write sku to cbi.";
    return false;
  }

  bool custom_label_tag_updated;
  std::string custom_label_tag;
  if (device_info.custom_label_index() >= 0 &&
      device_info.custom_label_index() < device_info.custom_label_list_size()) {
    custom_label_tag_updated = (device_info.custom_label_index() !=
                                device_info.original_custom_label_index());
    custom_label_tag =
        device_info.custom_label_list(device_info.custom_label_index());
  } else {
    custom_label_tag_updated = (device_info.whitelabel_index() !=
                                device_info.original_whitelabel_index());
    custom_label_tag =
        device_info.whitelabel_list(device_info.whitelabel_index());
  }
  if (custom_label_tag_updated &&
      !vpd_utils_->SetCustomLabelTag(custom_label_tag,
                                     rmad_config_.use_legacy_custom_label)) {
    LOG(ERROR) << "Failed to save custom_label_tag to vpd cache.";
    return false;
  }

  if (rmad_config_.has_cbi &&
      device_info.dram_part_number() !=
          device_info.original_dram_part_number() &&
      !cbi_utils_->SetDramPartNum(device_info.dram_part_number())) {
    LOG(ERROR) << "Failed to write dram part number to cbi.";
    return false;
  }

  if (device_info.original_feature_level() ==
      UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_UNKNOWN) {
    // Provision |is_chassis_branded| and |hw_compliance_version|.
    segmentation_utils_->SetFeatureFlags(device_info.is_chassis_branded(),
                                         device_info.hw_compliance_version());
  }

  if (!vpd_utils_->FlushOutRoVpdCache()) {
    LOG(ERROR) << "Failed to flush cache to ro vpd.";
    return false;
  }
  return true;
}

std::unique_ptr<SegmentationUtils>
UpdateDeviceInfoStateHandler::CreateFakeSegmentationUtils() const {
  return std::make_unique<FakeSegmentationUtils>(GetTestDirPath());
}

base::FilePath UpdateDeviceInfoStateHandler::GetTestDirPath() const {
  return working_dir_path_.AppendASCII(kTestDirPath);
}

base::FilePath UpdateDeviceInfoStateHandler::GetFakeFeaturesInputFilePath()
    const {
  return GetTestDirPath().AppendASCII(kFakeFeaturesInputFilePath);
}

}  // namespace rmad
