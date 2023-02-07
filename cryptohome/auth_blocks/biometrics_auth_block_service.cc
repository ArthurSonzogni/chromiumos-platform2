// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/callback.h>
#include <base/notreached.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/biometrics_command_processor.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"

namespace cryptohome {

namespace {
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
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
  processor_->SetEnrollScanDoneCallback(base::BindRepeating(
      &BiometricsAuthBlockService::OnEnrollScanDone, base::Unretained(this)));
}

void BiometricsAuthBlockService::StartEnrollSession(
    AuthFactorType auth_factor_type,
    ObfuscatedUsername obfuscated_username,
    PreparedAuthFactorToken::Consumer on_done) {
  if (active_token_ || pending_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceStartEnrollConcurrentSession),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Set up a callback with the processor to check the start session result.
  pending_token_ =
      std::make_unique<Token>(auth_factor_type, Token::TokenType::kEnroll,
                              std::move(obfuscated_username));
  processor_->StartEnrollSession(
      base::BindOnce(&BiometricsAuthBlockService::CheckSessionStartResult,
                     base::Unretained(this), std::move(on_done)));
}

void BiometricsAuthBlockService::CreateCredential(OperationInput payload,
                                                  OperationCallback on_done) {
  if (!active_token_ || active_token_->type() != Token::TokenType::kEnroll) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceCreateCredentialNoSession),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  processor_->CreateCredential(active_token_->user_id(), std::move(payload),
                               std::move(on_done));
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
    PreparedAuthFactorToken::Consumer on_done) {
  NOTREACHED();
}

void BiometricsAuthBlockService::MatchCredential(OperationInput payload,
                                                 OperationCallback on_done) {
  NOTREACHED();
}

void BiometricsAuthBlockService::EndAuthenticateSession() {
  NOTREACHED();
}

std::optional<brillo::Blob> BiometricsAuthBlockService::TakeNonce() {
  return std::exchange(auth_nonce_, std::nullopt);
}

BiometricsAuthBlockService::Token::Token(AuthFactorType auth_factor_type,
                                         TokenType token_type,
                                         ObfuscatedUsername user_id)
    : PreparedAuthFactorToken(auth_factor_type),
      token_type_(token_type),
      user_id_(std::move(user_id)),
      terminate_(*this) {}

void BiometricsAuthBlockService::Token::AttachToService(
    BiometricsAuthBlockService* service) {
  service_ = service;
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
  if (active_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceCheckStartConcurrentSession),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
    std::move(on_done).Run(std::move(status));
    return;
  }
  if (!pending_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceStartSessionNoToken),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(status));
    return;
  }
  std::unique_ptr<Token> token = std::move(pending_token_);
  if (!success) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocBiometricsServiceStartSessionFailure),
        ErrorActionSet({ErrorAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(status));
    return;
  }
  token->AttachToService(this);
  active_token_ = token.get();
  std::move(on_done).Run(std::move(token));
}

void BiometricsAuthBlockService::OnEnrollScanDone(
    user_data_auth::AuthEnrollmentProgress signal,
    std::optional<brillo::Blob> nonce) {
  if (!active_token_ || active_token_->type() != Token::TokenType::kEnroll) {
    return;
  }

  if (nonce.has_value()) {
    auth_nonce_ = std::move(*nonce);
  }
  enroll_signal_sender_.Run(std::move(signal));
}

void BiometricsAuthBlockService::OnAuthScanDone(
    user_data_auth::AuthScanDone signal, brillo::Blob nonce) {
  NOTREACHED();
}

}  // namespace cryptohome
