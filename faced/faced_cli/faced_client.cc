// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/faced_cli/faced_client.h"

#include <iostream>
#include <string>
#include <utility>

#include <absl/status/status.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_piece.h>
#include <brillo/cryptohome.h>
#include <brillo/dbus/dbus_connection.h>
#include <chromeos/dbus/service_constants.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "dbus_proxies/dbus-proxies.h"
#include "faced/face_auth_service_impl.h"
#include "faced/faced_cli/face_enrollment_session_delegate_impl.h"
#include "faced/mojom/faceauth.mojom.h"
#include "faced/util/blocking_future.h"
#include "faced/util/status.h"

namespace faced {
namespace {

using ::brillo::cryptohome::home::SanitizeUserName;
using ::chromeos::faceauth::mojom::EnrollmentSessionConfig;
using ::chromeos::faceauth::mojom::FaceAuthenticationService;
using ::chromeos::faceauth::mojom::FaceEnrollmentSession;
using ::chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate;

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

absl::Status Enroll(base::StringPiece user) {
  FACE_ASSIGN_OR_RETURN(FacedConnection connection, ConnectToFaced());

  mojo::Remote<FaceAuthenticationService> service =
      mojo::Remote<FaceAuthenticationService>(
          mojo::PendingRemote<FaceAuthenticationService>(
              std::move(connection.pipe),
              /*version=*/0));

  BlockingFuture<absl::Status> final_status;
  Enroller enroller(service, final_status.PromiseCallback());
  enroller.Run(user);
  final_status.Wait();

  return final_status.value();
}

Enroller::Enroller(mojo::Remote<FaceAuthenticationService>& service,
                   EnrollmentCompleteCallback enrollment_complete)
    : service_(service),
      delegate_(base::MakeRefCounted<FaceEnrollmentSessionDelegateImpl>(
          std::move(enrollment_complete))),
      receiver_(
          mojo::Receiver<FaceEnrollmentSessionDelegate>(delegate_.get())) {
  service_.set_disconnect_handler(base::BindOnce(
      &FaceEnrollmentSessionDelegateImpl::OnFaceAuthenticationServiceDisconnect,
      delegate_));
}

void Enroller::Run(base::StringPiece user) {
  service_->CreateEnrollmentSession(
      EnrollmentSessionConfig::New(SanitizeUserName(std::string(user)),
                                   /*accessibility=*/false),
      session_remote_.BindNewPipeAndPassReceiver(),
      receiver_.BindNewPipeAndPassRemote(),
      base::BindOnce(
          &FaceEnrollmentSessionDelegateImpl::CreateEnrollmentSessionComplete,
          delegate_));
}

}  // namespace faced
