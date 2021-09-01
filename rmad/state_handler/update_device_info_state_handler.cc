// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <google/protobuf/repeated_field.h>

#include "rmad/constants.h"
#include "rmad/utils/cbi_utils_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/regions_utils_impl.h"
#include "rmad/utils/vpd_utils_impl.h"

namespace rmad {

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

UpdateDeviceInfoStateHandler::UpdateDeviceInfoStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  cbi_utils_ = std::make_unique<CbiUtilsImpl>();
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  regions_utils_ = std::make_unique<RegionsUtilsImpl>();
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
}

UpdateDeviceInfoStateHandler::UpdateDeviceInfoStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<CbiUtils> cbi_utils,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<RegionsUtils> regions_utils,
    std::unique_ptr<VpdUtils> vpd_utils)
    : BaseStateHandler(json_store),
      cbi_utils_(std::move(cbi_utils)),
      cros_config_utils_(std::move(cros_config_utils)),
      regions_utils_(std::move(regions_utils)),
      vpd_utils_(std::move(vpd_utils)) {}

RmadErrorCode UpdateDeviceInfoStateHandler::InitializeState() {
  auto update_dev_info = std::make_unique<UpdateDeviceInfoState>();

  std::string serial_number;
  std::string region;
  uint64_t sku_id;
  std::string whitelabel_tag;
  std::string dram_part_number;

  std::vector<std::string> region_list;
  std::vector<int> sku_id_list;
  std::vector<std::string> whitelabel_tag_list;

  // We reserve 0 for unmatched indexes, it may be valid or invalid
  // depending on the type of information.
  uint32_t region_index = 0;
  uint32_t sku_index = 0;
  uint32_t whitelabel_index = 0;

  bool mlb_repair;

  if (!vpd_utils_ || !cbi_utils_ || !regions_utils_ || !cros_config_utils_) {
    LOG(ERROR) << "Failed to initialize utils for the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  // We can allow incorrect device info in vpd and cbi before writing.
  if (!vpd_utils_->GetSerialNumber(&serial_number)) {
    LOG(WARNING) << "Failed to get original serial number from vpd.";
  }
  if (!vpd_utils_->GetRegion(&region)) {
    LOG(WARNING) << "Failed to get original region from vpd.";
  }
  if (!cbi_utils_->GetSku(&sku_id)) {
    LOG(WARNING) << "Failed to get original sku from cbi.";
  }
  if (!vpd_utils_->GetWhitelabelTag(&whitelabel_tag)) {
    LOG(WARNING) << "Failed to get original whitelabel from vpd.";
  }
  if (!cbi_utils_->GetDramPartNum(&dram_part_number)) {
    LOG(WARNING) << "Failed to get original dram part number from cbi.";
  }

  if (!regions_utils_->GetRegionList(&region_list)) {
    LOG(ERROR) << "Failed to get the list of possible regions to initialize "
                  "the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (auto it = std::find(region_list.begin(), region_list.end(), region);
      it != region_list.end()) {
    // We reserve 0 for unmatched.
    region_index = std::distance(region_list.begin(), it) + 1;
  }

  if (!cros_config_utils_->GetSkuIdList(&sku_id_list)) {
    LOG(ERROR) << "Failed to get the list of possible sku-ids "
                  "to initialize the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (auto it = std::find(sku_id_list.begin(), sku_id_list.end(), sku_id);
      it != sku_id_list.end()) {
    // We reserve 0 for unmatched.
    sku_index = std::distance(sku_id_list.begin(), it) + 1;
  }

  if (!cros_config_utils_->GetWhitelabelTagList(&whitelabel_tag_list)) {
    LOG(ERROR) << "Failed to get the list of possible whitelabel-tags "
                  "to initialize the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  if (auto it = std::find(whitelabel_tag_list.begin(),
                          whitelabel_tag_list.end(), whitelabel_tag);
      it != whitelabel_tag_list.end()) {
    // We reserve 0 for unmatched.
    whitelabel_index = std::distance(whitelabel_tag_list.begin(), it) + 1;
  }

  if (!json_store_->GetValue(kMlbRepair, &mlb_repair)) {
    LOG(ERROR) << "Failed to get the mainboard repair status "
                  "to initialize the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  update_dev_info->set_original_serial_number(serial_number);
  update_dev_info->set_original_region_index(region_index);
  update_dev_info->set_original_sku_index(sku_index);
  update_dev_info->set_original_whitelabel_index(whitelabel_index);
  update_dev_info->set_original_dram_part_number(dram_part_number);

  // We leave the first option blank and reserve it for unmatched.
  update_dev_info->add_region_list("");
  for (auto region_option : region_list) {
    update_dev_info->add_region_list(region_option);
  }

  // We leave the first option blank and reserve it for unmatched.
  update_dev_info->add_sku_list(0);
  for (auto sku_option : sku_id_list) {
    // We get a vector<int> from cros_config, but we set a uint64_t to cbi.
    // Therefore, we should cast it to uint64_t here.
    update_dev_info->add_sku_list(static_cast<uint64_t>(sku_option));
  }

  // We leave the first option blank and reserve it for unmatched.
  update_dev_info->add_whitelabel_list("");
  for (auto whitelabel_option : whitelabel_tag_list) {
    update_dev_info->add_whitelabel_list(whitelabel_option);
  }

  update_dev_info->set_mlb_repair(mlb_repair);

  state_.set_allocated_update_device_info(update_dev_info.release());
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
UpdateDeviceInfoStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_update_device_info()) {
    LOG(ERROR) << "RmadState missing |update device info| state.";
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  if (!VerifyReadOnly(state.update_device_info()) ||
      !WriteDeviceInfo(state.update_device_info())) {
    return {.error = RMAD_ERROR_DEVICE_INFO_INVALID,
            .state_case = GetStateCase()};
  }

  state_ = state;

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kCheckCalibration};
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
    LOG(ERROR) << "The read-only |original whitelabel number| of "
                  "|update device info| is changed.";
    return false;
  }
  if (original_device_info.original_dram_part_number() !=
      device_info.original_dram_part_number()) {
    LOG(ERROR) << "The read-only |original dram part number| of "
                  "|update device info| is changed.";
    return false;
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
    LOG(ERROR) << "The read-only |whitelabel list| of |update device info| "
                  "is changed.";
    return false;
  }

  if (original_device_info.mlb_repair() != device_info.mlb_repair()) {
    LOG(ERROR) << "The read-only |mlb repair| of |update device info| "
                  "is changed.";
    return false;
  }

  return true;
}

