// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

#include <inttypes.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/lock.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <brillo/hwid/hwid_utils.h>
#include <openssl/rand.h>
#include <re2/re2.h>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/ssfc/ssfc_prober.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/system/tpm_manager_client_impl.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/cbi_utils_impl.h"
#include "rmad/utils/cmd_utils_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/futility_utils_impl.h"
#include "rmad/utils/gsc_utils_impl.h"
#include "rmad/utils/hwid_utils_impl.h"
#include "rmad/utils/iio_sensor_probe_utils_impl.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/rmad_config_utils_impl.h"
#include "rmad/utils/vpd_utils.h"
#include "rmad/utils/vpd_utils_impl.h"
#include "rmad/utils/write_protect_utils_impl.h"

namespace {

constexpr int kStableDeviceSecretSize = 32;

constexpr double kProgressComplete = 1.0;
// TODO(chenghan): Uncomment this when we have a non-blocking error.
// constexpr double kProgressFailedNonblocking = -1.0;
constexpr double kProgressFailedBlocking = -2.0;
constexpr double kProgressInit = 0.0;
constexpr double kProgressGetDestination = 0.1;
constexpr double kProgressGetModelName = 0.2;
constexpr double kProgressWriteSsfc = 0.3;
constexpr double kProgressReadFwConfig = 0.4;
constexpr double kProgressWriteFwConfig = 0.5;
constexpr double kProgressUpdateHwidBrandCode = 0.6;
constexpr double kProgressUpdateStableDeviceSecret = 0.7;
constexpr double kProgressFlushOutVpdCache = 0.8;
constexpr double kProgressResetGbbFlags = 0.9;
constexpr double kProgressProvisionTi50 = kProgressComplete;
constexpr double kProgressSetBoardId = kProgressComplete;

constexpr char kEmptyBoardIdType[] = "ffffffff";
constexpr char kTestBoardIdType[] = "5a5a4352";  // ZZCR.
constexpr char kTwoStagePvtBoardIdFlags[] = "00003f80";

const std::vector<std::string> kResetGbbFlagsArgv = {
    "/usr/bin/futility", "gbb", "--set", "--flash", "--flags=0"};

constexpr char kApWpsrCmd[] = "/usr/sbin/ap_wpsr";
constexpr char kApWpsrValueMaskRegexp[] = R"(SR Value\/Mask = (.+))";

constexpr char kSoundCardInitCmd[] = "/usr/bin/sound_card_init";
constexpr char kSoundCardIdPath[] = "/proc/asound/card0/id";
constexpr char kSoundCardInitRmaCaliSubCmd[] = "rma_calibration";

}  // namespace

namespace rmad {

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(kDefaultWorkingDirPath),
      should_calibrate_(false),
      sensor_integrity_(false) {
  ssfc_prober_ = std::make_unique<SsfcProberImpl>();
  power_manager_client_ = std::make_unique<PowerManagerClientImpl>();
  cbi_utils_ = std::make_unique<CbiUtilsImpl>();
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
  gsc_utils_ = std::make_unique<GscUtilsImpl>();
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  write_protect_utils_ = std::make_unique<WriteProtectUtilsImpl>();
  iio_sensor_probe_utils_ = std::make_unique<IioSensorProbeUtilsImpl>();
  vpd_utils_ = std::make_unique<VpdUtilsImpl>();
  hwid_utils_ = std::make_unique<HwidUtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  futility_utils_ = std::make_unique<FutilityUtilsImpl>();
  tpm_manager_client_ = std::make_unique<TpmManagerClientImpl>();
  rmad_config_utils_ = std::make_unique<RmadConfigUtilsImpl>();
  status_.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_UNKNOWN);
  status_.set_progress(kProgressInit);
  status_.set_error(ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);
}

