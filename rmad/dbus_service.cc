// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/dbus_service.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <brillo/dbus/data_serialization.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/rmad/dbus-constants.h>

namespace brillo {
namespace dbus_utils {

using rmad::CheckCalibrationState;
using rmad::ProvisionDeviceState;
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
void AppendValueToWriter(
    dbus::MessageWriter* writer,
    const CheckCalibrationState::CalibrationStatus& value) {
  dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  AppendValueToWriter(&struct_writer, static_cast<int>(value.name()));
  AppendValueToWriter(&struct_writer, static_cast<int>(value.status()));
  writer->CloseContainer(&struct_writer);
}

// Overload PopValueFromReader() for "CheckCalibrationState::CalibrationStatus"
// structure.
bool PopValueFromReader(dbus::MessageReader* reader,
                        CheckCalibrationState::CalibrationStatus* value) {
  dbus::MessageReader struct_reader(nullptr);
  if (!reader->PopStruct(&struct_reader)) {
    return false;
  }

  int name, status;
  if (!PopValueFromReader(&struct_reader, &name) ||
      !PopValueFromReader(&struct_reader, &status)) {
    return false;
  }
  value->set_name(
      static_cast<CheckCalibrationState::CalibrationStatus::Component>(name));
  value->set_status(
      static_cast<CheckCalibrationState::CalibrationStatus::Status>(status));
  return true;
}

template <>
struct DBusType<CheckCalibrationState::CalibrationStatus> {
  inline static std::string GetSignature() {
    return GetStructDBusSignature<int, int>();
  }
  inline static void Write(
      dbus::MessageWriter* writer,
      const CheckCalibrationState::CalibrationStatus& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(dbus::MessageReader* reader,
                          CheckCalibrationState::CalibrationStatus* value) {
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
  LOG(INFO) << "Starting DBus service";
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

  dbus_interface->AddMethodHandler(
      kGetLogPathMethod, base::Unretained(this),
      &DBusService::HandleMethod<std::string, &RmadInterface::GetLogPath>);

  error_signal_ = dbus_interface->RegisterSignal<RmadErrorCode>(kErrorSignal);
  calibration_signal_ =
      dbus_interface
          ->RegisterSignal<CheckCalibrationState::CalibrationStatus, double>(
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
      std::make_unique<base::RepeatingCallback<bool(
          CheckCalibrationState::CalibrationStatus status, double)>>(
          base::BindRepeating(&DBusService::SendCalibrationProgressSignal,
                              base::Unretained(this))));
}

bool DBusService::SendErrorSignal(RmadErrorCode error) {
  auto signal = error_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(error);
}

bool DBusService::SendCalibrationProgressSignal(
    CheckCalibrationState::CalibrationStatus status, double progress) {
  auto signal = calibration_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(status, progress);
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
    bus_->GetOriginTaskRunner()->PostTask(
        FROM_HERE, base::Bind(&Daemon::Quit, base::Unretained(this)));
  }
}

}  // namespace rmad
