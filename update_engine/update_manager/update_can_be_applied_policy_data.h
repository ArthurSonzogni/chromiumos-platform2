// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CAN_BE_APPLIED_POLICY_DATA_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CAN_BE_APPLIED_POLICY_DATA_H_

#include "update_engine/common/error_code.h"
#include "update_engine/payload_consumer/install_plan.h"
#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

class UpdateCanBeAppliedPolicyData : public PolicyDataInterface {
 public:
  explicit UpdateCanBeAppliedPolicyData(
      chromeos_update_engine::InstallPlan* install_plan)
      : install_plan_(install_plan),
        error_code_(chromeos_update_engine::ErrorCode::kSuccess) {}
  virtual ~UpdateCanBeAppliedPolicyData() = default;

  UpdateCanBeAppliedPolicyData(const UpdateCanBeAppliedPolicyData&) = delete;
  UpdateCanBeAppliedPolicyData& operator=(const UpdateCanBeAppliedPolicyData&) =
      delete;

  chromeos_update_engine::InstallPlan* install_plan() const {
    return install_plan_;
  }

  void set_error_code(chromeos_update_engine::ErrorCode error_code) {
    error_code_ = error_code;
  }
  chromeos_update_engine::ErrorCode error_code() const { return error_code_; }

 private:
  chromeos_update_engine::InstallPlan* install_plan_;
  chromeos_update_engine::ErrorCode error_code_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CAN_BE_APPLIED_POLICY_DATA_H_
