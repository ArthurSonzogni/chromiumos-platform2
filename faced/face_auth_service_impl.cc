// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/face_auth_service_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <brillo/cryptohome.h>

#include "faced/authentication_session.h"
#include "faced/enrollment_session.h"
#include "faced/util/task.h"

namespace faced {

using ::brillo::cryptohome::home::IsSanitizedUserName;
using ::chromeos::faceauth::mojom::AuthenticationSessionConfigPtr;
using ::chromeos::faceauth::mojom::CreateSessionResult;
using ::chromeos::faceauth::mojom::CreateSessionResultPtr;
using ::chromeos::faceauth::mojom::EnrollmentSessionConfigPtr;
using ::chromeos::faceauth::mojom::FaceAuthenticationSession;
using ::chromeos::faceauth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::faceauth::mojom::FaceEnrollmentSession;
using ::chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::faceauth::mojom::Result;
using ::chromeos::faceauth::mojom::SessionCreationError;
using ::chromeos::faceauth::mojom::SessionInfo;

FaceAuthServiceImpl::FaceAuthServiceImpl(
    mojo::PendingReceiver<FaceAuthenticationService> receiver,
    DisconnectionCallback disconnect_handler,
    FaceServiceManagerInterface& manager,
    std::optional<base::FilePath> daemon_store_path)
    : receiver_(this, std::move(receiver)), face_service_manager_(manager) {
  receiver_.set_disconnect_handler(
      base::BindOnce(&FaceAuthServiceImpl::HandleDisconnect,
                     base::Unretained(this), std::move(disconnect_handler)));

  if (daemon_store_path) {
    enrollment_storage_ = EnrollmentStorage(daemon_store_path.value());
  }
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

  // Lease a client for communicating with FaceService.
  absl::StatusOr<Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>>>
      face_service_client = face_service_manager_.LeaseClient();

  // Check the client is valid.
  if (!face_service_client.ok()) {
    PostToCurrentSequence(base::BindOnce(
        std::move(callback),
        CreateSessionResult::NewError(SessionCreationError::UNKNOWN)));
    return;
  }

  // Create a new session, and register for callbacks when it is closed.
  absl::StatusOr<std::unique_ptr<EnrollmentSession>> session =
      EnrollmentSession::Create(bitgen_, std::move(receiver),
                                std::move(delegate), std::move(config),
                                std::move(face_service_client.value()));

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

  // Lease a client for communicating with FaceService.
  absl::StatusOr<Lease<brillo::AsyncGrpcClient<faceauth::eora::FaceService>>>
      face_service_client = face_service_manager_.LeaseClient();

  // Check the client is valid.
  if (!face_service_client.ok()) {
    PostToCurrentSequence(base::BindOnce(
        std::move(callback),
        CreateSessionResult::NewError(SessionCreationError::UNKNOWN)));
    return;
  }

  // Create a new session, and register for callbacks when it is closed.
  absl::StatusOr<std::unique_ptr<AuthenticationSession>> session =
      AuthenticationSession::Create(bitgen_, std::move(receiver),
                                    std::move(delegate), std::move(config),
                                    std::move(face_service_client.value()));

  // TODO(b/246196994): handle session creation error propagation

  session_ = std::move(session.value());
  session_->RegisterDisconnectHandler(base::BindOnce(
      &FaceAuthServiceImpl::ClearSession, base::Unretained(this)));

  // Return session information to the caller.
  CreateSessionResultPtr result(CreateSessionResult::NewSessionInfo(
      SessionInfo::New(session_->session_id())));
  PostToCurrentSequence(base::BindOnce(std::move(callback), std::move(result)));
}

void FaceAuthServiceImpl::ListEnrollments(ListEnrollmentsCallback callback) {
  PostToCurrentSequence(base::BindOnce(std::move(callback),
                                       enrollment_storage_.ListEnrollments()));
}

void FaceAuthServiceImpl::RemoveEnrollment(const std::string& hashed_username,
                                           RemoveEnrollmentCallback callback) {
  if (!IsSanitizedUserName(hashed_username)) {
    PostToCurrentSequence(base::BindOnce(std::move(callback), Result::ERROR));
    return;
  }

  if (!enrollment_storage_.RemoveEnrollment(hashed_username).ok()) {
    PostToCurrentSequence(base::BindOnce(std::move(callback), Result::ERROR));
    return;
  }

  PostToCurrentSequence(base::BindOnce(std::move(callback), Result::OK));
}

void FaceAuthServiceImpl::ClearEnrollments(ClearEnrollmentsCallback callback) {
  if (enrollment_storage_.ClearEnrollments().ok()) {
    PostToCurrentSequence(base::BindOnce(std::move(callback), Result::OK));
    return;
  }

  PostToCurrentSequence(base::BindOnce(std::move(callback), Result::ERROR));
}

void FaceAuthServiceImpl::IsUserEnrolled(const std::string& hashed_username,
                                         IsUserEnrolledCallback callback) {
  if (!IsSanitizedUserName(hashed_username)) {
    PostToCurrentSequence(base::BindOnce(std::move(callback), false));
    return;
  }

  PostToCurrentSequence(
      base::BindOnce(std::move(callback),
                     enrollment_storage_.IsUserEnrolled(hashed_username)));
}

}  // namespace faced