bool UpdateDeviceInfoStateHandler::WriteDeviceInfo(
    const UpdateDeviceInfoState& device_info) {
  if (!vpd_utils_->SetSerialNumber(device_info.serial_number())) {
    LOG(ERROR) << "Failed to write |serial number| to vpd.";
    return false;
  }

  if (device_info.region_index() >=
          static_cast<uint64_t>(device_info.region_list_size()) ||
      device_info.region_index() == 0) {
    LOG(ERROR) << "It is a wrong |region index| of |region list|.";
    return false;
  }
  if (!vpd_utils_->SetRegion(
          device_info.region_list(device_info.region_index()))) {
    LOG(ERROR) << "Failed to write region to vpd.";
    return false;
  }

  if (device_info.sku_index() >=
          static_cast<uint64_t>(device_info.sku_list_size()) ||
      device_info.sku_index() == 0) {
    LOG(ERROR) << "It is a wrong |sku index| of |sku list|.";
    return false;
  }
  if (!cbi_utils_->SetSku(device_info.sku_list(device_info.sku_index()))) {
    LOG(ERROR) << "Failed to write sku to cbi.";
    return false;
  }

  // We can allow empty whitelabel-tag, so we can allow index == 0.
  if (device_info.whitelabel_index() >=
      static_cast<uint64_t>(device_info.whitelabel_list_size())) {
    LOG(ERROR) << "It is a wrong |whitelabel index| of |whitelabel list|.";
    return false;
  }
  // If the model does not support whitelabel, we don't need to write it.
  // Otherwise, even if the whitelabel is empty, we should always write
  // it to vpd.
  if (device_info.whitelabel_list_size() > 1 &&
      !vpd_utils_->SetWhitelabelTag(
          device_info.whitelabel_list(device_info.whitelabel_index()))) {
    LOG(ERROR) << "Failed to write whitelabel to vpd.";
    return false;
  }

  if (!cbi_utils_->SetDramPartNum(device_info.dram_part_number())) {
    LOG(ERROR) << "Failed to write dram part number to cbi.";
    return false;
  }

  return true;
}

}  // namespace rmad
