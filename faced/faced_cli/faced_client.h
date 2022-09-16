// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACED_CLI_FACED_CLIENT_H_
#define FACED_FACED_CLI_FACED_CLIENT_H_

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <brillo/dbus/dbus_connection.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "faced/mojom/faceauth.mojom.h"

namespace faced {

using EnrollmentComplete = base::OnceCallback<void(absl::Status)>;

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

// Run an enrollment via Faced for a given user
absl::Status Enroll(std::string_view user);

// Internal implementation details (exposed for testing) below.

// Run an enrollment for a given user using a remote FaceAuthenticationService
void EnrollWithRemoteService(
    std::string_view user,
    mojo::Remote<::chromeos::faceauth::mojom::FaceAuthenticationService>&
        service,
    EnrollmentComplete enrollment_complete);

}  // namespace faced

#endif  // FACED_FACED_CLI_FACED_CLIENT_H_