ProvisionDeviceStateHandler::ProvisionDeviceStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    const base::FilePath& working_dir_path,
    std::unique_ptr<SsfcProber> ssfc_prober,
    std::unique_ptr<PowerManagerClient> power_manager_client,
    std::unique_ptr<CbiUtils> cbi_utils,
    std::unique_ptr<CmdUtils> cmd_utils,
    std::unique_ptr<GscUtils> gsc_utils,
    std::unique_ptr<CrosConfigUtils> cros_config_utils,
    std::unique_ptr<WriteProtectUtils> write_protect_utils,
    std::unique_ptr<IioSensorProbeUtils> iio_sensor_probe_utils,
    std::unique_ptr<VpdUtils> vpd_utils,
    std::unique_ptr<HwidUtils> hwid_utils,
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<FutilityUtils> futility_utils,
    std::unique_ptr<TpmManagerClient> tpm_manager_client,
    std::unique_ptr<RmadConfigUtils> rmad_config_utils)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      ssfc_prober_(std::move(ssfc_prober)),
      power_manager_client_(std::move(power_manager_client)),
      cbi_utils_(std::move(cbi_utils)),
      cmd_utils_(std::move(cmd_utils)),
      gsc_utils_(std::move(gsc_utils)),
      cros_config_utils_(std::move(cros_config_utils)),
      write_protect_utils_(std::move(write_protect_utils)),
      iio_sensor_probe_utils_(std::move(iio_sensor_probe_utils)),
      vpd_utils_(std::move(vpd_utils)),
      hwid_utils_(std::move(hwid_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      futility_utils_(std::move(futility_utils)),
      tpm_manager_client_(std::move(tpm_manager_client)),
      rmad_config_utils_(std::move(rmad_config_utils)),
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

  if (!cros_config_utils_->GetRmadCrosConfig(&rmad_cros_config_)) {
    LOG(ERROR) << "Failed to get RMA config from cros_config";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
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
                                      RMAD_ADDITIONAL_ACTIVITY_REBOOT);
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

  NOTREACHED_IN_MIGRATION();
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
  auto rmad_config = rmad_config_utils_->GetConfig();

  std::set<RmadComponent> replaced_components_need_calibration;
  if (!IsCalibrationDisabled(working_dir_path_) &&
      !(rmad_config.has_value() &&
        rmad_config->skip_calibration_with_golden_value())) {
    if (bool mlb_repair;
        json_store_->GetValue(kMlbRepair, &mlb_repair) && mlb_repair) {
      // Potentially everything needs to be calibrated when MLB is repaired.
      for (const RmadComponent component : kComponentsNeedManualCalibration) {
        replaced_components_need_calibration.insert(component);
      }
    } else if (std::vector<std::string> replaced_component_names;
               json_store_->GetValue(kReplacedComponentNames,
                                     &replaced_component_names)) {
      for (const std::string& component_name : replaced_component_names) {
        RmadComponent component;
        CHECK(RmadComponent_Parse(component_name, &component));
        if (kComponentsNeedManualCalibration.contains(component)) {
          replaced_components_need_calibration.insert(component);
        }
      }
    }
  }

  // This is the part where we probe sensors through the iioservice provided by
  // the sensor team, which is different from the runtime probe service.
  std::set<RmadComponent> probed_components = iio_sensor_probe_utils_->Probe();

  sensor_integrity_ =
      CheckSensorStatusIntegrity(replaced_components_need_calibration,
                                 probed_components, &calibration_map);

  // Update probeable components using probe results.
  for (RmadComponent component : probed_components) {
    // Ignore the components that cannot be calibrated.
    if (!kComponentsNeedManualCalibration.contains(component)) {
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
    StoreErrorCode(RmadState::kProvisionDevice, RMAD_ERROR_MISSING_COMPONENT);
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

bool ProvisionDeviceStateHandler::GetSsfcFromCrosConfig(
    std::optional<uint32_t>* ssfc) const {
  if (ssfc_prober_->IsSsfcRequired()) {
    if (uint32_t ssfc_value; ssfc_prober_->ProbeSsfc(&ssfc_value)) {
      *ssfc = std::optional<uint32_t>{ssfc_value};
      return true;
    }
    LOG(ERROR) << "Failed to probe SSFC";
    return false;
  }
  *ssfc = std::nullopt;
  return true;
}

void ProvisionDeviceStateHandler::StartProvision() {
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressInit, ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN);

  // This should be run on the main thread.
  std::optional<uint32_t> ssfc;
  if (!GetSsfcFromCrosConfig(&ssfc)) {
    // TODO(chenghan): Add a new error enum for this.
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
    return;
  }

  RunProvision(ssfc);
}

void ProvisionDeviceStateHandler::RunProvision(std::optional<uint32_t> ssfc) {
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
               kProgressGetDestination);

  std::string model_name;
  if (!cros_config_utils_->GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name from cros_config.";
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressGetModelName);

  if (ssfc.has_value()) {
    if (base::PathExists(working_dir_path_.Append(kTestDirPath))) {
      DLOG(INFO) << "Setting SSFC bypassed in test mode.";
      DLOG(INFO) << "SSFC value: " << ssfc.value();
    } else if (!cbi_utils_->SetSsfc(ssfc.value())) {
      // Failed to set SSFC.
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
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressWriteSsfc);

  // Set firmware config to CBI according to cros_config.
  if (rmad_cros_config_.has_cbi) {
    if (ProvisionStatus::Error error = UpdateFirmwareConfig();
        error != ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN) {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking, error);
      return;
    }
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
                 kProgressWriteFwConfig);
  }

  // Update the HWID brand code according to cros_config.
  if (ProvisionStatus::Error error = UpdateHwidBrandCode();
      error != ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN) {
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking, error);
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressUpdateHwidBrandCode);

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
                 kProgressUpdateStableDeviceSecret);
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
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressFlushOutVpdCache);

  // Reset GBB flags.
  if (uint64_t shimless_mode;
      (vpd_utils_->GetShimlessMode(&shimless_mode) &&
       shimless_mode & kShimlessModeFlagsPreserveGbbFlags) ||
      base::PathExists(working_dir_path_.Append(kTestDirPath))) {
    // TODO(jeffulin): Remove test file usages.
    DLOG(INFO) << "GBB flags preserved for testing.";
  } else if (std::string output;
             !cmd_utils_->GetOutput(kResetGbbFlagsArgv, &output)) {
    LOG(ERROR) << "Failed to reset GBB flags";
    LOG(ERROR) << output;
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_GBB);
    return;
  }
  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressResetGbbFlags);

  // Set GSC board ID if it is not set yet.
  auto board_id_type = gsc_utils_->GetBoardIdType();
  if (!board_id_type.has_value()) {
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CR50);
    return;
  }
  auto board_id_flags = gsc_utils_->GetBoardIdFlags();
  if (board_id_type.value() == kEmptyBoardIdType) {
    bool is_two_stage = false;
    if (board_id_flags.has_value() &&
        board_id_flags == kTwoStagePvtBoardIdFlags) {
      // For two-stage cases (LOEM projects and spare MLB for RMA), the board ID
      // type are left empty and be set in LOEM or during RMA.
      is_two_stage = true;
      if (!cros_config_utils_->HasCustomLabel()) {
        // It's a spare MLB for RMA.
        DLOG(INFO) << "Setting GSC board ID type for spare MLB.";
      }
    } else {
      // TODO(chenghan): This is a security violation. Record a metric for it.
      LOG(ERROR) << "GSC board ID type is empty in RMA";
    }
    if (!gsc_utils_->SetBoardId(is_two_stage)) {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_CR50);
      return;
    }
  } else if (board_id_type.value() == kTestBoardIdType) {
    // TODO(chenghan): Test board ID is not allowed in RMA. Record a metrics for
    //                 it.
    LOG(ERROR) << "GSC board ID type cannot be ZZCR in RMA";
    if (uint64_t shimless_mode;
        (vpd_utils_->GetShimlessMode(&shimless_mode) &&
         shimless_mode & kShimlessModeFlagsBoardIdCheckResultBypass) ||
        base::PathExists(working_dir_path_.Append(kTestDirPath))) {
      // TODO(jeffulin): Remove test file usages.
      DLOG(INFO) << "GSC board ID check bypassed";
    } else {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_CR50);
      return;
    }
  }

  if (!CalibrateSmartAmp()) {
    // We are not blocking the process when it fails to calibrate because it is
    // expected on devices without Smart Amp.
    LOG(ERROR) << "Failed to calibrate smart amp";
  }

  if (GscDevice device; tpm_manager_client_->GetGscDevice(&device) &&
                        (device == GscDevice::GSC_DEVICE_DT ||
                         device == GscDevice::GSC_DEVICE_NT)) {
    ProvisionTi50();
    return;
  }

  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE,
               kProgressSetBoardId);
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
  DLOG(INFO) << "Rebooting after updating configs.";
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

