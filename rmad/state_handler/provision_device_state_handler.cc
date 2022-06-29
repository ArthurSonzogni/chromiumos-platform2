// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

#include <openssl/rand.h>

#include <algorithm>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/notreached.h>
#include <base/synchronization/lock.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"
#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/cbi_utils_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_cbi_utils.h"
#include "rmad/utils/fake_cros_config_utils.h"
#include "rmad/utils/fake_crossystem_utils.h"
#include "rmad/utils/fake_iio_sensor_probe_utils.h"
#include "rmad/utils/fake_ssfc_utils.h"
#include "rmad/utils/fake_vpd_utils.h"
#include "rmad/utils/iio_sensor_probe_utils_impl.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/ssfc_utils_impl.h"
#include "rmad/utils/vpd_utils_impl.h"

namespace {

constexpr int kStableDeviceSecretSize = 32;

constexpr double kProgressComplete = 1.0;
// TODO(chenghan): Uncomment this when we have a non-blocking error.
// constexpr double kProgressFailedNonblocking = -1.0;
constexpr double kProgressFailedBlocking = -2.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressGetDestination = 0.3;
constexpr double kProgressGetModelName = 0.5;
constexpr double kProgressGetSSFC = 0.7;
constexpr double kProgressWriteSSFC = 0.8;
constexpr double kProgressUpdateStableDeviceSecret = 0.9;
constexpr double kProgressFlushOutVpdCache = kProgressComplete;

// crossystem HWWP property name.
constexpr char kHwwpProperty[] = "wpsw_cur";

}  // namespace

namespace rmad {

namespace fake {

FakeProvisionDeviceStateHandler::FakeProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    const base::FilePath& working_dir_path)
    : ProvisionDeviceStateHandler(
          json_store,
          daemon_callback,
          std::make_unique<fake::FakePowerManagerClient>(working_dir_path),
          std::make_unique<fake::FakeCbiUtils>(working_dir_path),
          std::make_unique<fake::FakeCrosConfigUtils>(),
          std::make_unique<fake::FakeCrosSystemUtils>(working_dir_path),
          std::make_unique<fake::FakeIioSensorProbeUtils>(),
          std::make_unique<fake::FakeSsfcUtils>(),
          std::make_unique<fake::FakeVpdUtils>(working_dir_path)) {}

}  // namespace fake

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      should_calibrate_(false),
      sensor_integrity_(false) {
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
  cbi_utils_ = std::make_unique<CbiUtilsImpl>();
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  iio_sensor_probe_utils_ = std::make_unique<IioSensorProbeUtilsImpl>();
  ssfc_utils_ = std::make_unique<SsfcUtilsImpl>();
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
  status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN);
  status_.set_progress(kProgressInit);
  status_.set_error(ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);
}

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    std::unique_ptr<PowerManagerClient> power_manager_client,
    std::unique_ptr<CbiUtils> cbi_utils,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<IioSensorProbeUtils> iio_sensor_probe_utils,
    std::unique_ptr<SsfcUtils> ssfc_utils,
    std::unique_ptr<VpdUtils> vpd_utils)
    : BaseStateHandler(json_store, daemon_callback),
      power_manager_client_(std::move(power_manager_client)),
      cbi_utils_(std::move(cbi_utils)),
      cros_config_utils_(std::move(cros_config_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      iio_sensor_probe_utils_(std::move(iio_sensor_probe_utils)),
      ssfc_utils_(std::move(ssfc_utils)),
      vpd_utils_(std::move(vpd_utils)),
      should_calibrate_(false),
      sensor_integrity_(false) {
  status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN);
  status_.set_progress(kProgressInit);
  status_.set_error(ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);
}

RmadErrorCode ProvisionDeviceStateHandler::InitializeState() {
  if (!state_.has_provision_device() && !RetrieveState()) {
    state_.set_allocated_provision_device(new ProvisionDeviceState);
  }

  if (!task_runner_) {
    task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  }

  // If status_name is set in |json_store_|, it means it has been provisioned.
  // We should restore the status and let users decide.
  ProvisionStatus::Status provision_status = GetProgress().status();
  if (std::string status_name;
      json_store_->GetValue(kProvisionFinishedStatus, &status_name) &&
      ProvisionStatus::Status_Parse(status_name, &provision_status)) {
    UpdateStatus(provision_status, kProgressInit);
    if (provision_status == ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE ||
        provision_status ==
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING) {
      InitializeCalibrationTask();
    }
  }

  return RMAD_ERROR_OK;
}

void ProvisionDeviceStateHandler::RunState() {
  if (status_.status() == ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN) {
    StartProvision();
  }
  StartStatusTimer();
}

void ProvisionDeviceStateHandler::CleanUpState() {
  StopStatusTimer();
}

BaseStateHandler::GetNextStateCaseReply
ProvisionDeviceStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_provision_device()) {
    LOG(ERROR) << "RmadState missing |provision| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  state_ = state;
  StoreState();
  const ProvisionStatus& status = GetProgress();
  switch (state.provision_device().choice()) {
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_UNKNOWN:
      return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE:
      switch (status.status()) {
        case ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS:
          return NextStateCaseWrapper(RMAD_ERROR_WAIT);
        case ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE:
          [[fallthrough]];
        case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING:
          json_store_->SetValue(kProvisionFinishedStatus,
                                ProvisionStatus::Status_Name(status.status()));
          // Schedule a reboot after |kRebootDelay| seconds and return.
          reboot_timer_.Start(FROM_HERE, kRebootDelay, this,
                              &ProvisionDeviceStateHandler::Reboot);
          return NextStateCaseWrapper(GetStateCase(), RMAD_ERROR_EXPECT_REBOOT,
                                      AdditionalActivity::REBOOT);
        case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING:
          return NextStateCaseWrapper(RMAD_ERROR_PROVISIONING_FAILED);
        default:
          break;
      }
      break;
    case ProvisionDeviceState::RMAD_PROVISION_CHOICE_RETRY:
      StartProvision();
      StartStatusTimer();
      return NextStateCaseWrapper(RMAD_ERROR_WAIT);
    default:
      break;
  }

  NOTREACHED();
  return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
}

