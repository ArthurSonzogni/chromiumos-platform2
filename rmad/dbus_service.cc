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

namespace brillo {
namespace dbus_utils {

using rmad::CalibrationComponentStatus;
using rmad::ProvisionDeviceState;
using rmad::RmadComponent;
using rmad::RmadErrorCode;

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

// Overload AppendValueToWriter() for "CheckCalibrationState::CalibrationStatus"
// structure.
void AppendValueToWriter(dbus::MessageWriter* writer,
                         const CalibrationComponentStatus& value) {
  dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  AppendValueToWriter(&struct_writer, static_cast<int>(value.component()));
  AppendValueToWriter(&struct_writer, static_cast<int>(value.status()));
  AppendValueToWriter(&struct_writer, value.progress());
  writer->CloseContainer(&struct_writer);
}

// Overload PopValueFromReader() for "CheckCalibrationState::CalibrationStatus"
// structure.
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
struct DBusType<ProvisionDeviceState::ProvisioningStep> {
  inline static std::string GetSignature() {
    return DBusType<int>::GetSignature();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const ProvisionDeviceState::ProvisioningStep value) {
    DBusType<int>::Write(writer, static_cast<int>(value));
  }
  inline static bool Read(dbus::MessageReader* reader,
                          ProvisionDeviceState::ProvisioningStep* value) {
    int v;
    if (DBusType<int>::Read(reader, &v)) {
      *value = static_cast<ProvisionDeviceState::ProvisioningStep>(v);
      return true;
    } else {
      return false;
    }
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

int DBusService::OnInit() {
  VLOG(1) << "Starting DBus service";
  const int exit_code = DBusServiceDaemon::OnInit();
  return exit_code;
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
  calibration_signal_ =
      dbus_interface->RegisterSignal<CalibrationComponentStatus>(
          kCalibrationProgressSignal);
  provisioning_signal_ =
      dbus_interface
          ->RegisterSignal<ProvisionDeviceState::ProvisioningStep, double>(
              kProvisioningProgressSignal);
  hwwp_signal_ =
      dbus_interface->RegisterSignal<bool>(kHardwareWriteProtectionStateSignal);
  power_cable_signal_ =
      dbus_interface->RegisterSignal<bool>(kPowerCableStateSignal);

  RegisterSignalSenders();

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
      RmadState::StateCase::kRunCalibration,
      std::make_unique<
          base::RepeatingCallback<bool(CalibrationComponentStatus)>>(
          base::BindRepeating(&DBusService::SendCalibrationProgressSignal,
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

bool DBusService::SendCalibrationProgressSignal(
    CalibrationComponentStatus status) {
  auto signal = calibration_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(status);
}

bool DBusService::SendProvisioningProgressSignal(
    ProvisionDeviceState::ProvisioningStep step, double progress) {
  auto signal = provisioning_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(step, progress);
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