bool ProvisionDeviceStateHandler::IsHwwpDisabled() const {
  auto hwwp_enabled = write_protect_utils_->GetHardwareWriteProtectionStatus();
  return (hwwp_enabled.has_value() && !hwwp_enabled.value());
}

ProvisionStatus::Error ProvisionDeviceStateHandler::UpdateFirmwareConfig() {
  uint32_t cros_config_fw_config;
  if (!cros_config_utils_->GetFirmwareConfig(&cros_config_fw_config)) {
    // TODO(jeffulin): Some platforms have no firmware config even with CBI, so
    //                 we should record this in cros_config per platform. For
    //                 now if we fail to get firmware config in cros_config, we
    //                 skip setting it to CBI.
    LOG(WARNING) << "Failed to get firmware config in cros_config.";
    return ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN;
  }

  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
               kProgressReadFwConfig);

  uint32_t cbi_fw_config;
  bool get_cbi_fw_config_success =
      cbi_utils_->GetFirmwareConfig(&cbi_fw_config);
  if (!get_cbi_fw_config_success) {
    LOG(WARNING) << "Failed to get firmware config in cbi.";
  }

  // If the firmware config is not set in CBI, we just set what we found in
  // cros_config.
  if ((!get_cbi_fw_config_success || cros_config_fw_config != cbi_fw_config) &&
      !cbi_utils_->SetFirmwareConfig(cros_config_fw_config)) {
    // TODO(jeffulin): Add an error code of setting firmware config.
    LOG(ERROR) << "Failed to set firmware config to cbi.";
    return ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE;
  }

  return ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN;
}

