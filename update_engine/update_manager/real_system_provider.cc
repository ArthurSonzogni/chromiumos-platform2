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

#include "update_engine/update_manager/real_system_provider.h"

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <kiosk-app/dbus-proxies.h>

#include "update_engine/common/boot_control_interface.h"
#include "update_engine/common/hardware_interface.h"
#include "update_engine/common/hibernate_interface.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/cros/update_attempter.h"
#include "update_engine/update_manager/generic_variables.h"
#include "update_engine/update_manager/variable.h"

using chromeos_update_engine::SystemState;
using std::string;

namespace chromeos_update_manager {

namespace {

// The maximum number of consecutive failures before returning the default
// constructor value for T instead of failure.
const int kRetryPollVariableMaxRetry = 5;

// The polling interval to be used whenever GetValue() returns an error.
const base::TimeDelta kRetryPollVariableRetryInterval = base::Minutes(5);

// The RetryPollVariable variable is a polling variable that allows the function
// returning the value to fail a few times and shortens the polling rate when
// that happens.
template <typename T>
class RetryPollVariable : public Variable<T> {
 public:
  RetryPollVariable(const string& name,
                    const base::TimeDelta poll_interval,
                    base::RepeatingCallback<bool(T* res)> func)
      : Variable<T>(name, poll_interval),
        func_(func),
        base_interval_(poll_interval) {
    DCHECK_LT(kRetryPollVariableRetryInterval, base_interval_);
  }
  RetryPollVariable(const RetryPollVariable&) = delete;
  RetryPollVariable& operator=(const RetryPollVariable&) = delete;

 protected:
  // Variable override.
  const T* GetValue(base::TimeDelta /* timeout */,
                    string* /* errmsg */) override {
    std::unique_ptr<T> result(new T());
    if (!func_.Run(result.get())) {
      if (failed_attempts_ >= kRetryPollVariableMaxRetry) {
        // Give up on the retries and set back the desired polling interval.
        this->SetPollInterval(base_interval_);
        // Release the result instead of returning a |nullptr| to indicate that
        // the result could not be fetched.
        return result.release();
      }
      this->SetPollInterval(kRetryPollVariableRetryInterval);
      failed_attempts_++;
      return nullptr;
    }
    failed_attempts_ = 0;
    this->SetPollInterval(base_interval_);
    return result.release();
  }

 private:
  // The function to be called, stored as a base::Callback.
  base::RepeatingCallback<bool(T*)> func_;

  // The desired polling interval when |func_| works and returns true.
  base::TimeDelta base_interval_;

  // The number of consecutive failed attempts made.
  int failed_attempts_ = 0;
};

}  // namespace

bool RealSystemProvider::Init() {
  var_is_normal_boot_mode_.reset(new ConstCopyVariable<bool>(
      "is_normal_boot_mode",
      SystemState::Get()->hardware()->IsNormalBootMode()));

  var_is_official_build_.reset(new ConstCopyVariable<bool>(
      "is_official_build", SystemState::Get()->hardware()->IsOfficialBuild()));

  var_is_oobe_complete_.reset(new CallCopyVariable<bool>(
      "is_oobe_complete",
      base::BindRepeating(
          &chromeos_update_engine::HardwareInterface::IsOOBEComplete,
          base::Unretained(SystemState::Get()->hardware()),
          nullptr)));

  var_num_slots_.reset(new ConstCopyVariable<unsigned int>(
      "num_slots", SystemState::Get()->boot_control()->GetNumSlots()));

  var_kiosk_required_platform_version_.reset(new RetryPollVariable<string>(
      "kiosk_required_platform_version",
      base::Hours(5),  // Same as Chrome's CWS poll.
      base::BindRepeating(
          &RealSystemProvider::GetKioskAppRequiredPlatformVersion,
          base::Unretained(this))));

  var_chromeos_version_.reset(new ConstCopyVariable<base::Version>(
      "chromeos_version",
      base::Version(SystemState::Get()->request_params()->app_version())));

  var_is_updating_.reset(new CallCopyVariable<bool>(
      "is_updating",
      base::BindRepeating(
          &chromeos_update_engine::UpdateAttempter::IsUpdating,
          base::Unretained(SystemState::Get()->update_attempter()))));

  var_is_resuming_from_hibernate_.reset(new CallCopyVariable<bool>(
      "is_resuming_from_hibernate",
      base::BindRepeating(
          &chromeos_update_engine::HibernateInterface::IsResuming,
          base::Unretained(SystemState::Get()->hibernate()))));

  var_abort_resume_from_hibernate_.reset(new CallCopyVariable<bool>(
      "abort_resume_from_hibernate",
      base::BindRepeating(
          &chromeos_update_engine::HibernateInterface::AbortResume,
          base::Unretained(SystemState::Get()->hibernate()),
          "System update pending for too long")));

  return true;
}

bool RealSystemProvider::GetKioskAppRequiredPlatformVersion(
    string* required_platform_version) {
  brillo::ErrorPtr error;
  if (!kiosk_app_proxy_->GetRequiredPlatformVersion(required_platform_version,
                                                    &error)) {
    LOG(WARNING) << "Failed to get kiosk required platform version";
    required_platform_version->clear();
    return false;
  }

  return true;
}

}  // namespace chromeos_update_manager
