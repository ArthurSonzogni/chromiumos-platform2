// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/faced_cli/faced_client.h"

#include <absl/status/status.h>
#include <brillo/dbus/dbus_connection.h>
#include <chromeos/dbus/service_constants.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include <iostream>
#include <string>
#include <utility>

#include "dbus_proxies/dbus-proxies.h"
#include "faced/util/status.h"

namespace faced {

absl::StatusOr<FacedConnection> ConnectToFaced() {
  FacedConnection connection;

  mojo::PlatformChannel channel;
  mojo::OutgoingInvitation invitation;

  // This sends an invitation to faced for a Mojo connection to be bootstrapped.

  // Attach a message pipe to be extracted by the receiver. The other end of the
  // pipe is returned for us to use locally.
  connection.pipe =
      invitation.AttachMessagePipe(faced::kBootstrapMojoConnectionChannelToken);
  mojo::OutgoingInvitation::Send(std::move(invitation),
                                 base::kNullProcessHandle,
                                 channel.TakeLocalEndpoint());

  // Setup libbrillo dbus.
  brillo::DBusConnection dbus_connection;
  connection.bus = dbus_connection.Connect();
  if (!connection.bus) {
    return absl::InternalError(
        "Could not connect to the faced system service: Failed to connect to "
        "system bus through libbrillo.");
  }

  org::chromium::FaceAuthDaemonProxy proxy(connection.bus,
                                           faced::kFaceAuthDaemonName);
  brillo::dbus_utils::FileDescriptor handle(
      channel.TakeRemoteEndpoint().TakePlatformHandle().TakeFD());
  if (!proxy.BootstrapMojoConnection(handle, /*error=*/nullptr)) {
    return absl::InternalError(
        "Could not connect to the faced system service: Failed to send handle "
        "over DBus");
  }

  return connection;
}

absl::Status ConnectAndDisconnectFromFaced() {
  FACE_ASSIGN_OR_RETURN(FacedConnection connection, ConnectToFaced());

  std::cout << "Could successfully connect to the faced system service.\n";

  return absl::OkStatus();
}

}  // namespace faced
