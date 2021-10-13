// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/dbus_service.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <brillo/dbus/data_serialization.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/rmad/dbus-constants.h>
#include <sysexits.h>

namespace brillo {
namespace dbus_utils {

using rmad::CalibrationComponentStatus;
using rmad::CalibrationOverallStatus;
using rmad::FinalizeStatus;
using rmad::HardwareVerificationResult;
using rmad::ProvisionStatus;
using rmad::RmadComponent;
using rmad::RmadErrorCode;

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

namespace {

const char kCroslogCmd[] = "/usr/sbin/croslog";

}  // namespace

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusObject;

DBusService::DBusService(RmadInterface* rmad_interface)
    : brillo::DBusServiceDaemon(kRmadServiceName),
      rmad_interface_(rmad_interface) {}

DBusService::DBusService(const scoped_refptr<dbus::Bus>& bus,
                         RmadInterface* rmad_interface)
    : brillo::DBusServiceDaemon(kRmadServiceName),
      rmad_interface_(rmad_interface) {
  dbus_object_ = std::make_unique<DBusObject>(
      nullptr, bus, dbus::ObjectPath(kRmadServicePath));
}

int DBusService::OnEventLoopStarted() {
  const int exit_code = DBusServiceDaemon::OnEventLoopStarted();
  if (exit_code != EX_OK) {
    return exit_code;
  }

  // Initialize RMA interface and try a state transition.
  rmad_interface_->Initialize();
  RegisterSignalSenders();
  rmad_interface_->TryTransitionNextStateFromCurrentState();
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

  dbus_interface->AddMethodHandler(
      kGetCurrentStateMethod, base::Unretained(this),
      &DBusService::HandleMethod<GetStateReply,
                                 &RmadInterface::GetCurrentState>);
  dbus_interface->AddMethodHandler(
      kTransitionNextStateMethod, base::Unretained(this),
      &DBusService::HandleMethod<TransitionNextStateRequest, GetStateReply,
                                 &RmadInterface::TransitionNextState>);
  dbus_interface->AddMethodHandler(
      kTransitionPreviousStateMethod, base::Unretained(this),
      &DBusService::HandleMethod<GetStateReply,
                                 &RmadInterface::TransitionPreviousState>);
  dbus_interface->AddMethodHandler(
      kAbortRmaMethod, base::Unretained(this),
      &DBusService::HandleMethod<AbortRmaReply, &RmadInterface::AbortRma>);

  dbus_interface->AddSimpleMethodHandler(kGetLogPathMethod,
                                         base::Unretained(this),
                                         &DBusService::HandleGetLogPathMethod);
  dbus_interface->AddSimpleMethodHandler(kGetLogMethod, base::Unretained(this),
                                         &DBusService::HandleGetLogMethod);

  error_signal_ = dbus_interface->RegisterSignal<RmadErrorCode>(kErrorSignal);
  hardware_verification_signal_ =
      dbus_interface->RegisterSignal<HardwareVerificationResult>(
          kHardwareVerificationResultSignal);
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

void DBusService::RegisterSignalSenders() {
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kWpDisablePhysical,
      std::make_unique<base::RepeatingCallback<bool(bool)>>(base::BindRepeating(
          &DBusService::SendHardwareWriteProtectionStateSignal,
          base::Unretained(this))));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kWpEnablePhysical,
      std::make_unique<base::RepeatingCallback<bool(bool)>>(base::BindRepeating(
          &DBusService::SendHardwareWriteProtectionStateSignal,
          base::Unretained(this))));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kWelcome,
      std::make_unique<
          base::RepeatingCallback<bool(const HardwareVerificationResult&)>>(
          base::BindRepeating(
              &DBusService::SendHardwareVerificationResultSignal,
              base::Unretained(this))));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kRunCalibration,
      std::make_unique<base::RepeatingCallback<bool(CalibrationOverallStatus)>>(
          base::BindRepeating(&DBusService::SendCalibrationOverallSignal,
                              base::Unretained(this))));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kRunCalibration,
      std::make_unique<
          base::RepeatingCallback<bool(CalibrationComponentStatus)>>(
          base::BindRepeating(&DBusService::SendCalibrationProgressSignal,
                              base::Unretained(this))));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kProvisionDevice,
      std::make_unique<base::RepeatingCallback<bool(const ProvisionStatus&)>>(
          base::BindRepeating(&DBusService::SendProvisionProgressSignal,
                              base::Unretained(this))));
  rmad_interface_->RegisterSignalSender(
      RmadState::StateCase::kFinalize,
      std::make_unique<base::RepeatingCallback<bool(const FinalizeStatus&)>>(
          base::BindRepeating(&DBusService::SendFinalizeProgressSignal,
                              base::Unretained(this))));
}

std::string DBusService::HandleGetLogPathMethod() {
  return "not_supported";
}

GetLogReply DBusService::HandleGetLogMethod() {
  GetLogReply reply;
  std::string log_string;
  if (base::GetAppOutput({kCroslogCmd, "--identifier=rmad"}, &log_string)) {
    reply.set_log(log_string);
  } else {
    LOG(ERROR) << "Failed to generate logs from croslog";
    reply.set_error(RMAD_ERROR_CANNOT_GET_LOG);
  }
  return reply;
}

bool DBusService::SendErrorSignal(RmadErrorCode error) {
  auto signal = error_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(error);
}

bool DBusService::SendHardwareVerificationResultSignal(
    const HardwareVerificationResult& result) {
  auto signal = hardware_verification_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(result);
}

bool DBusService::SendCalibrationOverallSignal(
    CalibrationOverallStatus status) {
  auto signal = calibration_overall_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(status);
}

bool DBusService::SendCalibrationProgressSignal(
    CalibrationComponentStatus status) {
  auto signal = calibration_component_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(status);
}

bool DBusService::SendProvisionProgressSignal(const ProvisionStatus& status) {
  auto signal = provision_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(status);
}

bool DBusService::SendFinalizeProgressSignal(const FinalizeStatus& status) {
  auto signal = finalize_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(status);
}

bool DBusService::SendHardwareWriteProtectionStateSignal(bool enabled) {
  auto signal = hwwp_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(enabled);
}

bool DBusService::SendPowerCableStateSignal(bool plugged_in) {
  auto signal = power_cable_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(plugged_in);
}

void DBusService::ConditionallyQuit() {
  const RmadState::StateCase current_state_case =
      rmad_interface_->GetCurrentStateCase();
  if (current_state_case == RmadState::STATE_NOT_SET ||
      current_state_case == RmadState::kWpDisableComplete) {
    PostQuitTask();
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
