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
#include <base/location.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "faced/mojom/face_auth.mojom.h"

namespace faced {

namespace {

using ::chromeos::face_auth::mojom::EnrollmentCompleteMessage;
using ::chromeos::face_auth::mojom::EnrollmentCompleteMessagePtr;
using ::chromeos::face_auth::mojom::EnrollmentSessionConfigPtr;
using ::chromeos::face_auth::mojom::EnrollmentUpdateMessage;
using ::chromeos::face_auth::mojom::EnrollmentUpdateMessagePtr;
using ::chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::face_auth::mojom::FaceOperationStatus;
using ::chromeos::face_auth::mojom::SessionError;

// Generate a unique session ID.
//
// IDs should be used for debugging and diagnostics, and not security.
// We assume that the number of sessions during a single system boot is
// low enough that the probability of a collision is negligible.
uint64_t GenerateSessionId(absl::BitGen& bitgen) {
  return absl::Uniform<uint64_t>(bitgen);
}

}  // namespace

absl::StatusOr<std::unique_ptr<EnrollmentSession>> EnrollmentSession::Create(
    absl::BitGen& bitgen,
    mojo::PendingReceiver<FaceEnrollmentSession> receiver,
    mojo::PendingRemote<FaceEnrollmentSessionDelegate> delegate,
    EnrollmentSessionConfigPtr config) {
  uint64_t session_id = GenerateSessionId(bitgen);

  // Using `new` to access private constructor of `EnrollmentSession`.
  std::unique_ptr<EnrollmentSession> session(new EnrollmentSession(
      session_id, std::move(receiver), std::move(delegate)));

  session->delegate_.set_disconnect_handler(
      base::BindOnce(&EnrollmentSession::OnDelegateDisconnect,
                     session->weak_ptr_factory_.GetWeakPtr()));

  session->receiver_.set_disconnect_handler(
      base::BindOnce(&EnrollmentSession::OnSessionDisconnect,
                     session->weak_ptr_factory_.GetWeakPtr()));

  return session;
}

EnrollmentSession::EnrollmentSession(
    uint64_t session_id,
    mojo::PendingReceiver<FaceEnrollmentSession> receiver,
    mojo::PendingRemote<FaceEnrollmentSessionDelegate> delegate)
    : session_id_(session_id),
      receiver_(this, std::move(receiver)),
      delegate_(std::move(delegate)) {}

void EnrollmentSession::RegisterDisconnectHandler(
    DisconnectCallback disconnect_handler) {
  disconnect_callback_ = std::move(disconnect_handler);
}

void EnrollmentSession::NotifyUpdate(FaceOperationStatus status) {
  EnrollmentUpdateMessagePtr message(
      EnrollmentUpdateMessage::New(status, /*poses=*/std::vector<bool>()));
  delegate_->OnEnrollmentUpdate(std::move(message));
}

void EnrollmentSession::NotifyComplete(FaceOperationStatus status) {
  EnrollmentCompleteMessagePtr message(EnrollmentCompleteMessage::New(status));
  delegate_->OnEnrollmentComplete(std::move(message));

  // Close connection to delegate interface
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

void EnrollmentSession::NotifyCancelled() {
  delegate_->OnEnrollmentCancelled();

  // Close connection to delegate interface
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}
void EnrollmentSession::NotifyError(absl::Status error) {
  // TODO(bkersten): map absl::Status to SessionError
  SessionError session_error = SessionError::UNKNOWN;
  delegate_->OnEnrollmentError(session_error);

  // Close connection to delegate interface
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

void EnrollmentSession::OnSessionDisconnect() {
  receiver_.reset();

  // TODO(bkersten): cancel enrollment session operation

  NotifyCancelled();
}

void EnrollmentSession::OnDelegateDisconnect() {
  // TODO(bkersten): cancel enrollment session operation

  receiver_.reset();
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

}  // namespace faced
