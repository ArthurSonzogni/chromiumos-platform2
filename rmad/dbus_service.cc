// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/dbus_service.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/rmad/dbus-constants.h>

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

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("Failed to register D-Bus objects.", true));
}

void DBusService::QuitIfRmaNotRequired(const GetStateReply& reply) {
  if (reply.error() == RMAD_ERROR_RMA_NOT_REQUIRED) {
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
