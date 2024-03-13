// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/functional/callback.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_blocks/biometrics_command_processor.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"

namespace cryptohome {

namespace {
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
}  // namespace

BiometricsAuthBlockService::BiometricsAuthBlockService(
    std::unique_ptr<BiometricsCommandProcessor> processor,
    base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>
        enroll_signal_sender,
    base::RepeatingCallback<void(user_data_auth::AuthScanDone)>
        auth_signal_sender)
    : processor_(std::move(processor)),
      enroll_signal_sender_(enroll_signal_sender),
      auth_signal_sender_(auth_signal_sender) {
  // Unretained is safe here because processor_ is owned by this.
  processor_->SetEnrollScanDoneCallback(
      base::BindRepeating(&BiometricsAuthBlockService::OnEnrollScanDone,
                          weak_factory_.GetWeakPtr()));
  processor_->SetAuthScanDoneCallback(base::BindRepeating(
      &BiometricsAuthBlockService::OnAuthScanDone, weak_factory_.GetWeakPtr()));
  processor_->SetSessionFailedCallback(
      base::BindRepeating(&BiometricsAuthBlockService::OnSessionFailed,
                          weak_factory_.GetWeakPtr()));
}

bool BiometricsAuthBlockService::IsReady() {
  return processor_->IsReady();
}

void BiometricsAuthBlockService::GetNonce(
    base::OnceCallback<void(std::optional<brillo::Blob>)> callback) {
  processor_->GetNonce(std::move(callback));
}

void BiometricsAuthBlockService::StartEnrollSession(
    AuthFactorType auth_factor_type,
    OperationInput payload,
    PreparedAuthFactorToken::Consumer on_done) {
  if (active_token_ || pending_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceStartEnrollConcurrentSession),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Set up a callback with the processor to check the start session result.
  pending_token_ =
      std::make_unique<Token>(auth_factor_type, Token::TokenType::kEnroll);
  processor_->StartEnrollSession(
      std::move(payload),
      base::BindOnce(&BiometricsAuthBlockService::CheckSessionStartResult,
                     weak_factory_.GetWeakPtr(), std::move(on_done)));
}

void BiometricsAuthBlockService::CreateCredential(OperationCallback on_done) {
  if (!active_token_ || active_token_->type() != Token::TokenType::kEnroll) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceCreateCredentialNoSession),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  processor_->CreateCredential(std::move(on_done));
}

void BiometricsAuthBlockService::EndEnrollSession() {
  if (!active_token_ || active_token_->type() != Token::TokenType::kEnroll) {
    return;
  }

  active_token_ = nullptr;
  processor_->EndEnrollSession();
}

void BiometricsAuthBlockService::StartAuthenticateSession(
    AuthFactorType auth_factor_type,
    ObfuscatedUsername obfuscated_username,
    OperationInput payload,
    PreparedAuthFactorToken::Consumer on_done) {
  // Starting an authenticate session again during an active session is allowed.
  if (active_token_ &&
      active_token_->type() != Token::TokenType::kAuthenticate) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceCheckStartConcurrentSession),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
    std::move(on_done).Run(std::move(status));
    return;
  }
  if (pending_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocBiometricsServiceStartAuthenticateConcurrentSession),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Set up a callback with the manager to check the start session result.
  pending_token_ = std::make_unique<Token>(auth_factor_type,
                                           Token::TokenType::kAuthenticate);
  processor_->StartAuthenticateSession(
      std::move(obfuscated_username), std::move(payload),
      base::BindOnce(&BiometricsAuthBlockService::CheckSessionStartResult,
                     weak_factory_.GetWeakPtr(), std::move(on_done)));
}

void BiometricsAuthBlockService::MatchCredential(OperationCallback on_done) {
  if (!active_token_ ||
      active_token_->type() != Token::TokenType::kAuthenticate) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceMatchCredentialNoSession),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  processor_->MatchCredential(std::move(on_done));
}

void BiometricsAuthBlockService::EndAuthenticateSession() {
  if (!active_token_ ||
      active_token_->type() != Token::TokenType::kAuthenticate) {
    return;
  }

  active_token_ = nullptr;
  processor_->EndAuthenticateSession();
}

void BiometricsAuthBlockService::DeleteCredential(
    ObfuscatedUsername obfuscated_username,
    const std::string& record_id,
    base::OnceCallback<void(DeleteResult)> on_done) {
  processor_->DeleteCredential(obfuscated_username, record_id,
                               std::move(on_done));
}

