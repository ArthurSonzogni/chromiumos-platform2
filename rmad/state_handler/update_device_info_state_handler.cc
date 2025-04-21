// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/text_format.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/segmentation/fake_segmentation_utils.h"
#include "rmad/segmentation/segmentation_utils_impl.h"
#include "rmad/sku_filter.pb.h"
#include "rmad/utils/cbi_utils_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/regions_utils_impl.h"
#include "rmad/utils/rmad_config_utils_impl.h"
#include "rmad/utils/vpd_utils_impl.h"
#include "rmad/utils/write_protect_utils_impl.h"

namespace {

constexpr char kSkuFilterProtoFilePath[] = "sku_filter.textproto";

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

template <typename T>
bool HasDifferentElementsInColumn(const std::vector<std::vector<T>>& table,
                                  int i) {
  CHECK_GE(i, 0) << "Index cannot be negative.";
  if (table.size() <= 1) {
    return false;
  }
  CHECK_LT(i, table[0].size()) << "Index is larger than table size";
  const T& value = table[0][i];
  for (const auto& v : table) {
    CHECK_LT(i, v.size()) << "Index is larger than table size";
    if (v[i] != value) {
      return true;
    }
  }
  return false;
}

template <typename T>
std::vector<T> FilterArray(const std::vector<T>& arr,
                           const std::vector<bool>& select) {
  CHECK_EQ(arr.size(), select.size());
  std::vector<T> ret;
  for (int i = 0; i < arr.size(); ++i) {
    if (select[i]) {
      ret.push_back(arr[i]);
    }
  }
  return ret;
}

}  // namespace

