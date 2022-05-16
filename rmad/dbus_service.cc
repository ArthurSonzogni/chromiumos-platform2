// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/dbus_service.h"

#include <sysexits.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <brillo/dbus/data_serialization.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/rmad/dbus-constants.h>

#include "rmad/constants.h"
#include "rmad/system/fake_tpm_manager_client.h"
#include "rmad/system/tpm_manager_client_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_cros_config_utils.h"
#include "rmad/utils/fake_crossystem_utils.h"

namespace brillo {
namespace dbus_utils {

using rmad::CalibrationComponentStatus;
using rmad::CalibrationOverallStatus;
using rmad::FinalizeStatus;
using rmad::HardwareVerificationResult;
using rmad::ProvisionStatus;
using rmad::RmadComponent;
using rmad::RmadErrorCode;
using rmad::UpdateRoFirmwareStatus;

// Overload AppendValueToWriter() for |HardwareVerificationResult|,
// |CalibrationComponentStatus|, |ProvisionStatus| and |FinalizeStatus|
// structures.
void AppendValueToWriter(dbus::MessageWriter* writer,
                         const HardwareVerificationResult& value) {
  dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  AppendValueToWriter(&struct_writer, value.is_compliant());
  AppendValueToWriter(&struct_writer, value.error_str());
  writer->CloseContainer(&struct_writer);
}

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const CalibrationComponentStatus& value) {
  dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  AppendValueToWriter(&struct_writer, static_cast<int>(value.component()));
  AppendValueToWriter(&struct_writer, static_cast<int>(value.status()));
  AppendValueToWriter(&struct_writer, value.progress());
  writer->CloseContainer(&struct_writer);
}

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const ProvisionStatus& value) {
  dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  AppendValueToWriter(&struct_writer, static_cast<int>(value.status()));
  AppendValueToWriter(&struct_writer, value.progress());
  writer->CloseContainer(&struct_writer);
}

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const FinalizeStatus& value) {
  dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  AppendValueToWriter(&struct_writer, static_cast<int>(value.status()));
  AppendValueToWriter(&struct_writer, value.progress());
  writer->CloseContainer(&struct_writer);
}

// Overload PopValueFromReader() for |HardwareVerificationResult|,
// |CalibrationComponentStatus|, |ProvisionStatus| and |FinalizeStatus|
// structures.
bool PopValueFromReader(dbus::MessageReader* reader,
                        HardwareVerificationResult* value) {
  dbus::MessageReader struct_reader(nullptr);
  if (!reader->PopStruct(&struct_reader)) {
    return false;
  }

  bool is_compliant;
  std::string error_str;
  if (!PopValueFromReader(&struct_reader, &is_compliant) ||
      !PopValueFromReader(&struct_reader, &error_str)) {
    return false;
  }
  value->set_is_compliant(is_compliant);
  value->set_error_str(error_str);
  return true;
}

bool PopValueFromReader(dbus::MessageReader* reader,
                        CalibrationComponentStatus* value) {
  dbus::MessageReader struct_reader(nullptr);
  if (!reader->PopStruct(&struct_reader)) {
    return false;
  }

  int component, status;
  double progress;
  if (!PopValueFromReader(&struct_reader, &component) ||
      !PopValueFromReader(&struct_reader, &status) ||
      !PopValueFromReader(&struct_reader, &progress)) {
    return false;
  }
  value->set_component(static_cast<RmadComponent>(component));
  value->set_status(
      static_cast<CalibrationComponentStatus::CalibrationStatus>(status));
  value->set_progress(progress);
  return true;
}

bool PopValueFromReader(dbus::MessageReader* reader, ProvisionStatus* value) {
  dbus::MessageReader struct_reader(nullptr);
  if (!reader->PopStruct(&struct_reader)) {
    return false;
  }

  int status;
  double progress;
  if (!PopValueFromReader(&struct_reader, &status) ||
      !PopValueFromReader(&struct_reader, &progress)) {
    return false;
  }
  value->set_status(static_cast<ProvisionStatus::Status>(status));
  value->set_progress(progress);
  return true;
}

bool PopValueFromReader(dbus::MessageReader* reader, FinalizeStatus* value) {
  dbus::MessageReader struct_reader(nullptr);
  if (!reader->PopStruct(&struct_reader)) {
    return false;
  }

  int status;
  double progress;
  if (!PopValueFromReader(&struct_reader, &status) ||
      !PopValueFromReader(&struct_reader, &progress)) {
    return false;
  }
  value->set_status(static_cast<FinalizeStatus::Status>(status));
  value->set_progress(progress);
  return true;
}

