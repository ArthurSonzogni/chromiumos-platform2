// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACED_CLI_FACED_CLIENT_H_
#define FACED_FACED_CLI_FACED_CLIENT_H_

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <brillo/dbus/dbus_connection.h>
#include <mojo/public/cpp/system/message_pipe.h>

namespace faced {

// Components of a connection to the Faced daemon
struct FacedConnection {
  // DBus connection to Faced
  scoped_refptr<dbus::Bus> bus;

  // Pipe for Mojo communication
  mojo::ScopedMessagePipeHandle pipe;
};

// Establish Mojo connection to Faced bootstrapped over DBus
absl::StatusOr<FacedConnection> ConnectToFaced();

// Establish a Mojo connection to Faced bootstrapped over DBus then disconnect
absl::Status ConnectAndDisconnectFromFaced();

}  // namespace faced

#endif  // FACED_FACED_CLI_FACED_CLIENT_H_
