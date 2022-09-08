// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/authentication_session.h"

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

using ::chromeos::face_auth::mojom::AuthenticationCompleteMessage;
using ::chromeos::face_auth::mojom::AuthenticationCompleteMessagePtr;
using ::chromeos::face_auth::mojom::AuthenticationSessionConfigPtr;
using ::chromeos::face_auth::mojom::AuthenticationUpdateMessage;
using ::chromeos::face_auth::mojom::AuthenticationUpdateMessagePtr;
using ::chromeos::face_auth::mojom::FaceAuthenticationSession;
using ::chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::face_auth::mojom::FaceOperationStatus;
using ::chromeos::face_auth::mojom::SessionCreationError;
using ::chromeos::face_auth::mojom::SessionError;
using ::chromeos::face_auth::mojom::SessionInfo;

absl::StatusOr<std::unique_ptr<AuthenticationSession>>
AuthenticationSession::Create(
    absl::BitGen& bitgen,
    mojo::PendingReceiver<FaceAuthenticationSession> receiver,
    mojo::PendingRemote<FaceAuthenticationSessionDelegate> delegate,
    AuthenticationSessionConfigPtr config) {
  uint64_t session_id = GenerateSessionId(bitgen);

  // Using `new` to access private constructor of `AuthenticationSession`.
  std::unique_ptr<AuthenticationSession> session(new AuthenticationSession(
      session_id, std::move(receiver), std::move(delegate)));

  session->delegate_.set_disconnect_handler(
      base::BindOnce(&AuthenticationSession::OnDelegateDisconnect,
                     session->weak_ptr_factory_.GetWeakPtr()));

  session->receiver_.set_disconnect_handler(
      base::BindOnce(&AuthenticationSession::OnSessionDisconnect,
                     session->weak_ptr_factory_.GetWeakPtr()));

  return session;
}

AuthenticationSession::AuthenticationSession(
    uint64_t session_id,
    mojo::PendingReceiver<FaceAuthenticationSession> receiver,
    mojo::PendingRemote<FaceAuthenticationSessionDelegate> delegate)
    : session_id_(session_id),
      receiver_(this, std::move(receiver)),
      delegate_(std::move(delegate)) {}

void AuthenticationSession::RegisterDisconnectHandler(
    DisconnectCallback disconnect_handler) {
  disconnect_callback_ = std::move(disconnect_handler);
}

void AuthenticationSession::NotifyUpdate(FaceOperationStatus status) {
  AuthenticationUpdateMessagePtr message(
      AuthenticationUpdateMessage::New(status));
  delegate_->OnAuthenticationUpdate(std::move(message));
}

void AuthenticationSession::NotifyComplete(FaceOperationStatus status) {
  AuthenticationCompleteMessagePtr message(
      AuthenticationCompleteMessage::New(status));
  delegate_->OnAuthenticationComplete(std::move(message));

  // Close connection to delegate interface
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

void AuthenticationSession::NotifyCancelled() {
  delegate_->OnAuthenticationCancelled();

  // Close connection to delegate interface
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

void AuthenticationSession::NotifyError(absl::Status error) {
  // TODO(bkersten): map absl::Status to SessionError
  SessionError session_error = SessionError::UNKNOWN;
  delegate_->OnAuthenticationError(session_error);

  // Close connection to delegate interface
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

void AuthenticationSession::OnSessionDisconnect() {
  receiver_.reset();

  // TODO(bkersten): cancel authentication session operation

  NotifyCancelled();
}

void AuthenticationSession::OnDelegateDisconnect() {
  // TODO(bkersten): cancel authentication session operation

  receiver_.reset();
  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

}  // namespace faced