ProvisionStatus::Error ProvisionDeviceStateHandler::UpdateHwidBrandCode() {
  std::string hwid;
  if (!crossystem_utils_->GetHwid(&hwid)) {
    LOG(ERROR) << "Failed to get HWID string";
    return ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ;
  }

  auto hwid_elements = hwid_utils_->DecomposeHwid(hwid);

  if (!hwid_elements.has_value()) {
    LOG(ERROR) << "Failed to decompose HWID string.";
    return ProvisionStatus::RMAD_PROVISION_ERROR_INTERNAL;
  }

  if (!hwid_elements.value().brand_code.has_value()) {
    // Some older models have no brand code in their HWID, so we just leave it
    // blank here.
    return ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN;
  }

  // Compare the brand code in HWID and cros_config.
  std::string brand_code;
  if (!cros_config_utils_->GetBrandCode(&brand_code)) {
    LOG(ERROR) << "Failed to get brand code from cros_config.";
    return ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ;
  }

  std::string raw_hwid = base::StringPrintf(
      "%s-%s %s", hwid_elements.value().model_name.value().c_str(),
      brand_code.c_str(),
      hwid_elements.value().encoded_components.value().c_str());

  auto checksum = brillo::hwid::CalculateChecksum(raw_hwid);

  if (!checksum.has_value()) {
    LOG(ERROR) << "Failed to calculate HWID checksum.";
    return ProvisionStatus::RMAD_PROVISION_ERROR_INTERNAL;
  }

  std::string new_hwid =
      base::StringPrintf("%s%s", raw_hwid.c_str(), checksum.value().c_str());

  if (!futility_utils_->SetHwid(new_hwid)) {
    LOG(ERROR) << "Failed to set HWID.";
    return ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE;
  }
  DLOG(INFO) << "Set HWID as " << new_hwid << ".";

  return ProvisionStatus::RMAD_PROVISION_ERROR_UNKNOWN;
}

