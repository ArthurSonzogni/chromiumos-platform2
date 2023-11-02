//
// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_UPDATER_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_UPDATER_PROVIDER_H_

#include <string>

#include "update_engine/update_manager/fake_variable.h"
#include "update_engine/update_manager/updater_provider.h"

namespace chromeos_update_manager {

// Fake implementation of the UpdaterProvider base class.
class FakeUpdaterProvider : public UpdaterProvider {
 public:
  FakeUpdaterProvider() {}
  FakeUpdaterProvider(const FakeUpdaterProvider&) = delete;
  FakeUpdaterProvider& operator=(const FakeUpdaterProvider&) = delete;

  FakeVariable<base::Time>* var_updater_started_time() override {
    return &var_updater_started_time_;
  }

  FakeVariable<base::Time>* var_last_checked_time() override {
    return &var_last_checked_time_;
  }

  FakeVariable<base::Time>* var_update_completed_time() override {
    return &var_update_completed_time_;
  }

  FakeVariable<double>* var_progress() override { return &var_progress_; }

  FakeVariable<Stage>* var_stage() override { return &var_stage_; }

  FakeVariable<std::string>* var_new_version() override {
    return &var_new_version_;
  }

  FakeVariable<uint64_t>* var_payload_size() override {
    return &var_payload_size_;
  }

  FakeVariable<std::string>* var_curr_channel() override {
    return &var_curr_channel_;
  }

  FakeVariable<std::string>* var_new_channel() override {
    return &var_new_channel_;
  }

  FakeVariable<bool>* var_p2p_enabled() override { return &var_p2p_enabled_; }

  FakeVariable<bool>* var_cellular_enabled() override {
    return &var_cellular_enabled_;
  }

  FakeVariable<bool>* var_market_segment_disabled() override {
    return &var_market_segment_disabled_;
  }

  FakeVariable<unsigned int>* var_consecutive_failed_update_checks() override {
    return &var_consecutive_failed_update_checks_;
  }

  FakeVariable<unsigned int>* var_server_dictated_poll_interval() override {
    return &var_server_dictated_poll_interval_;
  }

  FakeVariable<UpdateRequestStatus>* var_forced_update_requested() override {
    return &var_forced_update_requested_;
  }

  FakeVariable<int64_t>* var_test_update_check_interval_timeout() override {
    return &var_test_update_check_interval_timeout_;
  }

  FakeVariable<bool>* var_consumer_auto_update_disabled() override {
    return &var_consumer_auto_update_disabled_;
  }

 private:
  FakeVariable<base::Time> var_updater_started_time_{"updater_started_time",
                                                     kVariableModePoll};
  FakeVariable<base::Time> var_last_checked_time_{"last_checked_time",
                                                  kVariableModePoll};
  FakeVariable<base::Time> var_update_completed_time_{"update_completed_time",
                                                      kVariableModePoll};
  FakeVariable<double> var_progress_{"progress", kVariableModePoll};
  FakeVariable<Stage> var_stage_{"stage", kVariableModePoll};
  FakeVariable<std::string> var_new_version_{"new_version", kVariableModePoll};
  FakeVariable<uint64_t> var_payload_size_{"payload_size", kVariableModePoll};
  FakeVariable<std::string> var_curr_channel_{"curr_channel",
                                              kVariableModePoll};
  FakeVariable<std::string> var_new_channel_{"new_channel", kVariableModePoll};
  FakeVariable<bool> var_p2p_enabled_{"p2p_enabled", kVariableModeAsync};
  FakeVariable<bool> var_cellular_enabled_{"cellular_enabled",
                                           kVariableModeAsync};
  FakeVariable<bool> var_market_segment_disabled_{"market_segment_disabled",
                                                  kVariableModeAsync};
  FakeVariable<unsigned int> var_consecutive_failed_update_checks_{
      "consecutive_failed_update_checks", kVariableModePoll};
  FakeVariable<unsigned int> var_server_dictated_poll_interval_{
      "server_dictated_poll_interval", kVariableModePoll};
  FakeVariable<UpdateRequestStatus> var_forced_update_requested_{
      "forced_update_requested", kVariableModeAsync};
  FakeVariable<int64_t> var_test_update_check_interval_timeout_{
      "test_update_check_interval_timeout", kVariableModePoll};
  FakeVariable<bool> var_consumer_auto_update_disabled_{
      "consumer_auto_update_disabled", kVariableModeAsync};
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_UPDATER_PROVIDER_H_
