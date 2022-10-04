// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_ENROLLMENT_SESSION_H_
#define FACED_ENROLLMENT_SESSION_H_

#include <cstdint>
#include <memory>

#include <absl/random/random.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <brillo/grpc/async_grpc_client.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "faced/face_service.h"
#include "faced/mojom/faceauth.mojom.h"
#include "faced/session.h"

namespace faced {

// Enrollment session encapsulates the dependencies needed and operations
// performed during enrollment.
class EnrollmentSession
    : public SessionInterface,
      public chromeos::faceauth::mojom::FaceEnrollmentSession {
 public:
  static absl::StatusOr<std::unique_ptr<EnrollmentSession>> Create(
      absl::BitGen& bitgen,
      mojo::PendingReceiver<chromeos::faceauth::mojom::FaceEnrollmentSession>
          receiver,
      mojo::PendingRemote<
          chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate> delegate,
      chromeos::faceauth::mojom::EnrollmentSessionConfigPtr config,
      Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> client);

  ~EnrollmentSession() override = default;

  // Disallow copy and move.
  EnrollmentSession(const EnrollmentSession&) = delete;
  EnrollmentSession& operator=(const EnrollmentSession&) = delete;

  // `SessionInterface` implementation.
  uint64_t session_id() override { return session_id_; }
  void RegisterDisconnectHandler(
      DisconnectCallback disconnect_handler) override;

  // Notify FaceEnrollmentSessionDelegate of enrollment state changes.
  //
  // Notify of enrollment progress.
  void NotifyUpdate(chromeos::faceauth::mojom::FaceOperationStatus status);
  // Notify of completed enrollment and close the connection.
  void NotifyComplete(chromeos::faceauth::mojom::FaceOperationStatus status);
  // Notify of cancelled enrollment and close the connection.
  void NotifyCancelled();
  // Notify of unrecoverable error and close the connection.
  void NotifyError(absl::Status error);

 private:
  EnrollmentSession(
      uint64_t session_id,
      mojo::PendingReceiver<chromeos::faceauth::mojom::FaceEnrollmentSession>
          receiver,
      mojo::PendingRemote<
          chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate> delegate,
      Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> client);

  // Handle the disconnection of the session receiver.
  void OnSessionDisconnect();
  // Handle the disconnection of the remote delegate.
  void OnDelegateDisconnect();

  int64_t session_id_;
  mojo::Receiver<chromeos::faceauth::mojom::FaceEnrollmentSession> receiver_;
  mojo::Remote<chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate>
      delegate_;

  DisconnectCallback disconnect_callback_;

  // Async gRPC client that uses an internal completion queue.
  Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> rpc_client_;

  // Must be last member.
  base::WeakPtrFactory<EnrollmentSession> weak_ptr_factory_{this};
};

}  // namespace faced

#endif  // FACED_ENROLLMENT_SESSION_H_
