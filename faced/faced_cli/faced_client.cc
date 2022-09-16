// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/faced_cli/faced_client.h"

#include <absl/status/status.h>
#include <brillo/cryptohome.h>
#include <brillo/dbus/dbus_connection.h>
#include <chromeos/dbus/service_constants.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include <iostream>
#include <string>
#include <utility>

#include "dbus_proxies/dbus-proxies.h"
#include "faced/face_auth_service_impl.h"
#include "faced/faced_cli/face_enrollment_session_delegate_impl.h"
#include "faced/mojom/faceauth.mojom.h"
#include "faced/util/blocking_future.h"
#include "faced/util/status.h"
#include "faced/util/task.h"

namespace faced {
namespace {

using ::brillo::cryptohome::home::SanitizeUserName;
using ::chromeos::faceauth::mojom::CreateSessionResultPtr;
using ::chromeos::faceauth::mojom::EnrollmentSessionConfig;
using ::chromeos::faceauth::mojom::FaceAuthenticationService;
using ::chromeos::faceauth::mojom::FaceEnrollmentSession;
using ::chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate;

// Upon disconnection of FaceAuthenticationService remote, print out that a
// disconnection occurred.
void OnDisconnect() {
  std::cout << "FaceAuthenticationService disconnected.\n";
}

// TODO(b/247034576): Handle enrollment
void CreateEnrollmentComplete(EnrollmentComplete enrollment_complete,
                              CreateSessionResultPtr result) {
  // Check if session creation failed.
  if (!result->is_session_info()) {
    PostToCurrentSequence(base::BindOnce(
        std::move(enrollment_complete),
        absl::InternalError("Failed to create an enrollment session.")));
  }

  std::cout << "Successfully created enrollment.\n";
  PostToCurrentSequence(
      base::BindOnce(std::move(enrollment_complete), absl::OkStatus()));
}

}  // namespace

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

void EnrollWithRemoteService(std::string_view user,
                             mojo::Remote<FaceAuthenticationService>& service,
                             EnrollmentComplete enrollment_complete) {
  mojo::Remote<FaceEnrollmentSession> session_remote;
  FaceEnrollmentSessionDelegateImpl delegate_impl;
  mojo::Receiver<FaceEnrollmentSessionDelegate> receiver(&delegate_impl);

  service->CreateEnrollmentSession(
      EnrollmentSessionConfig::New(SanitizeUserName(std::string(user)),
                                   /*accessibility=*/false),
      session_remote.BindNewPipeAndPassReceiver(),
      receiver.BindNewPipeAndPassRemote(),
      base::BindOnce(&CreateEnrollmentComplete,
                     std::move(enrollment_complete)));
}

absl::Status Enroll(std::string_view user) {
  FACE_ASSIGN_OR_RETURN(FacedConnection connection, ConnectToFaced());

  mojo::Remote<FaceAuthenticationService> service(
      mojo::PendingRemote<FaceAuthenticationService>(std::move(connection.pipe),
                                                     /*version=*/0));
  service.set_disconnect_handler(base::BindOnce(&OnDisconnect));

  BlockingFuture<absl::Status> final_status;
  EnrollWithRemoteService(user, service, final_status.PromiseCallback());
  final_status.Wait();

  return final_status.value();
}

}  // namespace faced
