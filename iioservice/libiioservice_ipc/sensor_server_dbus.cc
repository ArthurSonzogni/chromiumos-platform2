// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/libiioservice_ipc/sensor_server_dbus.h"

#include <memory>

#include <base/bind.h>
#include <base/check.h>
#include <chromeos/dbus/service_constants.h>

#include "iioservice/include/common.h"

namespace iioservice {

void SensorServerDbus::BootstrapMojoConnection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sensor_sequence_checker_);
  DCHECK(sensor_bus_);

  proxy_ = sensor_bus_->GetObjectProxy(
      ::mojo_connection_service::kMojoConnectionServiceServiceName,
      dbus::ObjectPath(
          ::mojo_connection_service::kMojoConnectionServiceServicePath));
  if (!proxy_) {
    LOGF(ERROR) << "Failed to get proxy for "
                << ::mojo_connection_service::kMojoConnectionServiceServiceName;
    return;
  }

  method_call_ = std::make_unique<dbus::MethodCall>(
      ::mojo_connection_service::kMojoConnectionServiceInterface,
      ::mojo_connection_service::kBootstrapMojoConnectionForIioServiceMethod);

  proxy_->WaitForServiceToBeAvailable(
      base::BindOnce(&SensorServerDbus::OnServiceAvailabilityChange,
                     weak_factory_.GetWeakPtr()));
}

void SensorServerDbus::OnInvitationReceived(
    mojo::IncomingInvitation invitation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sensor_sequence_checker_);

  // Bind primordial message pipe to a SensorHalServer implementation.
  OnServerReceived(mojo::PendingReceiver<cros::mojom::SensorHalServer>(
      invitation.ExtractMessagePipe(0)));
}

}  // namespace iioservice
