// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_UPDATE_DEVICE_INFO_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_UPDATE_DEVICE_INFO_STATE_HANDLER_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/file_path.h>

#include "rmad/segmentation/segmentation_utils.h"
#include "rmad/sku_filter.pb.h"
#include "rmad/state_handler/base_state_handler.h"
#include "rmad/utils/cbi_utils.h"
#include "rmad/utils/cros_config_utils.h"
#include "rmad/utils/regions_utils.h"
#include "rmad/utils/rmad_config_utils.h"
#include "rmad/utils/vpd_utils.h"
#include "rmad/utils/write_protect_utils.h"

namespace rmad {

class UpdateDeviceInfoStateHandler : public BaseStateHandler {
 public:
  explicit UpdateDeviceInfoStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mock |working_dir_path|, |config_dir_path|, |cbi_utils_|,
  // |cros_config_utils_|, |write_protect_utils_|, |regions_utils_|,
  // |vpd_utils_|, |segmentation_utils_|, and |rmad_config_utils_| for testing.
  explicit UpdateDeviceInfoStateHandler(
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
      std::unique_ptr<RmadConfigUtils> rmad_config_utils);

  ASSIGN_STATE(RmadState::StateCase::kUpdateDeviceInfo);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~UpdateDeviceInfoStateHandler() override = default;

 private:
  void GenerateSkuListsFromDesignConfigList(
      const std::vector<DesignConfig>& design_config_list,
      std::vector<uint32_t>* sku_id_list,
      std::vector<std::string>* sku_description_list) const;
  void GenerateCustomLabelTagListFromDesignConfigList(
      const std::vector<DesignConfig>& design_config_list,
      std::vector<std::string>* custom_label_tag_list) const;
  bool VerifyReadOnly(const UpdateDeviceInfoState& device_info);
  bool WriteDeviceInfo(const UpdateDeviceInfoState& device_info);
  std::unique_ptr<SegmentationUtils> CreateFakeSegmentationUtils() const;
  base::FilePath GetTestDirPath() const;
  base::FilePath GetFakeFeaturesInputFilePath() const;
  std::optional<SkuFilter> GetSkuFilter() const;
  std::optional<std::unordered_map<uint32_t, std::string>>
  GetSkuDescriptionOverrides() const;
  bool IsSpareMlb() const;
  void SetFieldModifiabilities(UpdateDeviceInfoState* update_dev_info);
  void SetCustomizedSnName(UpdateDeviceInfoState* update_dev_info);
  void SetGoogleSkuVisibility(UpdateDeviceInfoState* update_dev_info);

  RmadCrosConfig rmad_cros_config_;

  base::FilePath working_dir_path_;
  base::FilePath config_dir_path_;
  std::unique_ptr<CbiUtils> cbi_utils_;
  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
  std::unique_ptr<WriteProtectUtils> write_protect_utils_;
  std::unique_ptr<RegionsUtils> regions_utils_;
  std::unique_ptr<VpdUtils> vpd_utils_;
  std::unique_ptr<SegmentationUtils> segmentation_utils_;
  std::unique_ptr<RmadConfigUtils> rmad_config_utils_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_UPDATE_DEVICE_INFO_STATE_HANDLER_H_
