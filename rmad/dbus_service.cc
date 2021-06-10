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

using rmad::CalibrateComponentsState;
using rmad::ProvisionDeviceState;
using rmad::RmadErrorCode;

template <>
struct DBusType<RmadErrorCode> {
  inline static std::string GetSignature() {
    return DBusType<uint32_t>::GetSignature();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const RmadErrorCode value) {
    DBusType<uint32_t>::Write(writer, static_cast<uint32_t>(value));
  }
  inline static bool Read(dbus::MessageReader* reader, RmadErrorCode* value) {
    uint32_t v;
    if (DBusType<uint32_t>::Read(reader, &v)) {
      *value = static_cast<rmad::RmadErrorCode>(v);
      return true;
    } else {
      return false;
    }
  }
};

template <>
struct DBusType<CalibrateComponentsState::CalibrationComponent> {
  inline static std::string GetSignature() {
    return DBusType<uint32_t>::GetSignature();
  }
  inline static void Write(
      dbus::MessageWriter* writer,
      const CalibrateComponentsState::CalibrationComponent value) {
    DBusType<uint32_t>::Write(writer, static_cast<uint32_t>(value));
  }
  inline static bool Read(
      dbus::MessageReader* reader,
      CalibrateComponentsState::CalibrationComponent* value) {
    uint32_t v;
    if (DBusType<uint32_t>::Read(reader, &v)) {
      *value = static_cast<CalibrateComponentsState::CalibrationComponent>(v);
      return true;
    } else {
      return false;
    }
  }
};

template <>
struct DBusType<ProvisionDeviceState::ProvisioningStep> {
  inline static std::string GetSignature() {
    return DBusType<uint32_t>::GetSignature();
  }
  inline static void Write(dbus::MessageWriter* writer,
                           const ProvisionDeviceState::ProvisioningStep value) {
    DBusType<uint32_t>::Write(writer, static_cast<uint32_t>(value));
  }
  inline static bool Read(dbus::MessageReader* reader,
                          ProvisionDeviceState::ProvisioningStep* value) {
    uint32_t v;
    if (DBusType<uint32_t>::Read(reader, &v)) {
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
  calibration_signal_ = dbus_interface->RegisterSignal<
      CalibrateComponentsState::CalibrationComponent, double>(
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
}

bool DBusService::SendErrorSignal(RmadErrorCode error) {
  auto signal = error_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(error);
}

bool DBusService::SendCalibrationProgressSignal(
    CalibrateComponentsState::CalibrationComponent component, double progress) {
  auto signal = calibration_signal_.lock();
  return (signal.get() == nullptr) ? false : signal->Send(component, progress);
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

void DBusService::QuitIfRmaNotRequired() {
  if (rmad_interface_->GetCurrentStateCase() == RmadState::STATE_NOT_SET) {
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
