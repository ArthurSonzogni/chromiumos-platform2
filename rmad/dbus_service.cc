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

DBusService::~DBusService() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
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

  dbus_interface->AddMethodHandler(kGetCurrentStateMethod,
                                   base::Unretained(this),
                                   &DBusService::HandleGetCurrentState);

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("Failed to register D-Bus objects.", true));
}

void DBusService::HandleGetCurrentState(
    std::unique_ptr<GetCurrentStateResponse> response,
    const GetCurrentStateRequest& request) {
  // Convert to shared_ptr so rmad_interface_ can safely copy the callback.
  using SharedResponsePointer = std::shared_ptr<GetCurrentStateResponse>;
  rmad_interface_->GetCurrentState(
      request, base::Bind(&DBusService::ReplyAndQuit<GetCurrentStateResponse,
                                                     GetCurrentStateReply>,
                          base::Unretained(this),
                          SharedResponsePointer(std::move(response))));
}

template <typename ResponseType, typename ReplyProtobufType>
void DBusService::ReplyAndQuit(std::shared_ptr<ResponseType> response,
                               const ReplyProtobufType& reply) {
  response->Return(reply);
  PostQuitTask();
}

void DBusService::PostQuitTask() {
  if (bus_) {
    bus_->GetOriginTaskRunner()->PostTask(
        FROM_HERE, base::Bind(&Daemon::Quit, base::Unretained(this)));
  }
}

}  // namespace rmad
