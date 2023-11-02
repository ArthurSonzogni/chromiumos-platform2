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

#include "update_engine/cros/real_system_state.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/constants/imageloader.h>
#include <chromeos/dbus/service_constants.h>

#include "update_engine/common/boot_control.h"
#include "update_engine/common/boot_control_stub.h"
#include "update_engine/common/constants.h"
#include "update_engine/common/dlcservice_interface.h"
#include "update_engine/common/hardware.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/dbus_connection.h"
#include "update_engine/cros/metrics_reporter_omaha.h"
#include "update_engine/update_manager/omaha_request_params_policy.h"
#include "update_engine/update_manager/state_factory.h"

namespace chromeos_update_engine {

bool RealSystemState::Initialize() {
  hardware_ = hardware::CreateHardware();
  if (!hardware_) {
    LOG(ERROR) << "Error initializing the HardwareInterface.";
    return false;
  }

  boot_control_ = boot_control::CreateBootControl();
  if (!boot_control_) {
    LOG(WARNING) << "Unable to create BootControl instance, using stub "
                 << "instead. All update attempts will fail.";
    boot_control_ = std::make_unique<BootControlStub>();
  }

  kiosk_app_proxy_.reset(new org::chromium::KioskAppServiceInterfaceProxy(
      DBusConnection::Get()->GetDBus(), chromeos::kKioskAppServiceName));

  LOG_IF(INFO, !hardware_->IsNormalBootMode()) << "Booted in dev mode.";
  LOG_IF(INFO, !hardware_->IsOfficialBuild()) << "Booted non-official build.";

  connection_manager_ = connection_manager::CreateConnectionManager();
  if (!connection_manager_) {
    LOG(ERROR) << "Error initializing the ConnectionManagerInterface.";
    return false;
  }

  power_manager_ = power_manager::CreatePowerManager();
  if (!power_manager_) {
    LOG(ERROR) << "Error initializing the PowerManagerInterface.";
    return false;
  }

  dlcservice_ = CreateDlcService();
  if (!dlcservice_) {
    LOG(ERROR) << "Error initializing the DlcServiceInterface.";
    return false;
  }

  dlc_utils_ = CreateDlcUtils();
  if (!dlc_utils_) {
    LOG(ERROR) << "Error initializing the DlcUtilsInterface.";
    return false;
  }

  cros_healthd_ = CreateCrosHealthd();
  if (!cros_healthd_) {
    LOG(ERROR) << "Error initializing the CrosHealthdInterface,";
    return false;
  }

  call_wrapper_ = CreateCallWrapper();
  if (!call_wrapper_) {
    LOG(ERROR) << "Error initializing the CallWrapperInterface.";
    return false;
  }

  hibernate_ = CreateHibernateService();
  if (!hibernate_) {
    LOG(ERROR) << "Error initializing the HibernateInterface";
    return false;
  }

  // Initialize standard and powerwash-safe prefs.
  base::FilePath non_volatile_path;
  // TODO(deymo): Fall back to in-memory prefs if there's no physical directory
  // available.
  if (!hardware_->GetNonVolatileDirectory(&non_volatile_path)) {
    LOG(ERROR) << "Failed to get a non-volatile directory.";
    return false;
  }
  Prefs* prefs;
  prefs_.reset(prefs = new Prefs());
  if (!prefs->Init(non_volatile_path.Append(kPrefsSubDirectory))) {
    LOG(ERROR) << "Failed to initialize preferences.";
    return false;
  }

  base::FilePath powerwash_safe_path;
  if (!hardware_->GetPowerwashSafeDirectory(&powerwash_safe_path)) {
    // TODO(deymo): Fall-back to in-memory prefs if there's no powerwash-safe
    // directory, or disable powerwash feature.
    powerwash_safe_path = non_volatile_path.Append("powerwash-safe");
    LOG(WARNING) << "No powerwash-safe directory, using non-volatile one.";
  }
  powerwash_safe_prefs_.reset(prefs = new Prefs());
  if (!prefs->Init(
          powerwash_safe_path.Append(kPowerwashSafePrefsSubDirectory))) {
    LOG(ERROR) << "Failed to initialize powerwash preferences.";
    return false;
  }

  // Check the system rebooted marker file.
  std::string boot_id;
  if (utils::GetBootId(&boot_id)) {
    std::string prev_boot_id;
    system_rebooted_ = (!prefs_->GetString(kPrefsBootId, &prev_boot_id) ||
                        prev_boot_id != boot_id);
    prefs_->SetString(kPrefsBootId, boot_id);
  } else {
    LOG(WARNING) << "Couldn't detect the bootid, assuming system was rebooted.";
    system_rebooted_ = true;
  }

  // Initialize the OmahaRequestParams with the default settings. These settings
  // will be re-initialized before every request using the actual request
  // options. This initialization here pre-loads current channel and version, so
  // the DBus service can access it.
  if (!request_params_.Init("", "", {})) {
    LOG(WARNING) << "Ignoring OmahaRequestParams initialization error. Some "
                    "features might not work properly.";
  }

  certificate_checker_.reset(
      new CertificateChecker(prefs_.get(), &openssl_wrapper_));
  certificate_checker_->Init();

  update_attempter_.reset(new UpdateAttempter(certificate_checker_.get()));

  // Initialize the UpdateAttempter before the UpdateManager.
  update_attempter_->Init();

  // Initialize the Update Manager using the default state factory.
  chromeos_update_manager::State* um_state =
      chromeos_update_manager::DefaultStateFactory(&policy_provider_,
                                                   kiosk_app_proxy_.get());

  if (!um_state) {
    LOG(ERROR) << "Failed to initialize the Update Manager.";
    return false;
  }
  update_manager_.reset(new chromeos_update_manager::UpdateManager(
      base::Seconds(5), base::Hours(12), um_state));

  // The P2P Manager depends on the Update Manager for its initialization.
  p2p_manager_.reset(P2PManager::Construct(nullptr,
                                           update_manager_.get(),
                                           "cros_au",
                                           kMaxP2PFilesToKeep,
                                           kMaxP2PFileAge));

  if (!payload_state_.Initialize()) {
    LOG(ERROR) << "Failed to initialize the payload state object.";
    return false;
  }

  std::optional<int> rollback_allowed_milestones;
  bool consumer_owned = true;
  update_attempter_->RefreshDevicePolicy();
  const policy::DevicePolicy* policy = device_policy();
  if (policy) {
    if (int policy_value; policy->GetRollbackAllowedMilestones(&policy_value)) {
      rollback_allowed_milestones = policy_value;
    }
    consumer_owned = !policy->IsEnterpriseEnrolled();
  }

  // Set max kernel key version to `kRollforwardInfinity`.
  // On non-official builds, consumer devices, or if rollback is disabled via
  // the allowed milestone policy, kernel key version should not be restricted.
  if (!hardware_->IsOfficialBuild() || rollback_allowed_milestones == 0 ||
      consumer_owned) {
    if (!hardware()->SetMaxKernelKeyRollforward(
            chromeos_update_manager::kRollforwardInfinity)) {
      LOG(ERROR) << "Failed to set kernel_max_rollforward to infinity for"
                 << " device with test/dev image.";
    }
  }

  // All is well. Initialization successful.
  return true;
}

}  // namespace chromeos_update_engine
