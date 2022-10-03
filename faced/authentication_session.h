// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_AUTHENTICATION_SESSION_H_
#define FACED_AUTHENTICATION_SESSION_H_

#include <cstdint>
#include <memory>

#include <absl/random/random.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "faced/face_service.h"
#include "faced/mojom/faceauth.mojom.h"
#include "faced/session.h"

namespace faced {

// Authentication session encapsulates the dependencies needed and operations
// performed during authentication.
class AuthenticationSession
    : public SessionInterface,
      public chromeos::faceauth::mojom::FaceAuthenticationSession {
 public:
  static absl::StatusOr<std::unique_ptr<AuthenticationSession>> Create(
      absl::BitGen& bitgen,
      mojo::PendingReceiver<
          chromeos::faceauth::mojom::FaceAuthenticationSession> receiver,
      mojo::PendingRemote<
          chromeos::faceauth::mojom::FaceAuthenticationSessionDelegate>
          delegate,
      chromeos::faceauth::mojom::AuthenticationSessionConfigPtr config,
      Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> client);

  ~AuthenticationSession() override = default;

  // Disallow copy and move.
  AuthenticationSession(const AuthenticationSession&) = delete;
  AuthenticationSession& operator=(const AuthenticationSession&) = delete;

  // `SessionInterface` implementation.
  uint64_t session_id() override { return session_id_; }
  void RegisterDisconnectHandler(
      DisconnectCallback disconnect_handler) override;
  void Start(StartCallback callback) override;

  // Notify FaceAuthenticationSessionDelegate of session state changes.
  //
  // Notify of authentication progress.
  void NotifyUpdate(chromeos::faceauth::mojom::FaceOperationStatus status);
  // Notify of completed authentication and close the connection.
  void NotifyComplete(chromeos::faceauth::mojom::FaceOperationStatus status);
  // Notify of cancelled enrollment and close the connection.
  void NotifyCancelled();
  // Notify of unrecoverable error and close the connection.
  void NotifyError(absl::Status error);

 private:
  AuthenticationSession(
      uint64_t session_id,
      mojo::PendingReceiver<
          chromeos::faceauth::mojom::FaceAuthenticationSession> receiver,
      mojo::PendingRemote<
          chromeos::faceauth::mojom::FaceAuthenticationSessionDelegate>
          delegate,
      Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> client);

  // Handle the disconnection of the session receiver.
  void OnSessionDisconnect();
  // Handle the disconnection of the remote delegate.
  void OnDelegateDisconnect();

  int64_t session_id_;
  mojo::Receiver<chromeos::faceauth::mojom::FaceAuthenticationSession>
      receiver_;
  mojo::Remote<chromeos::faceauth::mojom::FaceAuthenticationSessionDelegate>
      delegate_;

  DisconnectCallback disconnect_callback_;

  // Async gRPC client that uses an internal completion queue.
  Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> rpc_client_;

  // Must be last member.
  base::WeakPtrFactory<AuthenticationSession> weak_ptr_factory_{this};
};

}  // namespace faced

#endif  // FACED_AUTHENTICATION_SESSION_H_