// DBusType definition for signals.
template <>
struct DBusType<RmadErrorCode> {
  inline static std::string GetSignature() {
    return DBusType<int>::GetSignature();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const RmadErrorCode value) {
    DBusType<int>::Write(writer, static_cast<int>(value));
  }
  inline static bool Read(dbus::MessageReader* reader, RmadErrorCode* value) {
    int v;
    if (DBusType<int>::Read(reader, &v)) {
      *value = static_cast<rmad::RmadErrorCode>(v);
      return true;
    } else {
      return false;
    }
  }
};

template <>
struct DBusType<HardwareVerificationResult> {
  inline static std::string GetSignature() {
    return GetStructDBusSignature<bool, std::string>();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const HardwareVerificationResult& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(dbus::MessageReader* reader,
                          HardwareVerificationResult* value) {
    return PopValueFromReader(reader, value);
  }
};

template <>
struct DBusType<UpdateRoFirmwareStatus> {
  inline static std::string GetSignature() {
    return DBusType<int>::GetSignature();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const UpdateRoFirmwareStatus status) {
    DBusType<int>::Write(writer, static_cast<int>(status));
  }
  inline static bool Read(dbus::MessageReader* reader,
                          UpdateRoFirmwareStatus* status) {
    int v;
    if (DBusType<int>::Read(reader, &v)) {
      *status = static_cast<UpdateRoFirmwareStatus>(v);
      return true;
    } else {
      return false;
    }
  }
};

template <>
struct DBusType<CalibrationOverallStatus> {
  inline static std::string GetSignature() {
    return DBusType<int>::GetSignature();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const CalibrationOverallStatus value) {
    DBusType<int>::Write(writer, static_cast<int>(value));
  }
  inline static bool Read(dbus::MessageReader* reader,
                          CalibrationOverallStatus* value) {
    int v;
    if (DBusType<int>::Read(reader, &v)) {
      *value = static_cast<CalibrationOverallStatus>(v);
      return true;
    } else {
      return false;
    }
  }
};

template <>
struct DBusType<CalibrationComponentStatus> {
  inline static std::string GetSignature() {
    return GetStructDBusSignature<int, int, double>();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const CalibrationComponentStatus& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(dbus::MessageReader* reader,
                          CalibrationComponentStatus* value) {
    return PopValueFromReader(reader, value);
  }
};

template <>
struct DBusType<ProvisionStatus> {
  inline static std::string GetSignature() {
    return GetStructDBusSignature<int, double>();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const ProvisionStatus& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(dbus::MessageReader* reader, ProvisionStatus* value) {
    return PopValueFromReader(reader, value);
  }
};

template <>
struct DBusType<FinalizeStatus> {
  inline static std::string GetSignature() {
    return GetStructDBusSignature<int, double>();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const FinalizeStatus& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(dbus::MessageReader* reader, FinalizeStatus* value) {
    return PopValueFromReader(reader, value);
  }
};

}  // namespace dbus_utils
}  // namespace brillo

namespace rmad {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusObject;

DBusService::DBusService(RmadInterface* rmad_interface)
    : brillo::DBusServiceDaemon(kRmadServiceName),
      rmad_interface_(rmad_interface),
      state_file_path_(kDefaultJsonStoreFilePath),
      is_external_utils_initialized_(false),
      is_interface_set_up_(false),
      test_mode_(false) {}

DBusService::DBusService(const scoped_refptr<dbus::Bus>& bus,
                         RmadInterface* rmad_interface,
                         const base::FilePath& state_file_path,
                         std::unique_ptr<TpmManagerClient> tpm_manager_client,
                         std::unique_ptr<CrosConfigUtils> cros_config_utils,
                         std::unique_ptr<CrosSystemUtils> crossystem_utils)
    : brillo::DBusServiceDaemon(kRmadServiceName),
      rmad_interface_(rmad_interface),
      state_file_path_(state_file_path),
      tpm_manager_client_(std::move(tpm_manager_client)),
      cros_config_utils_(std::move(cros_config_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      is_external_utils_initialized_(true),
      is_interface_set_up_(false),
      test_mode_(false) {
  dbus_object_ = std::make_unique<DBusObject>(
      nullptr, bus, dbus::ObjectPath(kRmadServicePath));
}

int DBusService::OnEventLoopStarted() {
  const int exit_code = DBusServiceDaemon::OnEventLoopStarted();
  if (exit_code != EX_OK) {
    return exit_code;
  }

  if (!is_external_utils_initialized_) {
    if (test_mode_) {
      const base::FilePath test_dir_path =
          base::FilePath(kDefaultWorkingDirPath).AppendASCII(kTestDirPath);
      tpm_manager_client_ =
          std::make_unique<fake::FakeTpmManagerClient>(test_dir_path);
      cros_config_utils_ = std::make_unique<fake::FakeCrosConfigUtils>();
      crossystem_utils_ =
          std::make_unique<fake::FakeCrosSystemUtils>(test_dir_path);
    } else {
      tpm_manager_client_ =
          std::make_unique<TpmManagerClientImpl>(GetSystemBus());
      cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
      crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
    }
    is_external_utils_initialized_ = true;
  }
  is_rma_required_ = CheckRmaCriteria();
  return EX_OK;
}

void DBusService::RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) {
  if (!dbus_object_.get()) {
    CHECK(bus_.get());
    dbus_object_ = std::make_unique<DBusObject>(
        nullptr, bus_, dbus::ObjectPath(kRmadServicePath));
  }
  brillo::dbus_utils::DBusInterface* dbus_interface =
      dbus_object_->AddOrGetInterface(kRmadInterfaceName);

  dbus_interface->AddMethodHandler(kIsRmaRequiredMethod, base::Unretained(this),
                                   &DBusService::HandleIsRmaRequiredMethod);
  dbus_interface->AddMethodHandler(
      kGetCurrentStateMethod, base::Unretained(this),
      &DBusService::DelegateToInterface<GetStateReply,
                                        &RmadInterface::GetCurrentState>);
  dbus_interface->AddMethodHandler(
      kTransitionNextStateMethod, base::Unretained(this),
      &DBusService::DelegateToInterface<TransitionNextStateRequest,
                                        GetStateReply,
                                        &RmadInterface::TransitionNextState>);
  dbus_interface->AddMethodHandler(
      kTransitionPreviousStateMethod, base::Unretained(this),
      &DBusService::DelegateToInterface<
          GetStateReply, &RmadInterface::TransitionPreviousState>);
  dbus_interface->AddMethodHandler(
      kAbortRmaMethod, base::Unretained(this),
      &DBusService::DelegateToInterface<AbortRmaReply,
                                        &RmadInterface::AbortRma>);
  dbus_interface->AddMethodHandler(
      kGetLogMethod, base::Unretained(this),
      &DBusService::DelegateToInterface<GetLogReply, &RmadInterface::GetLog>);
  dbus_interface->AddMethodHandler(
      kSaveLogMethod, base::Unretained(this),
      &DBusService::DelegateToInterface<std::string, SaveLogReply,
                                        &RmadInterface::SaveLog>);
  dbus_interface->AddMethodHandler(
      kRecordBrowserActionMetricMethod, base::Unretained(this),
      &DBusService::DelegateToInterface<
          RecordBrowserActionMetricRequest, RecordBrowserActionMetricReply,
          &RmadInterface::RecordBrowserActionMetric>);

  error_signal_ = dbus_interface->RegisterSignal<RmadErrorCode>(kErrorSignal);
  hardware_verification_signal_ =
      dbus_interface->RegisterSignal<HardwareVerificationResult>(
          kHardwareVerificationResultSignal);
  update_ro_firmware_status_signal_ =
      dbus_interface->RegisterSignal<UpdateRoFirmwareStatus>(
          kUpdateRoFirmwareStatusSignal);
  calibration_overall_signal_ =
      dbus_interface->RegisterSignal<CalibrationOverallStatus>(
          kCalibrationOverallSignal);
  calibration_component_signal_ =
      dbus_interface->RegisterSignal<CalibrationComponentStatus>(
          kCalibrationProgressSignal);
  provision_signal_ = dbus_interface->RegisterSignal<ProvisionStatus>(
      kProvisioningProgressSignal);
  finalize_signal_ =
      dbus_interface->RegisterSignal<FinalizeStatus>(kFinalizeProgressSignal);
  hwwp_signal_ =
      dbus_interface->RegisterSignal<bool>(kHardwareWriteProtectionStateSignal);
  power_cable_signal_ =
      dbus_interface->RegisterSignal<bool>(kPowerCableStateSignal);

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("Failed to register D-Bus objects.", true));
}

bool DBusService::CheckRmaCriteria() const {
  // Only allow Shimless RMA in normal mode.
  if (std::string mainfw_type;
      !crossystem_utils_->GetMainFwType(&mainfw_type) ||
      mainfw_type != "normal") {
    return false;
  }
  // Only allow Shimless RMA on some models.
  if (std::string model; !cros_config_utils_->GetModelName(&model) ||
                         std::find(kAllowedModels.begin(), kAllowedModels.end(),
                                   model) == kAllowedModels.end()) {
    return false;
  }
  if (base::PathExists(state_file_path_)) {
    return true;
  }
  CHECK(is_external_utils_initialized_);
  if (RoVerificationStatus status;
      tpm_manager_client_->GetRoVerificationStatus(&status) &&
      (status == RoVerificationStatus::PASS ||
       status == RoVerificationStatus::UNSUPPORTED_TRIGGERED)) {
    // Initialize the state file so we can reliably boot into RMA even if Chrome
    // accidentally reboots the device before calling |GetCurrentState| API.
    base::WriteFile(state_file_path_, "{}");
    return true;
  }
  return false;
}

bool DBusService::SetUpInterface() {
  CHECK(rmad_interface_);
  if (!is_interface_set_up_) {
    if (!rmad_interface_->SetUp()) {
      return false;
    }
    is_interface_set_up_ = true;
    SetUpInterfaceCallbacks();
    rmad_interface_->TryTransitionNextStateFromCurrentState();
  }
  return true;
}

void DBusService::SetUpInterfaceCallbacks() {
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kWpDisablePhysical,
      base::BindRepeating(&DBusService::SendHardwareWriteProtectionStateSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kWpEnablePhysical,
      base::BindRepeating(&DBusService::SendHardwareWriteProtectionStateSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kWelcome,
      base::BindRepeating(&DBusService::SendHardwareVerificationResultSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kUpdateRoFirmware,
      base::BindRepeating(&DBusService::SendUpdateRoFirmwareStatusSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kRunCalibration,
      base::BindRepeating(&DBusService::SendCalibrationOverallSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kRunCalibration,
      base::BindRepeating(&DBusService::SendCalibrationProgressSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kProvisionDevice,
      base::BindRepeating(&DBusService::SendProvisionProgressSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kFinalize,
      base::BindRepeating(&DBusService::SendFinalizeProgressSignal,
                          base::Unretained(this)));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kRepairComplete,
      base::BindRepeating(&DBusService::SendPowerCableStateSignal,
                          base::Unretained(this)));
}

void DBusService::HandleIsRmaRequiredMethod(
    std::unique_ptr<DBusMethodResponse<bool>> response) {
  // Quit the daemon if we are not in RMA.
  bool quit_daemon = !is_rma_required_;
  SendReply(std::move(response), is_rma_required_, quit_daemon);
}

void DBusService::SendErrorSignal(RmadErrorCode error) {
  auto signal = error_signal_.lock();
  if (signal) {
    signal->Send(error);
  }
}

void DBusService::SendHardwareVerificationResultSignal(
    const HardwareVerificationResult& result) {
  auto signal = hardware_verification_signal_.lock();
  if (signal) {
    signal->Send(result);
  }
}

void DBusService::SendUpdateRoFirmwareStatusSignal(
    UpdateRoFirmwareStatus status) {
  auto signal = update_ro_firmware_status_signal_.lock();
  if (signal) {
    signal->Send(status);
  }
}

void DBusService::SendCalibrationOverallSignal(
    CalibrationOverallStatus status) {
  auto signal = calibration_overall_signal_.lock();
  if (signal) {
    signal->Send(status);
  }
}

void DBusService::SendCalibrationProgressSignal(
    CalibrationComponentStatus status) {
  auto signal = calibration_component_signal_.lock();
  if (signal) {
    signal->Send(status);
  }
}

void DBusService::SendProvisionProgressSignal(const ProvisionStatus& status) {
  auto signal = provision_signal_.lock();
  if (signal) {
    signal->Send(status);
  }
}

void DBusService::SendFinalizeProgressSignal(const FinalizeStatus& status) {
  auto signal = finalize_signal_.lock();
  if (signal) {
    signal->Send(status);
  }
}

void DBusService::SendHardwareWriteProtectionStateSignal(bool enabled) {
  auto signal = hwwp_signal_.lock();
  if (signal) {
    signal->Send(enabled);
  }
}

void DBusService::SendPowerCableStateSignal(bool plugged_in) {
  auto signal = power_cable_signal_.lock();
  if (signal) {
    signal->Send(plugged_in);
  }
}

void DBusService::PostQuitTask() {
  if (bus_) {
    VLOG(1) << "Stopping DBus service";
    bus_->GetOriginTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(&Daemon::Quit, base::Unretained(this)));
  }
}

}  // namespace rmad
