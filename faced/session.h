// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_SESSION_H_
#define FACED_SESSION_H_

#include <cstdint>
#include <memory>

#include <absl/random/random.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "faced/mojom/face_auth.mojom.h"

namespace faced {

// Interface for registering disconnect handler on a session.
class SessionInterface {
 public:
  virtual ~SessionInterface() = default;

  // Return a unique identifier for this session.
  //
  // The session id is used to identify a session across connections.
  // It is for debugging purposes only.
  virtual uint64_t session_id() = 0;

  using DisconnectCallback = base::OnceCallback<void()>;

  // Register a callback to be called when the session is disconnected.
  //
  // It is invoked when the remote session delegate is disconnected or when
  // the session ends and closes the connection.
  virtual void RegisterDisconnectHandler(
      DisconnectCallback disconnect_handler) = 0;
};

// Enrollment session encapsulates the dependencies needed and operations
// performed during enrollment.
class EnrollmentSession : public SessionInterface {
 public:
  static absl::StatusOr<std::unique_ptr<EnrollmentSession>> Create(
      absl::BitGen& bitgen,
      mojo::PendingRemote<
          chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate> delegate,
      chromeos::face_auth::mojom::EnrollmentSessionConfigPtr config);

  ~EnrollmentSession() override = default;

  // Disallow copy and move.
  EnrollmentSession(const EnrollmentSession&) = delete;
  EnrollmentSession& operator=(const EnrollmentSession&) = delete;

  // `SessionInterface` implementation.
  uint64_t session_id() override { return session_id_; }
  void RegisterDisconnectHandler(
      DisconnectCallback disconnect_handler) override;

 private:
  EnrollmentSession(
      uint64_t session_id,
      mojo::PendingRemote<
          chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate> delegate);

  // Handle the disconnection of the remote.
  void OnDisconnect();

  int64_t session_id_;
  mojo::Remote<chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate>
      delegate_;

  DisconnectCallback disconnect_callback_;

  // Must be last member.
  base::WeakPtrFactory<EnrollmentSession> weak_ptr_factory_{this};
};

// Authentication session encapsulates the dependencies needed and operations
// performed during authentication.
class AuthenticationSession : public SessionInterface {
 public:
  static absl::StatusOr<std::unique_ptr<AuthenticationSession>> Create(
      absl::BitGen& bitgen,
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

 private:
  AuthenticationSession(
      uint64_t session_id,
      mojo::PendingRemote<
          chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate>
          delegate);

  // Handle the disconnection of the remote.
  void OnDisconnect();

  int64_t session_id_;
  mojo::Remote<chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate>
      delegate_;

  DisconnectCallback disconnect_callback_;

  // Must be last member.
  base::WeakPtrFactory<AuthenticationSession> weak_ptr_factory_{this};
};

}  // namespace faced

#endif  // FACED_SESSION_H_
