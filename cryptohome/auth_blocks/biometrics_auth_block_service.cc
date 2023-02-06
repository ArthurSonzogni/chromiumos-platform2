// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <absl/cleanup/cleanup.h>
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
      auth_signal_sender_(auth_signal_sender) {}

void BiometricsAuthBlockService::StartEnrollSession(
    AuthFactorType auth_factor_type,
    std::string obfuscated_username,
    PreparedAuthFactorToken::Consumer on_done) {
  NOTREACHED();
}

void BiometricsAuthBlockService::CreateCredential(OperationInput payload,
                                                  OperationCallback on_done) {
  NOTREACHED();
}

void BiometricsAuthBlockService::EndEnrollSession() {
  NOTREACHED();
}

void BiometricsAuthBlockService::StartAuthenticateSession(
    AuthFactorType auth_factor_type,
    std::string obfuscated_username,
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
  NOTREACHED();
  return std::nullopt;
}

BiometricsAuthBlockService::Token::Token(AuthFactorType auth_factor_type,
                                         TokenType token_type,
                                         std::string user_id)
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

}  // namespace cryptohome
