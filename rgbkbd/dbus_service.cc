// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/dbus_service.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/rgbkbd/dbus-constants.h>

namespace rgbkbd {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusObject;

DBusService::DBusService() : brillo::DBusServiceDaemon(kRgbkbdServiceName) {}

DBusService::~DBusService() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
}

int DBusService::OnInit() {
  LOG(INFO) << "Starting DBus service";
  const int exit_code = DBusServiceDaemon::OnInit();
  LOG(INFO) << "DBus service exiting with code " << exit_code;
  return exit_code;
}

void DBusService::RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) {
  if (!dbus_object_.get()) {
    CHECK(bus_.get());
    dbus_object_ = std::make_unique<DBusObject>(
        nullptr, bus_, dbus::ObjectPath(kRgbkbdServicePath));
  }

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("Failed to register D-Bus objects.", true));
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
        FROM_HERE, base::BindOnce(&Daemon::Quit, base::Unretained(this)));
  }
}

}  // namespace rgbkbd