void ProvisionDeviceStateHandler::ProvisionTi50() {
  // Set addressing mode.
  if (gsc_utils_->GetAddressingMode() == SpiAddressingMode::kNotProvisioned) {
    auto flash_size = futility_utils_->GetFlashSize();
    if (!flash_size.has_value()) {
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
      return;
    }

    if (!gsc_utils_->SetAddressingMode(
            gsc_utils_->GetAddressingModeByFlashSize(flash_size.value()))) {
      LOG(ERROR) << "Failed to set addressing mode. Flash size: "
                 << flash_size.value();
      UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                   kProgressFailedBlocking,
                   ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE);
      return;
    }
  }

  // Set WPSR.
  if (std::optional<bool> provision_status = gsc_utils_->IsApWpsrProvisioned();
      provision_status.has_value() && !provision_status.value()) {
    daemon_callback_->GetExecuteGetFlashInfoCallback().Run(base::BindOnce(
        &ProvisionDeviceStateHandler::ProvisionWpsr, base::Unretained(this)));
    return;
  }

  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE,
               kProgressProvisionTi50);
}

bool ProvisionDeviceStateHandler::CalibrateSmartAmp() {
  base::FilePath path = base::FilePath(kSoundCardIdPath);
  std::string id;
  if (!base::ReadFileToString(path, &id)) {
    LOG(ERROR) << "Failed to get sound card id";
    return false;
  } else if (!base::TrimWhitespaceASCII(id, base::TRIM_TRAILING, &id)) {
    LOG(ERROR) << "Failed to trim sound card id";
    return false;
  }
  DLOG(INFO) << "Got sound card id: " << id;

  auto conf = cros_config_utils_->GetSoundCardConfig();
  if (!conf.has_value()) {
    LOG(ERROR) << "Failed to get sound card config";
    return false;
  }
  DLOG(INFO) << "Got sound card config: " << conf.value();

  auto amp = cros_config_utils_->GetSpeakerAmp();
  if (!amp.has_value()) {
    LOG(ERROR) << "Failed to get speaker amp";
    return false;
  }
  DLOG(INFO) << "Got speaker amp: " << amp.value();

  std::string output;
  if (!cmd_utils_->GetOutputAndError(
          {kSoundCardInitCmd, kSoundCardInitRmaCaliSubCmd, "--id", id, "--conf",
           conf.value(), "--amp", amp.value()},
          &output)) {
    LOG(ERROR) << "Failed to calibrate sound card: " << output;
    return false;
  }

  return true;
}

void ProvisionDeviceStateHandler::ProvisionWpsr(
    const std::optional<FlashInfo>& flash_info) {
  if (!flash_info.has_value()) {
    LOG(ERROR) << "Failed to get flash informaion.";
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_READ);
    return;
  }

  std::string output;
  const std::string start =
      base::StringPrintf("0x%" PRIx64, flash_info.value().wpsr_start);
  const std::string length =
      base::StringPrintf("0x%" PRIx64, flash_info.value().wpsr_length);

  // Try to map the flash name to one recognized by |ap_wpsr|. Some flash chips
  // do not need this transform so we are not blocking the process here.
  auto mapped_flash_name =
      cros_config_utils_->GetSpiFlashTransform(flash_info.value().flash_name);

  std::string name = (mapped_flash_name.has_value())
                         ? mapped_flash_name.value()
                         : flash_info.value().flash_name;

  // TODO(jeffulin): Make the step of provisioning WPSR a blocking step after we
  // have long-term solutions of b/327527364.
  if (!cmd_utils_->GetOutputAndError(
          {kApWpsrCmd, "--name", name, "--start", start, "--length", length},
          &output)) {
    LOG(ERROR) << "Failed to get WPSR values and masks: " << output;
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE,
                 kProgressComplete);
    return;
  }

  std::string value_mask;
  if (!RE2::PartialMatch(output, kApWpsrValueMaskRegexp, &value_mask)) {
    LOG(ERROR) << "Failed to parse WPSR values and masks.";
    LOG(ERROR) << "ap_wpsr output: " << output;
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_INTERNAL);
    return;
  }

  if (!gsc_utils_->SetWpsr(value_mask)) {
    UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING,
                 kProgressFailedBlocking,
                 ProvisionStatus::RMAD_PROVISION_ERROR_CANNOT_WRITE);
    return;
  }

  UpdateStatus(ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE,
               kProgressComplete);
}

}  // namespace rmad
