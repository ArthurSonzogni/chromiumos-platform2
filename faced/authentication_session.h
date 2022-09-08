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

#include "faced/mojom/face_auth.mojom.h"
#include "faced/session.h"

namespace faced {

// Authentication session encapsulates the dependencies needed and operations
// performed during authentication.
class AuthenticationSession
    : public SessionInterface,
      public chromeos::face_auth::mojom::FaceAuthenticationSession {
 public:
  static absl::StatusOr<std::unique_ptr<AuthenticationSession>> Create(
      absl::BitGen& bitgen,
      mojo::PendingReceiver<
          chromeos::face_auth::mojom::FaceAuthenticationSession> receiver,
      mojo::PendingRemote<
          chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate>
          delegate,
      chromeos::face_auth::mojom::AuthenticationSessionConfigPtr config);

  ~AuthenticationSession() override = default;

  // Disallow copy and move.
  AuthenticationSession(const AuthenticationSession&) = delete;
  AuthenticationSession& operator=(const AuthenticationSession&) = delete;

  // `SessionInterface` implementation.
  uint64_t session_id() override { return session_id_; }
  void RegisterDisconnectHandler(
      DisconnectCallback disconnect_handler) override;

  // Notify FaceAuthenticationSessionDelegate of session state changes.
  //
  // Notify of authentication progress.
  void NotifyUpdate(chromeos::face_auth::mojom::FaceOperationStatus status);
  // Notify of completed authentication and close the connection.
  void NotifyComplete(chromeos::face_auth::mojom::FaceOperationStatus status);
  // Notify of cancelled enrollment and close the connection.
  void NotifyCancelled();
  // Notify of unrecoverable error and close the connection.
  void NotifyError(absl::Status error);

 private:
  AuthenticationSession(
      uint64_t session_id,
      mojo::PendingReceiver<
          chromeos::face_auth::mojom::FaceAuthenticationSession> receiver,
      mojo::PendingRemote<
          chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate>
          delegate);

  // Handle the disconnection of the session receiver.
  void OnSessionDisconnect();
  // Handle the disconnection of the remote delegate.
  void OnDelegateDisconnect();

  int64_t session_id_;
  mojo::Receiver<chromeos::face_auth::mojom::FaceAuthenticationSession>
      receiver_;
  mojo::Remote<chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate>
      delegate_;

  DisconnectCallback disconnect_callback_;

  // Must be last member.
  base::WeakPtrFactory<AuthenticationSession> weak_ptr_factory_{this};
};

}  // namespace faced

#endif  // FACED_AUTHENTICATION_SESSION_H_
