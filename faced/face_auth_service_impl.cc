// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/face_auth_service_impl.h"

#include <cstdint>
#include <utility>

#include <base/bind.h>

#include "faced/authentication_session.h"
#include "faced/enrollment_session.h"
#include "faced/util/task.h"

namespace faced {

using ::chromeos::faceauth::mojom::AuthenticationSessionConfigPtr;
using ::chromeos::faceauth::mojom::CreateSessionResult;
using ::chromeos::faceauth::mojom::CreateSessionResultPtr;
using ::chromeos::faceauth::mojom::EnrollmentSessionConfigPtr;
using ::chromeos::faceauth::mojom::FaceAuthenticationSession;
using ::chromeos::faceauth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::faceauth::mojom::FaceEnrollmentSession;
using ::chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::faceauth::mojom::SessionCreationError;
using ::chromeos::faceauth::mojom::SessionInfo;

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
  PostToCurrentSequence(std::move(callback));
}

void FaceAuthServiceImpl::CreateEnrollmentSession(
    EnrollmentSessionConfigPtr config,
    ::mojo::PendingReceiver<FaceEnrollmentSession> receiver,
    ::mojo::PendingRemote<FaceEnrollmentSessionDelegate> delegate,
    CreateEnrollmentSessionCallback callback) {
  // If a session is already active, return an error.
  if (session_) {
    PostToCurrentSequence(base::BindOnce(
        std::move(callback),
        CreateSessionResult::NewError(SessionCreationError::ALREADY_EXISTS)));
    return;
  }

  // Create a new session, and register for callbacks when it is closed.
  absl::StatusOr<std::unique_ptr<EnrollmentSession>> session =
      EnrollmentSession::Create(bitgen_, std::move(receiver),
                                std::move(delegate), std::move(config));

  // TODO(b/246196994): handle session creation error propagation

  session_ = std::move(session.value());
  session_->RegisterDisconnectHandler(base::BindOnce(
      &FaceAuthServiceImpl::ClearSession, base::Unretained(this)));

  // Return session information to the caller.
  CreateSessionResultPtr result(CreateSessionResult::NewSessionInfo(
      SessionInfo::New(session_->session_id())));
  PostToCurrentSequence(base::BindOnce(std::move(callback), std::move(result)));
}

void FaceAuthServiceImpl::CreateAuthenticationSession(
    AuthenticationSessionConfigPtr config,
    ::mojo::PendingReceiver<FaceAuthenticationSession> receiver,
    ::mojo::PendingRemote<FaceAuthenticationSessionDelegate> delegate,
    CreateAuthenticationSessionCallback callback) {
  // If a session is already active, return an error.
  if (session_) {
    PostToCurrentSequence(base::BindOnce(
        std::move(callback),
        CreateSessionResult::NewError(SessionCreationError::ALREADY_EXISTS)));
    return;
  }

  // Create a new session, and register for callbacks when it is closed.
  absl::StatusOr<std::unique_ptr<AuthenticationSession>> session =
      AuthenticationSession::Create(bitgen_, std::move(receiver),
                                    std::move(delegate), std::move(config));

  // TODO(b/246196994): handle session creation error propagation

  session_ = std::move(session.value());
  session_->RegisterDisconnectHandler(base::BindOnce(
      &FaceAuthServiceImpl::ClearSession, base::Unretained(this)));

  // Return session information to the caller.
  CreateSessionResultPtr result(CreateSessionResult::NewSessionInfo(
      SessionInfo::New(session_->session_id())));
  PostToCurrentSequence(base::BindOnce(std::move(callback), std::move(result)));
}

}  // namespace faced