BaseStateHandler::GetNextStateCaseReply
ProvisionDeviceStateHandler::TryGetNextStateCaseAtBoot() {
  // If the status is already complete or non-blocking at startup, we should go
  // to the next state. Otherwise, don't transition.
  switch (GetProgress().status()) {
    case ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE:
      [[fallthrough]];
    case ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_NON_BLOCKING:
      if (should_calibrate_) {
        if (sensor_integrity_) {
          return NextStateCaseWrapper(RmadState::StateCase::kSetupCalibration);
        } else {
          // TODO(genechang): Go to kCheckCalibration for the user to check.
          return NextStateCaseWrapper(RmadState::StateCase::kSetupCalibration);
        }
      } else if (bool wipe_device;
                 json_store_->GetValue(kWipeDevice, &wipe_device) &&
                 !wipe_device) {
        return NextStateCaseWrapper(RmadState::StateCase::kWpEnablePhysical);
      } else {
        return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
      }
    default:
      break;
  }

  return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
}

void ProvisionDeviceStateHandler::InitializeCalibrationTask() {
  // There are several situations:
  // 1. replaced & probed -> calibrate
  // 2. probed only -> skip
  // 3. replaced only w/ mlb repair-> ignore
  // 4. replaced only w/o mlb repair -> error

  InstructionCalibrationStatusMap calibration_map;

  std::set<RmadComponent> replaced_components_need_calibration;
  if (std::vector<std::string> replaced_component_names; json_store_->GetValue(
          kReplacedComponentNames, &replaced_component_names)) {
    for (const std::string& component_name : replaced_component_names) {
      RmadComponent component;
      CHECK(RmadComponent_Parse(component_name, &component));
      if (std::find(kComponentsNeedManualCalibration.begin(),
                    kComponentsNeedManualCalibration.end(),
                    component) != kComponentsNeedManualCalibration.end()) {
        replaced_components_need_calibration.insert(component);
      }
    }
  }

  // This is the part where we probe sensors through the iioservice provided by
  // the sensor team, which is different from the runtime probe service.
  std::set<RmadComponent> probed_components = iio_sensor_probe_utils_->Probe();

  sensor_integrity_ =
      CheckSensorStatusIntegrity(replaced_components_need_calibration,
                                 probed_components, &calibration_map);

  // Update probeable components using runtime_probe results.
  for (RmadComponent component : probed_components) {
    // Ignore the components that cannot be calibrated.
    if (std::find(kComponentsNeedManualCalibration.begin(),
                  kComponentsNeedManualCalibration.end(),
                  component) == kComponentsNeedManualCalibration.end()) {
      continue;
    }

    // 1. replaced & probed -> calibrate
    // 2. probed only -> skip
    if (replaced_components_need_calibration.count(component)) {
      calibration_map[GetCalibrationSetupInstruction(component)][component] =
          CalibrationComponentStatus::RMAD_CALIBRATION_WAITING;
      should_calibrate_ = true;
    } else {
      calibration_map[GetCalibrationSetupInstruction(component)][component] =
          CalibrationComponentStatus::RMAD_CALIBRATION_SKIP;
    }
  }

  if (!SetCalibrationMap(json_store_, calibration_map)) {
    LOG(ERROR) << "Failed to set the calibration map.";
  }
}