namespace rmad {

UpdateDeviceInfoStateHandler::UpdateDeviceInfoStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(kDefaultWorkingDirPath),
      config_dir_path_(kDefaultConfigDirPath) {
  cbi_utils_ = std::make_unique<CbiUtilsImpl>();
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  write_protect_utils_ = std::make_unique<WriteProtectUtilsImpl>();
  regions_utils_ = std::make_unique<RegionsUtilsImpl>();
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
  rmad_config_utils_ = std::make_unique<RmadConfigUtilsImpl>();
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
    const base::FilePath& config_dir_path,
    std::unique_ptr<CbiUtils> cbi_utils,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<WriteProtectUtils> write_protect_utils,
    std::unique_ptr<RegionsUtils> regions_utils,
    std::unique_ptr<VpdUtils> vpd_utils,
    std::unique_ptr<SegmentationUtils> segmentation_utils,
    std::unique_ptr<RmadConfigUtils> rmad_config_utils)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      config_dir_path_(config_dir_path),
      cbi_utils_(std::move(cbi_utils)),
      cros_config_utils_(std::move(cros_config_utils)),
      write_protect_utils_(std::move(write_protect_utils)),
      regions_utils_(std::move(regions_utils)),
      vpd_utils_(std::move(vpd_utils)),
      segmentation_utils_(std::move(segmentation_utils)),
      rmad_config_utils_(std::move(rmad_config_utils)) {}

RmadErrorCode UpdateDeviceInfoStateHandler::InitializeState() {
  CHECK(cbi_utils_);
  CHECK(cros_config_utils_);
  CHECK(write_protect_utils_);
  CHECK(regions_utils_);
  CHECK(vpd_utils_);
  CHECK(segmentation_utils_);

  // Make sure HWWP is off before initializing the state.
  if (auto hwwp_enabled =
          write_protect_utils_->GetHardwareWriteProtectionStatus();
      !hwwp_enabled.has_value() || hwwp_enabled.value()) {
    return RMAD_ERROR_WP_ENABLED;
  }

  // Get rmad config first.
  if (!cros_config_utils_->GetRmadCrosConfig(&rmad_cros_config_)) {
    LOG(ERROR) << "Failed to get RMA config from cros_config";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  auto update_dev_info = std::make_unique<UpdateDeviceInfoState>();

  std::string serial_number;
  std::string region;
  uint32_t sku_id;
  bool is_custom_label_exist;
  std::string custom_label_tag;
  std::string dram_part_number;
  UpdateDeviceInfoState::FeatureLevel feature_level;

  std::vector<std::string> region_list;
  std::vector<DesignConfig> design_config_list;
  std::vector<uint32_t> sku_id_list;
  std::vector<std::string> sku_description_list;
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
    if (!rmad_cros_config_.has_cbi) {
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
  if (rmad_cros_config_.has_cbi &&
      !cbi_utils_->GetDramPartNum(&dram_part_number)) {
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

  if (!cros_config_utils_->GetDesignConfigList(&design_config_list)) {
    LOG(ERROR) << "Failed to get the list of possible design configs to "
                  "initialize the handler.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }
  // Remove design configs that the SKU ID is 0x7fffffff (unprovisioned SKU ID),
  // and then sort the list by SKU.
  design_config_list.erase(
      std::remove_if(design_config_list.begin(), design_config_list.end(),
                     [](const DesignConfig& config) {
                       return config.sku_id.has_value() &&
                              config.sku_id.value() == 0x7fffffff;
                     }),
      design_config_list.end());
  std::sort(design_config_list.begin(), design_config_list.end(),
            [](const DesignConfig& a, const DesignConfig& b) {
              return a.sku_id < b.sku_id;
            });
  // Construct |sku_id_list|, |sku_descripsion_list| and |custom_label_tag_list|
  // from |design_config_list|.
  GenerateSkuListsFromDesignConfigList(design_config_list, &sku_id_list,
                                       &sku_description_list);
  GenerateCustomLabelTagListFromDesignConfigList(design_config_list,
                                                 &custom_label_tag_list);

  if (!rmad_cros_config_.has_cbi) {
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
          [[fallthrough]];
        case 2:
          // TODO(jeffulin): Map case 2 to RMAD_FEATURE_LEVEL_2 when it is
          // introduced.
          feature_level = UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_1;
          break;
        default:
          feature_level = UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_0;
          NOTREACHED();
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

  for (const std::string& region_option : region_list) {
    update_dev_info->add_region_list(region_option);
  }

  for (uint32_t sku_option : sku_id_list) {
    update_dev_info->add_sku_list(static_cast<uint64_t>(sku_option));
  }
  for (const std::string& sku_description : sku_description_list) {
    update_dev_info->add_sku_description_list(sku_description);
  }

  for (const std::string& custom_label_option : custom_label_tag_list) {
    update_dev_info->add_whitelabel_list(custom_label_option);
    update_dev_info->add_custom_label_list(custom_label_option);
  }

  update_dev_info->set_mlb_repair(mlb_repair);

  SetFieldModifiabilities(update_dev_info.get());

  state_.set_allocated_update_device_info(update_dev_info.release());
  return RMAD_ERROR_OK;
}

void UpdateDeviceInfoStateHandler::GenerateSkuListsFromDesignConfigList(
    const std::vector<DesignConfig>& design_config_list,
    std::vector<uint32_t>* sku_id_list,
    std::vector<std::string>* sku_description_list) const {
  CHECK(sku_id_list);
  CHECK(sku_description_list);
  sku_id_list->clear();
  sku_description_list->clear();
  // Cache all the descriptions of all SKUs.
  std::vector<std::vector<std::string>> sku_property_descriptions;
  // SKU description overrides.
  auto description_override = GetSkuDescriptionOverrides();

  // |design_config_list| should be sorted by SKU ID.
  for (const DesignConfig& design_config : design_config_list) {
    // Skip empty and duplicate SKU IDs. Duplicate SKU IDs should only happen on
    // custom label devices, and theoretically, configs with the same SKU ID
    // should have the same hardware properties.
    // TODO(chenghan): Show (SKU ID, custom label tag) in pairs instead of
    //                 separate dropdown lists in the UX.
    if (!design_config.sku_id.has_value() ||
        (!sku_id_list->empty() &&
         sku_id_list->back() == design_config.sku_id.value())) {
      continue;
    }
    uint32_t sku_id = design_config.sku_id.value();
    if (!description_override.has_value()) {
      // No SKU filter. Show all SKUs and determine the hardware property
      // descriptions later.
      sku_id_list->push_back(sku_id);
      sku_property_descriptions.push_back(design_config.hardware_properties);
    } else if (auto it = description_override.value().find(sku_id);
               it != description_override.value().end()) {
      // Has SKU filter. Show the SKUs in the allowlist and override their
      // descriptions.
      sku_id_list->push_back(sku_id);
      sku_description_list->push_back(it->second);
    } else if (base::PathExists(GetTestDirPath())) {
      // Populate excluded SKUs in testing mode since DUTs in lab may not be in
      // OEMs' list.
      sku_id_list->push_back(sku_id);
      sku_description_list->push_back("");
    }
  }

  // Determine SKU descriptions if there is no description overrides, and there
  // are more than 1 SKUs.
  if (!description_override.has_value() && sku_id_list->size() > 1) {
    // Determine which columns to show in the description.
    const int num_properties = sku_property_descriptions[0].size();
    std::vector<bool> select_property(num_properties);
    for (int i = 0; i < num_properties; ++i) {
      // Only show the property if there are different values across SKUs.
      select_property[i] =
          HasDifferentElementsInColumn(sku_property_descriptions, i);
    }
    // Generate SKU descriptions.
    for (const std::vector<std::string>& descriptions :
         sku_property_descriptions) {
      sku_description_list->push_back(
          base::JoinString(FilterArray(descriptions, select_property), ", "));
    }
  }

  CHECK(sku_id_list->size() <= 1 ||
        sku_id_list->size() == sku_description_list->size());
}

void UpdateDeviceInfoStateHandler::
    GenerateCustomLabelTagListFromDesignConfigList(
        const std::vector<DesignConfig>& design_config_list,
        std::vector<std::string>* custom_label_tag_list) const {
  CHECK(custom_label_tag_list);
  custom_label_tag_list->clear();
  for (const DesignConfig& design_config : design_config_list) {
    // Custom label tag might not exist.
    if (design_config.custom_label_tag.has_value()) {
      custom_label_tag_list->push_back(design_config.custom_label_tag.value());
    } else {
      custom_label_tag_list->push_back("");
    }
  }
  // Sort and remove duplicate custom label tags.
  std::sort(custom_label_tag_list->begin(), custom_label_tag_list->end());
  custom_label_tag_list->erase(
      std::unique(custom_label_tag_list->begin(), custom_label_tag_list->end()),
      custom_label_tag_list->end());
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
    if (auto hwwp_enabled =
            write_protect_utils_->GetHardwareWriteProtectionStatus();
        !hwwp_enabled.has_value() || hwwp_enabled.value()) {
      return NextStateCaseWrapper(RMAD_ERROR_WP_ENABLED);
    }
    return NextStateCaseWrapper(RMAD_ERROR_CANNOT_WRITE);
  }

  state_ = state;

  return NextStateCaseWrapper(RmadState::StateCase::kUpdateRoFirmware);
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

  if (rmad_cros_config_.has_cbi &&
      device_info.sku_index() != device_info.original_sku_index() &&
      !cbi_utils_->SetSkuId(static_cast<uint32_t>(
          device_info.sku_list(device_info.sku_index())))) {
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
      !vpd_utils_->SetCustomLabelTag(
          custom_label_tag, rmad_cros_config_.use_legacy_custom_label)) {
    LOG(ERROR) << "Failed to save custom_label_tag to vpd cache.";
    return false;
  }

  if (rmad_cros_config_.has_cbi &&
      device_info.dram_part_number() !=
          device_info.original_dram_part_number() &&
      !cbi_utils_->SetDramPartNum(device_info.dram_part_number())) {
    LOG(ERROR) << "Failed to write dram part number to cbi.";
    return false;
  }

  if (device_info.original_feature_level() ==
      UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_UNKNOWN) {
    // Provision |is_chassis_branded| and |hw_compliance_version|.
    int compliance_version = device_info.hw_compliance_version();
    // The |hw_compliance_version| field in the user input is expected to be
    // either 0 or 1. A value of 1 indicates the need to update the device to
    // the appropriate feature level.
    if (const auto feature_level = segmentation_utils_->LookUpFeatureLevel();
        compliance_version == 1 && feature_level.has_value()) {
      compliance_version = feature_level.value();
    }

    segmentation_utils_->SetFeatureFlags(device_info.is_chassis_branded(),
                                         compliance_version);
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

std::optional<SkuFilter> UpdateDeviceInfoStateHandler::GetSkuFilter() const {
  std::string model_name;
  if (!cros_config_utils_->GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name";
    return std::nullopt;
  }

  const base::FilePath textproto_file_path =
      config_dir_path_.Append(model_name).Append(kSkuFilterProtoFilePath);
  if (!base::PathExists(textproto_file_path)) {
    // This is expected for projects that don't use SKU filter.
    return std::nullopt;
  }

  std::string textproto;
  if (!base::ReadFileToString(textproto_file_path, &textproto)) {
    LOG(ERROR) << "Failed to read " << textproto_file_path.value();
    return std::nullopt;
  }

  SkuFilter sku_filter;
  if (!google::protobuf::TextFormat::ParseFromString(textproto, &sku_filter)) {
    LOG(ERROR) << "Failed to parse SKU filter list";
    return std::nullopt;
  }

  return sku_filter;
}

std::optional<std::unordered_map<uint32_t, std::string>>
UpdateDeviceInfoStateHandler::GetSkuDescriptionOverrides() const {
  std::optional<SkuFilter> sku_filter = GetSkuFilter();
  if (!sku_filter.has_value() || sku_filter.value().sku_list().empty()) {
    return std::nullopt;
  }

  std::unordered_map<uint32_t, std::string> sku_description_map;
  for (const SkuWithDescription& entry : sku_filter.value().sku_list()) {
    uint32_t sku = entry.sku();
    if (sku_description_map.find(sku) == sku_description_map.end()) {
      if (entry.has_description()) {
        sku_description_map[sku] = entry.description();
      } else {
        sku_description_map[sku] = "";
      }
    } else {
      LOG(WARNING) << "Duplicate SKU " << sku << " found in the filter";
    }
  }
  return sku_description_map;
}

bool UpdateDeviceInfoStateHandler::IsSpareMlb() const {
  bool spare_mlb = false;
  return json_store_->GetValue(kSpareMlb, &spare_mlb) && spare_mlb;
}

void UpdateDeviceInfoStateHandler::SetFieldModifiabilities(
    UpdateDeviceInfoState* update_dev_info) {
  // All the input fields are modifiable by default.
  update_dev_info->set_serial_number_modifiable(true);
  update_dev_info->set_region_modifiable(true);
  update_dev_info->set_sku_modifiable(true);
  update_dev_info->set_whitelabel_modifiable(true);
  update_dev_info->set_dram_part_number_modifiable(true);
  update_dev_info->set_custom_label_modifiable(true);
  update_dev_info->set_feature_level_modifiable(true);

  if (auto rmad_config = rmad_config_utils_->GetConfig();
      !rmad_config.has_value() || !rmad_config->dynamic_device_info_inputs()) {
    return;
  }

  // With dynamic input field config set to be true, DRAM part number and
  // Custom-label are greyed out.
  update_dev_info->set_dram_part_number_modifiable(false);
  update_dev_info->set_whitelabel_modifiable(false);
  update_dev_info->set_custom_label_modifiable(false);

  if (!IsSpareMlb()) {
    // If it is not a spare MLB case, further grey out the followings:
    // 1. Serial Number
    // 2. SKU
    // 3. Feature Level
    update_dev_info->set_serial_number_modifiable(false);
    update_dev_info->set_sku_modifiable(false);
    update_dev_info->set_feature_level_modifiable(false);
  }
}

}  // namespace rmad
