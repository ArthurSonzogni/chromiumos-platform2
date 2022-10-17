// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/enrollment_session.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <absl/status/status.h>
#include <base/bind.h>
#include <base/check.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "faced/common/face_status.h"
#include "faced/mojom/faceauth.mojom.h"
#include "faced/proto/face_service.grpc.pb.h"
#include "faced/proto/face_service.pb.h"
#include "faced/util/task.h"

namespace faced {

using ::chromeos::faceauth::mojom::EnrollmentCompleteMessage;
using ::chromeos::faceauth::mojom::EnrollmentCompleteMessagePtr;
using ::chromeos::faceauth::mojom::EnrollmentSessionConfigPtr;
using ::chromeos::faceauth::mojom::EnrollmentUpdateMessage;
using ::chromeos::faceauth::mojom::EnrollmentUpdateMessagePtr;
using ::chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::faceauth::mojom::FaceOperationStatus;
using ::chromeos::faceauth::mojom::SessionError;

using ::faceauth::eora::AbortEnrollmentRequest;
using ::faceauth::eora::AbortEnrollmentResponse;
using ::faceauth::eora::StartEnrollmentRequest;
using ::faceauth::eora::StartEnrollmentResponse;

absl::StatusOr<std::unique_ptr<EnrollmentSession>> EnrollmentSession::Create(
    absl::BitGen& bitgen,
    mojo::PendingReceiver<FaceEnrollmentSession> receiver,
    mojo::PendingRemote<FaceEnrollmentSessionDelegate> delegate,
    EnrollmentSessionConfigPtr config,
    Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> client) {
  uint64_t session_id = GenerateSessionId(bitgen);

  // Using `new` to access private constructor of `EnrollmentSession`.
  std::unique_ptr<EnrollmentSession> session(new EnrollmentSession(
      session_id, std::move(receiver), std::move(delegate), std::move(client)));

  session->delegate_.set_disconnect_handler(
      base::BindOnce(&EnrollmentSession::OnDelegateDisconnect,
                     base::Unretained(session.get())));

  session->receiver_.set_disconnect_handler(
      base::BindOnce(&EnrollmentSession::OnSessionDisconnect,
                     base::Unretained(session.get())));

  return session;
}

EnrollmentSession::EnrollmentSession(
    uint64_t session_id,
    mojo::PendingReceiver<FaceEnrollmentSession> receiver,
    mojo::PendingRemote<FaceEnrollmentSessionDelegate> delegate,
    Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>> client)
    : session_id_(session_id),
      receiver_(this, std::move(receiver)),
      delegate_(std::move(delegate)),
      rpc_client_(std::move(client)) {}

void EnrollmentSession::RegisterCompletionHandler(
    CompletionCallback completion_handler) {
  completion_callback_ = std::move(completion_handler);
}

void EnrollmentSession::NotifyUpdate(FaceOperationStatus status) {
  EnrollmentUpdateMessagePtr message(
      EnrollmentUpdateMessage::New(status, /*poses=*/std::vector<bool>()));
  delegate_->OnEnrollmentUpdate(std::move(message));
}

void EnrollmentSession::NotifyComplete() {
  delegate_->OnEnrollmentComplete(EnrollmentCompleteMessage::New());

  FinishSession();
}

void EnrollmentSession::NotifyCancelled() {
  delegate_->OnEnrollmentCancelled();

  FinishSession();
}

void EnrollmentSession::NotifyError(absl::Status error) {
  // TODO(bkersten): map absl::Status to SessionError
  SessionError session_error = SessionError::UNKNOWN;
  delegate_->OnEnrollmentError(session_error);

  FinishSession();
}

void EnrollmentSession::Start(StartCallback callback) {
  (*rpc_client_)
      ->CallRpc(&faceauth::eora::FaceService::Stub::AsyncStartEnrollment,
                StartEnrollmentRequest(),
                base::BindOnce(&EnrollmentSession::CompleteStartEnrollment,
                               base::Unretained(this), std::move(callback)));
}

void EnrollmentSession::CompleteStartEnrollment(
    StartCallback callback,
    grpc::Status status,
    std::unique_ptr<StartEnrollmentResponse> response) {
  // Ensure the StartEnrollment RPC succeeded.
  if (!status.ok()) {
    PostToCurrentSequence(base::BindOnce(
        std::move(callback), absl::UnavailableError(status.error_message())));
    return;
  }

  absl::Status rpc_status = ToAbslStatus(response->status());
  PostToCurrentSequence(base::BindOnce(std::move(callback), rpc_status));
}

void EnrollmentSession::OnSessionDisconnect() {
  // Remove delegate disconnection handler once abort is in progress
  delegate_.reset_on_disconnect();
  receiver_.reset();

  AbortEnrollment(base::BindOnce(&EnrollmentSession::FinishOnSessionDisconnect,
                                 base::Unretained(this)));
}

void EnrollmentSession::OnDelegateDisconnect() {
  receiver_.reset();
  delegate_.reset();

  AbortEnrollment(base::BindOnce(&EnrollmentSession::FinishOnDelegateDisconnect,
                                 base::Unretained(this)));
}

void EnrollmentSession::AbortEnrollment(AbortCallback callback) {
  (*rpc_client_)
      ->CallRpc(&faceauth::eora::FaceService::Stub::AsyncAbortEnrollment,
                AbortEnrollmentRequest(), std::move(callback));
}

void EnrollmentSession::FinishOnSessionDisconnect(
    grpc::Status status, std::unique_ptr<AbortEnrollmentResponse> response) {
  if (!delegate_.is_bound()) {
    VLOG(1) << "Cannot notify of session disconnect as delegate is not bound.";
    FinishSession();
    return;
  }

  if (!status.ok()) {
    NotifyError(absl::UnavailableError(status.error_message()));
    return;
  }

  absl::Status rpc_status = ToAbslStatus(response->status());
  if (!rpc_status.ok()) {
    NotifyError(rpc_status);
    return;
  }

  NotifyCancelled();
}

void EnrollmentSession::FinishOnDelegateDisconnect(
    grpc::Status status,
    std::unique_ptr<faceauth::eora::AbortEnrollmentResponse> response) {
  FinishSession();
}

void EnrollmentSession::FinishSession() {
  // Close the connections to the enrollment session interfaces.
  delegate_.reset();
  receiver_.reset();

  if (completion_callback_) {
    PostToCurrentSequence(std::move(completion_callback_));
  }
}

}  // namespace faced
