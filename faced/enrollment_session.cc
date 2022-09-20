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

#include "faced/mojom/faceauth.mojom.h"

namespace faced {

using ::chromeos::faceauth::mojom::EnrollmentCompleteMessage;
using ::chromeos::faceauth::mojom::EnrollmentCompleteMessagePtr;
using ::chromeos::faceauth::mojom::EnrollmentSessionConfigPtr;
using ::chromeos::faceauth::mojom::EnrollmentUpdateMessage;
using ::chromeos::faceauth::mojom::EnrollmentUpdateMessagePtr;
using ::chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::faceauth::mojom::FaceOperationStatus;
using ::chromeos::faceauth::mojom::SessionError;

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