void BiometricsAuthBlockService::EnrollLegacyTemplate(
    AuthFactorType auth_factor_type,
    const std::string& template_id,
    OperationInput payload,
    StatusCallback on_done) {
  // Not allowed to enroll legacy template when the service is having an active
  // session.
  if (active_token_ || pending_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceMigrateFpConcurrentSession),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
    std::move(on_done).Run(std::move(status));
    return;
  }

  processor_->EnrollLegacyTemplate(
      template_id, std::move(payload),
      base::BindOnce(&BiometricsAuthBlockService::CheckEnrollLegacyResult,
                     weak_factory_.GetWeakPtr(), std::move(on_done)));
}

void BiometricsAuthBlockService::ListLegacyRecords(
    LegacyRecordsCallback on_done) {
  processor_->ListLegacyRecords(std::move(on_done));
}

BiometricsAuthBlockService::Token::Token(AuthFactorType auth_factor_type,
                                         TokenType token_type)
    : PreparedAuthFactorToken(auth_factor_type),
      token_type_(token_type),
      terminate_(*this) {}

void BiometricsAuthBlockService::Token::AttachToService(
    BiometricsAuthBlockService* service) {
  service_ = service;
}

void BiometricsAuthBlockService::Token::DetachFromService() {
  service_ = nullptr;
}

CryptohomeStatus BiometricsAuthBlockService::Token::TerminateAuthFactor() {
  if (service_) {
    switch (token_type_) {
      case TokenType::kEnroll:
        service_->EndEnrollSession();
        break;
      case TokenType::kAuthenticate:
        service_->EndAuthenticateSession();
        break;
    }
  }
  return OkStatus<CryptohomeError>();
}

void BiometricsAuthBlockService::CheckSessionStartResult(
    PreparedAuthFactorToken::Consumer on_done, bool success) {
  if (!pending_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceStartSessionNoToken),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(status));
    return;
  }
  std::unique_ptr<Token> token = std::move(pending_token_);
  if (!success) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceStartSessionFailure),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(status));
    return;
  }
  token->AttachToService(this);
  if (active_token_) {
    active_token_->DetachFromService();
  }
  active_token_ = token.get();
  std::move(on_done).Run(std::move(token));
}

void BiometricsAuthBlockService::CheckEnrollLegacyResult(StatusCallback on_done,
                                                         bool success) {
  if (!success) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceEnrollLegacyTemplateFailure),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(status));
    return;
  }
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void BiometricsAuthBlockService::OnEnrollScanDone(
    user_data_auth::AuthEnrollmentProgress signal) {
  // Process the signal either there is an active session or there is an
  // pending session since the signals could arrive as soon as the
  // session start reply and the session tranisition is yet to happen.
  Token* token = pending_token_ ? pending_token_.get() : active_token_;
  if (token && token->type() == Token::TokenType::kEnroll) {
    enroll_signal_sender_.Run(std::move(signal));
  }
}

void BiometricsAuthBlockService::OnAuthScanDone(
    user_data_auth::AuthScanDone signal) {
  // Process the signal either there is an active session or there is an
  // pending session since the signals could arrive as soon as the
  // session start reply and the session tranisition is yet to happen.
  Token* token = pending_token_ ? pending_token_.get() : active_token_;
  if (token && token->type() == Token::TokenType::kAuthenticate) {
    auth_signal_sender_.Run(std::move(signal));
  }
}

void BiometricsAuthBlockService::OnSessionFailed() {
  if (!active_token_) {
    return;
  }

  Token::TokenType type = active_token_->type();
  active_token_->DetachFromService();
  active_token_ = nullptr;
  // Use FINGERPRINT_SCAN_RESULT_FATAL_ERROR to indicate session failure. We
  // don't have to make an explicit end session call here because it's assumed
  // that the session will be ended itself when an error occurs.
  switch (type) {
    case Token::TokenType::kEnroll: {
      user_data_auth::AuthEnrollmentProgress enroll_signal;
      enroll_signal.mutable_scan_result()->set_fingerprint_result(
          user_data_auth::FINGERPRINT_SCAN_RESULT_FATAL_ERROR);
      enroll_signal_sender_.Run(std::move(enroll_signal));
      break;
    }
    case Token::TokenType::kAuthenticate: {
      user_data_auth::AuthScanDone auth_signal;
      auth_signal.mutable_scan_result()->set_fingerprint_result(
          user_data_auth::FINGERPRINT_SCAN_RESULT_FATAL_ERROR);
      auth_signal_sender_.Run(std::move(auth_signal));
      break;
    }
  }
}

}  // namespace cryptohome
