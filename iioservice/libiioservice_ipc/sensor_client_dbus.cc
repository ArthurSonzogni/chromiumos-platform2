// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/libiioservice_ipc/sensor_client_dbus.h"

#include <chromeos/dbus/service_constants.h>
#include <base/check.h>
#include <dbus/object_proxy.h>

namespace iioservice {

void SensorClientDbus::BootstrapMojoConnection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sensor_sequence_checker_);
  DCHECK(sensor_bus_);

  dbus::ObjectProxy* proxy = sensor_bus_->GetObjectProxy(
      ::mojo_connection_service::kMojoConnectionServiceServiceName,
      dbus::ObjectPath(
          ::mojo_connection_service::kMojoConnectionServiceServicePath));

  dbus::MethodCall method_call(
      ::mojo_connection_service::kMojoConnectionServiceInterface,
      ::mojo_connection_service::
          kBootstrapMojoConnectionForSensorClientsMethod);
  dbus::MessageWriter writer(&method_call);
  proxy->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                    base::BindOnce(&SensorClientDbus::OnBootstrapMojoResponse,
                                   weak_factory_.GetWeakPtr()));
}

void SensorClientDbus::OnInvitationReceived(
    mojo::IncomingInvitation invitation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sensor_sequence_checker_);

  // Bind primordial message pipe to a SensorHalClient implementation.
  OnClientReceived(mojo::PendingReceiver<cros::mojom::SensorHalClient>(
      invitation.ExtractMessagePipe(0)));
}

}  // namespace iioservice
