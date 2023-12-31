//
// Copyright (C) 2012 The Android Open Source Project
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

#include "update_engine/cros/fake_system_state.h"

namespace chromeos_update_engine {

// Mock the SystemStateInterface so that we could lie that
// OOBE is completed even when there's no such marker file, etc.
FakeSystemState::FakeSystemState()
    : mock_update_attempter_(nullptr),
      clock_(&fake_clock_),
      connection_manager_(&mock_connection_manager_),
      hardware_(&fake_hardware_),
      metrics_reporter_(&mock_metrics_reporter_),
      prefs_(&fake_prefs_),
      powerwash_safe_prefs_(&fake_powerwash_safe_prefs_),
      payload_state_(&mock_payload_state_),
      update_attempter_(&mock_update_attempter_),
      request_params_(&mock_request_params_),
      p2p_manager_(&mock_p2p_manager_),
      update_manager_(&fake_update_manager_),
      call_wrapper_(&mock_call_wrapper_),
      dlc_utils_(&mock_dlc_utils_),
      device_policy_(nullptr),
      fake_system_rebooted_(false) {
  mock_payload_state_.Initialize();
}

}  // namespace chromeos_update_engine
