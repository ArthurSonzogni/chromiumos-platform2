// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/session.h"

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

using ::chromeos::face_auth::mojom::AuthenticationSessionConfigPtr;
using ::chromeos::face_auth::mojom::CreateSessionResult;
using ::chromeos::face_auth::mojom::CreateSessionResultPtr;
using ::chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::face_auth::mojom::FaceOperationStatus;
using ::chromeos::face_auth::mojom::SessionCreationError;
using ::chromeos::face_auth::mojom::SessionError;
using ::chromeos::face_auth::mojom::SessionInfo;

// Generate a unique session ID.
//
// IDs should be used for debugging and diagnostics, and not security.
// We assume that the number of sessions during a single system boot is
// low enough that the probability of a collision is negligible.
uint64_t GenerateSessionId(absl::BitGen& bitgen) {
  return absl::Uniform<uint64_t>(bitgen);
}

}  // namespace

absl::StatusOr<std::unique_ptr<AuthenticationSession>>
AuthenticationSession::Create(
    absl::BitGen& bitgen,
    mojo::PendingRemote<FaceAuthenticationSessionDelegate> delegate,
    AuthenticationSessionConfigPtr config) {
  uint64_t session_id = GenerateSessionId(bitgen);

  // Using `new` to access private constructor of `AuthenticationSession`.
  std::unique_ptr<AuthenticationSession> session(
      new AuthenticationSession(session_id, std::move(delegate)));

  session->delegate_.set_disconnect_handler(
      base::BindOnce(&AuthenticationSession::OnDisconnect,
                     session->weak_ptr_factory_.GetWeakPtr()));

  return session;
}

AuthenticationSession::AuthenticationSession(
    uint64_t session_id,
    mojo::PendingRemote<FaceAuthenticationSessionDelegate> delegate)
    : session_id_(session_id), delegate_(std::move(delegate)) {}

void AuthenticationSession::RegisterDisconnectHandler(
    DisconnectCallback disconnect_handler) {
  disconnect_callback_ = std::move(disconnect_handler);
}

void AuthenticationSession::OnDisconnect() {
  // TODO(bkersten): cancel authentication session operation

  delegate_.reset();

  if (disconnect_callback_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(disconnect_callback_));
  }
}

}  // namespace faced