bool ProvisionDeviceStateHandler::CheckSensorStatusIntegrity(
    const std::set<RmadComponent>& replaced_components_need_calibration,
    const std::set<RmadComponent>& probed_components,
    InstructionCalibrationStatusMap* calibration_map) {
  // There are several situations:
  // 1. replaced & probed -> calibrate
  // 2. probed only -> skip
  // 3. replaced only w/ mlb repair-> ignore
  // 4. replaced only w/o mlb repair -> V1: log message, V2: let user check

  // Since if it's a mainboard repair, all components are marked as replaced
  // and all situations are valid (cases 1, 2, and 3). In this case, we don't
  // care about those sensors that were marked as replaced but not probed.
  if (bool mlb_repair;
      json_store_->GetValue(kMlbRepair, &mlb_repair) && mlb_repair) {
    return true;
  }

  bool component_integrity = true;
  // Handle sensors marked as replaced but not probed (case 4).
  for (RmadComponent component : replaced_components_need_calibration) {
    if (probed_components.count(component)) {
      continue;
    }
    // 4. replaced only w/o mlb repair -> V1: log message, V2: let user check
    // TODO(genechang): Set to a missing status for displaying messages in V2
    StoreErrorCode(RMAD_ERROR_MISSING_COMPONENT);
    component_integrity = false;
  }

  return component_integrity;
}

void ProvisionDeviceStateHandler::SendStatusSignal() {
  const ProvisionStatus& status = GetProgress();
  daemon_callback_->GetProvisionSignalCallback().Run(status);
  if (status.status() != ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS) {
    StopStatusTimer();
  }
}

void ProvisionDeviceStateHandler::StartStatusTimer() {
  StopStatusTimer();
  status_timer_.Start(FROM_HERE, kReportStatusInterval, this,
                      &ProvisionDeviceStateHandler::SendStatusSignal);
}

void ProvisionDeviceStateHandler::StopStatusTimer() {
  if (status_timer_.IsRunning()) {
    status_timer_.Stop();
  }
}

void ProvisionDeviceStateHandler::StartProvision() {
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressInit, ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ProvisionDeviceStateHandler::RunProvision,
                                base::Unretained(this)));
}

void ProvisionDeviceStateHandler::RunProvision() {
  // We should do all blocking items first, and then do non-blocking items.
  // In this case, once it fails, we can directly update the status to
  // FAILED_BLOCKING or FAILED_NON_BLOCKING based on the failed item.

  bool same_owner = false;
  if (!json_store_->GetValue(kSameOwner, &same_owner)) {
    LOG(ERROR) << "Failed to get device destination from json store";
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressGetDestination,
               ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);

  std::string model_name;
  if (!cros_config_utils_->GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name from cros_config.";
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressGetModelName,
               ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);

  bool need_to_update_ssfc = false;
  uint32_t ssfc;
  if (!ssfc_utils_->GetSSFC(model_name, &need_to_update_ssfc, &ssfc)) {
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressGetSSFC, ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);

  if (need_to_update_ssfc && !cbi_utils_->SetSSFC(ssfc)) {
    if (IsHwwpDisabled()) {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE);
    } else {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_WP_ENABLED);
    }
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressWriteSSFC);

  if (!same_owner) {
    std::string stable_device_secret;
    if (!GenerateStableDeviceSecret(&stable_device_secret)) {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_GENERATE_SECRET);
      return;
    }

    // Writing a string to the vpd cache should always succeed.
    if (!vpd_utils_->SetStableDeviceSecret(stable_device_secret)) {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_INTERNAL);
      return;
    }
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
                 kProgressUpdateStableDeviceSecret,
                 ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);
    // TODO(genechang): Reset fingerprint sensor here."
  }

  // VPD is locked by SWWP only and should not be enabled throughout the RMA.
  if (!vpd_utils_->FlushOutRoVpdCache()) {
    if (IsHwwpDisabled()) {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE);
    } else {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_WP_ENABLED);
    }
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE,
               kProgressFlushOutVpdCache,
               ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);
}

void ProvisionDeviceStateHandler::UpdateStatus(ProvisionStatus::Status status,
                                               double progress,
                                               ProvisionStatus::Error error) {
  base::AutoLock scoped_lock(lock_);
  status_.set_status(status);
  status_.set_progress(progress);
  status_.set_error(error);
}

ProvisionStatus ProvisionDeviceStateHandler::GetProgress() const {
  base::AutoLock scoped_lock(lock_);
  return status_;
}

bool ProvisionDeviceStateHandler::GenerateStableDeviceSecret(
    std::string* stable_device_secret) {
  CHECK(stable_device_secret);
  unsigned char buffer[kStableDeviceSecretSize];
  if (RAND_bytes(buffer, kStableDeviceSecretSize) != 1) {
    LOG(ERROR) << "Failed to get random bytes.";
    return false;
  }

  *stable_device_secret = base::HexEncode(buffer, kStableDeviceSecretSize);
  return true;
}

void ProvisionDeviceStateHandler::Reboot() {
  LOG(INFO) << "Rebooting after updating configs.";
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

bool ProvisionDeviceStateHandler::IsHwwpDisabled() const {
  int hwwp_status;
  return (crossystem_utils_->GetInt(kHwwpProperty, &hwwp_status) &&
          hwwp_status == 0);
}

}  // namespace rmad
