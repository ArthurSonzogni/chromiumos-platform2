// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/dbus_service.h"

#include <memory>
#include <string>

#include <chromeos/bind_lambda.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>

#include "tpm_manager/common/dbus_interface.h"

using chromeos::dbus_utils::DBusMethodResponse;

namespace tpm_manager {

DBusService::DBusService(const scoped_refptr<dbus::Bus>& bus,
                         TpmManagerInterface* service)
    : dbus_object_(nullptr, bus, dbus::ObjectPath(kTpmManagerServicePath)),
      service_(service) {}

void DBusService::Register(const CompletionAction& callback) {
  chromeos::dbus_utils::DBusInterface* dbus_interface =
      dbus_object_.AddOrGetInterface(kTpmManagerInterface);

  dbus_interface->AddMethodHandler(kGetTpmStatus, base::Unretained(this),
                                   &DBusService::HandleGetTpmStatus);
  dbus_object_.RegisterAsync(callback);
}

void DBusService::HandleGetTpmStatus(
    std::unique_ptr<DBusMethodResponse<const GetTpmStatusReply&>> response,
    const GetTpmStatusRequest& request) {
  // Convert |response| to a shared_ptr so |service_| can safely copy the
  // callback.
  using SharedResponsePointer = std::shared_ptr<
      DBusMethodResponse<const GetTpmStatusReply&>>;
  // A callback that fills the reply protobuf and sends it.
  auto callback = [](const SharedResponsePointer& response,
                     const GetTpmStatusReply& reply) {
    response->Return(reply);
  };
  service_->GetTpmStatus(
      request,
      base::Bind(callback, SharedResponsePointer(std::move(response))));
}

}  // namespace tpm_manager
