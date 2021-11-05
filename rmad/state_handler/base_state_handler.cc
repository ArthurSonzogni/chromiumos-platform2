// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/base64.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_constants.h"

namespace rmad {

BaseStateHandler::BaseStateHandler(scoped_refptr<JsonStore> json_store)
    : json_store_(json_store) {}

const RmadState& BaseStateHandler::GetState() const {
  return state_;
}

bool BaseStateHandler::StoreState() {
  std::map<std::string, std::string> state_map;
  json_store_->GetValue(kStateMap, &state_map);

  std::string key = base::NumberToString(GetStateCase());
  std::string serialized_string, serialized_string_base64;
  state_.SerializeToString(&serialized_string);
  base::Base64Encode(serialized_string, &serialized_string_base64);

  state_map[key] = serialized_string_base64;
  return json_store_->SetValue(kStateMap, state_map);
}

bool BaseStateHandler::RetrieveState() {
  if (std::map<std::string, std::string> state_map;
      json_store_->GetValue(kStateMap, &state_map)) {
    std::string key = base::NumberToString(GetStateCase());
    auto it = state_map.find(key);
    if (it != state_map.end()) {
      std::string serialized_string;
      DCHECK(base::Base64Decode(it->second, &serialized_string));
      return state_.ParseFromString(serialized_string);
    }
  }
  return false;
}

BaseStateHandler::GetNextStateCaseReply BaseStateHandler::NextStateCaseWrapper(
    RmadState::StateCase state_case,
    RmadErrorCode error,
    AdditionalActivity activity) {
  if (!StoreErrorCode(error)) {
    LOG(ERROR) << "Failed to store the error code to |json_store_|.";
  }

  if (!StoreAdditionalActivity(activity)) {
    LOG(ERROR) << "Failed to store the additional activity to |json_store_|.";
  }

  return {.error = error, .state_case = state_case};
}

BaseStateHandler::GetNextStateCaseReply BaseStateHandler::NextStateCaseWrapper(
    RmadState::StateCase state_case) {
  return NextStateCaseWrapper(state_case, RMAD_ERROR_OK,
                              AdditionalActivity::NOTHING);
}

BaseStateHandler::GetNextStateCaseReply BaseStateHandler::NextStateCaseWrapper(
    RmadErrorCode error) {
  return NextStateCaseWrapper(GetStateCase(), error,
                              AdditionalActivity::NOTHING);
}

bool BaseStateHandler::StoreErrorCode(RmadErrorCode error) {
  // If this is expected, then we do nothing.
  if (std::find(kExpectedErrorCodes.begin(), kExpectedErrorCodes.end(),
                error) != kExpectedErrorCodes.end()) {
    return true;
  }

  std::vector<std::string> occurred_errors;
  // Ignore the return value, since it may not have been set yet.
  json_store_->GetValue(kOccurredErrors, &occurred_errors);
  occurred_errors.push_back(RmadErrorCode_Name(error));

  return json_store_->SetValue(kOccurredErrors, occurred_errors);
}

bool BaseStateHandler::StoreAdditionalActivity(AdditionalActivity activity) {
  if (AdditionalActivity::NOTHING == activity) {
    return true;
  }

  std::vector<int> additional_activities;
  // Ignore the return value, since it may not have been set yet.
  json_store_->GetValue(kAdditionalActivities, &additional_activities);
  additional_activities.push_back(static_cast<int>(activity));

  // For those expected power cycle events, we calculate running time and append
  // it to the |json_store_| for metrics.
  if (std::find(kExpectedPowerCycleActivities.begin(),
                kExpectedPowerCycleActivities.end(),
                activity) != kExpectedPowerCycleActivities.end()) {
    double current_timestamp = base::Time::Now().ToDoubleT();
    double setup_timestamp;
    if (!json_store_->GetValue(kSetupTimestamp, &setup_timestamp)) {
      LOG(ERROR) << "Failed to get setup timestamp for measuring "
                    "running time.";
      return false;
    }

    double running_time = 0;
    // Ignore the return value, since it may not have been set yet.
    json_store_->GetValue(kRunningTime, &running_time);
    running_time += current_timestamp - setup_timestamp;
    // Once we increase the running time, we should also update the timestamp to
    // prevent double counting issues.
    if (!json_store_->SetValue(kRunningTime, running_time) ||
        !json_store_->SetValue(kSetupTimestamp, current_timestamp)) {
      LOG(ERROR) << "Failed to set running time for metrics.";
      return false;
    }
  }

  return json_store_->SetValue(kAdditionalActivities, additional_activities);
}

}  // namespace rmad
