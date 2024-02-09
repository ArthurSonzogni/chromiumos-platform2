// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CHECK_ALLOWED_POLICY_DATA_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CHECK_ALLOWED_POLICY_DATA_H_

#include <string>
#include <utility>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Parameters of an update check. These parameters are determined by the
// UpdateCheckAllowed policy.
struct UpdateCheckParams {
  // Whether the auto-updates are enabled on this build.
  bool updates_enabled{true};

  // Attributes pertaining to the case where update checks are allowed.
  //
  // A target version prefix, if imposed by policy; otherwise, an empty string.
  std::string target_version_prefix;
  // Whether a rollback with data save should be initiated on channel
  // downgrade (e.g. beta to stable).
  bool rollback_on_channel_downgrade{false};
  // A target channel, if so imposed by policy; otherwise, an empty string.
  std::string target_channel;

  // Whether the allowed update is interactive (user-initiated) or periodic.
  bool interactive{false};

  // Forces a fw update with OS update.
  bool force_fw_update{false};
};

class UpdateCheckAllowedPolicyData : public PolicyDataInterface {
 public:
  UpdateCheckAllowedPolicyData() = default;
  explicit UpdateCheckAllowedPolicyData(UpdateCheckParams params)
      : update_check_params(std::move(params)) {}
  virtual ~UpdateCheckAllowedPolicyData() = default;

  UpdateCheckAllowedPolicyData(const UpdateCheckAllowedPolicyData&) = delete;
  UpdateCheckAllowedPolicyData& operator=(const UpdateCheckAllowedPolicyData&) =
      delete;

  // Helper function to convert |PolicyDataInterface| into proper data type.
  static UpdateCheckParams* GetUpdateCheckParams(PolicyDataInterface* data) {
    return &(
        static_cast<UpdateCheckAllowedPolicyData*>(data)->update_check_params);
  }

  UpdateCheckParams update_check_params;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_CHECK_ALLOWED_POLICY_DATA_H_
