// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/face_auth_service_impl.h"

#include <cstdint>
#include <utility>

#include <base/bind.h>

#include "faced/session.h"

namespace faced {

using ::chromeos::face_auth::mojom::AuthenticationSessionConfigPtr;
using ::chromeos::face_auth::mojom::CreateSessionResult;
using ::chromeos::face_auth::mojom::CreateSessionResultPtr;
using ::chromeos::face_auth::mojom::EnrollmentSessionConfigPtr;
using ::chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::face_auth::mojom::SessionCreationError;
using ::chromeos::face_auth::mojom::SessionInfo;

FaceAuthServiceImpl::FaceAuthServiceImpl(
    mojo::PendingReceiver<FaceAuthenticationService> receiver,
    DisconnectionCallback disconnect_handler)
    : receiver_(this, std::move(receiver)) {
  receiver_.set_disconnect_handler(
      base::BindOnce(&FaceAuthServiceImpl::HandleDisconnect,
                     base::Unretained(this), std::move(disconnect_handler)));
}

void FaceAuthServiceImpl::HandleDisconnect(base::OnceClosure callback) {
  ClearSession();
  receiver_.reset();
  base::SequencedTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                   std::move(callback));
}

void FaceAuthServiceImpl::CreateEnrollmentSession(
    EnrollmentSessionConfigPtr config,
    ::mojo::PendingRemote<FaceEnrollmentSessionDelegate> delegate,
    CreateEnrollmentSessionCallback callback) {
  // If a session is already active, return an error.
  if (session_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  CreateSessionResult::NewError(
                                      SessionCreationError::ALREADY_EXISTS)));
    return;
  }

  // Create a new session, and register for callbacks when it is closed.
  absl::StatusOr<std::unique_ptr<EnrollmentSession>> session =
      EnrollmentSession::Create(bitgen_, std::move(delegate),
                                std::move(config));

  // TODO(b/246196994): handle session creation error propagation

  session_ = std::move(session.value());
  session_->RegisterDisconnectHandler(base::BindOnce(
      &FaceAuthServiceImpl::ClearSession, base::Unretained(this)));

  // Return session information to the caller.
  CreateSessionResultPtr result(CreateSessionResult::NewSessionInfo(
      SessionInfo::New(session_->session_id())));
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(result)));
}

void FaceAuthServiceImpl::CreateAuthenticationSession(
    AuthenticationSessionConfigPtr config,
    ::mojo::PendingRemote<FaceAuthenticationSessionDelegate> delegate,
    CreateAuthenticationSessionCallback callback) {
  // If a session is already active, return an error.
  if (session_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  CreateSessionResult::NewError(
                                      SessionCreationError::ALREADY_EXISTS)));
    return;
  }

  // Create a new session, and register for callbacks when it is closed.
  absl::StatusOr<std::unique_ptr<AuthenticationSession>> session =
      AuthenticationSession::Create(bitgen_, std::move(delegate),
                                    std::move(config));

  // TODO(b/246196994): handle session creation error propagation

  session_ = std::move(session.value());
  session_->RegisterDisconnectHandler(base::BindOnce(
      &FaceAuthServiceImpl::ClearSession, base::Unretained(this)));

  // Return session information to the caller.
  CreateSessionResultPtr result(CreateSessionResult::NewSessionInfo(
      SessionInfo::New(session_->session_id())));
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(result)));
}

}  // namespace faced
