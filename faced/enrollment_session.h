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
#include <base/callback_forward.h>
#include <brillo/grpc/async_grpc_client.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "faced/mojom/faceauth.mojom.h"
#include "faced/proto/face_service.grpc.pb.h"
#include "faced/proto/face_service.pb.h"
#include "faced/session.h"
#include "faced/util/lease.h"

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
  void Start(StartCallback callback) override;

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

  // Callback to process the response from StartEnrollment.
  void CompleteStartEnrollment(
      StartCallback callback,
      grpc::Status status,
      std::unique_ptr<faceauth::eora::StartEnrollmentResponse> response);

  using AbortCallback = base::OnceCallback<void(
      grpc::Status, std::unique_ptr<faceauth::eora::AbortEnrollmentResponse>)>;
  void AbortEnrollment(AbortCallback callback);

  // Callbacks to process the response from AbortEnrollment.
  void FinishOnSessionDisconnect(
      grpc::Status status,
      std::unique_ptr<faceauth::eora::AbortEnrollmentResponse> response);
  void FinishOnDelegateDisconnect(
      grpc::Status status,
      std::unique_ptr<faceauth::eora::AbortEnrollmentResponse> response);

  // Async gRPC client that uses an internal completion queue.
  Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> rpc_client_;

  // Must be last member.
  base::WeakPtrFactory<EnrollmentSession> weak_ptr_factory_{this};
};

}  // namespace faced

#endif  // FACED_ENROLLMENT_SESSION_H_
