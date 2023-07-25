// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_UPDATE_DEVICE_INFO_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_UPDATE_DEVICE_INFO_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>

#include "rmad/segmentation/segmentation_utils.h"
#include "rmad/utils/cbi_utils.h"
#include "rmad/utils/cros_config_utils.h"
#include "rmad/utils/regions_utils.h"
#include "rmad/utils/vpd_utils.h"
#include "rmad/utils/write_protect_utils.h"

namespace rmad {

class UpdateDeviceInfoStateHandler : public BaseStateHandler {
 public:
  explicit UpdateDeviceInfoStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mock |working_dir_path|, |cbi_utils_|, |cros_config_utils_|,
  // |write_protect_utils_|, |regions_utils_|, |vpd_utils_| and
  // |segmentation_utils_| for testing.
  explicit UpdateDeviceInfoStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      const base::FilePath& working_dir_path,
      std::unique_ptr<CbiUtils> cbi_utils,
      std::unique_ptr<CrosConfigUtils> cros_config_utils,
      std::unique_ptr<WriteProtectUtils> write_protect_utils,
      std::unique_ptr<RegionsUtils> regions_utils,
      std::unique_ptr<VpdUtils> vpd_utils,
      std::unique_ptr<SegmentationUtils> segmentation_utils);

  ASSIGN_STATE(RmadState::StateCase::kUpdateDeviceInfo);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~UpdateDeviceInfoStateHandler() override = default;

 private:
  bool VerifyReadOnly(const UpdateDeviceInfoState& device_info);
  bool WriteDeviceInfo(const UpdateDeviceInfoState& device_info);
  std::unique_ptr<SegmentationUtils> CreateFakeSegmentationUtils() const;
  base::FilePath GetTestDirPath() const;
  base::FilePath GetFakeFeaturesInputFilePath() const;

  RmadConfig rmad_config_;

  base::FilePath working_dir_path_;
  std::unique_ptr<CbiUtils> cbi_utils_;
  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
  std::unique_ptr<WriteProtectUtils> write_protect_utils_;
  std::unique_ptr<RegionsUtils> regions_utils_;
  std::unique_ptr<VpdUtils> vpd_utils_;
  std::unique_ptr<SegmentationUtils> segmentation_utils_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_UPDATE_DEVICE_INFO_STATE_HANDLER_H_
