// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/async_challenge_credential_auth_block.h"

#include <memory>
#include <utility>
#include <variant>

#include <base/check.h>
#include <base/logging.h>
#include <base/notreached.h>

#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"

using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

AsyncChallengeCredentialAuthBlock::AsyncChallengeCredentialAuthBlock(
    ChallengeCredentialsHelper* challenge_credentials_helper,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    const std::string& account_id)
    : AuthBlock(kSignatureChallengeProtected),
      challenge_credentials_helper_(challenge_credentials_helper),
      key_challenge_service_(std::move(key_challenge_service)),
      account_id_(account_id) {
  CHECK(challenge_credentials_helper_);
  CHECK(key_challenge_service_);
}

void AsyncChallengeCredentialAuthBlock::Create(const AuthInput& auth_input,
                                               CreateCallback callback) {
  if (!key_challenge_service_) {
    LOG(ERROR) << __func__ << ": No valid key challenge service.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAsyncChalCredAuthBlockNoKeyServiceInCreate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }

  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << __func__ << ": No valid obfuscated username.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAsyncChalCredAuthBlockNoInputUserInCreate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }

  if (!auth_input.challenge_credential_auth_input.has_value()) {
    LOG(ERROR) << __func__ << ": No valid challenge credential auth input.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAsyncChalCredAuthBlockNoInputAuthInCreate),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }

  if (auth_input.challenge_credential_auth_input.value()
          .challenge_signature_algorithms.empty()) {
    LOG(ERROR) << __func__ << ": No valid challenge signature algorithms.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAsyncChalCredAuthBlockNoInputAlgInCreate),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }

  structure::ChallengePublicKeyInfo public_key_info{
      .public_key_spki_der = auth_input.challenge_credential_auth_input.value()
                                 .public_key_spki_der,
      .signature_algorithm = auth_input.challenge_credential_auth_input.value()
                                 .challenge_signature_algorithms,
  };

  const std::string& obfuscated_username =
      auth_input.obfuscated_username.value();

  challenge_credentials_helper_->GenerateNew(
      std::move(account_id_), std::move(public_key_info), obfuscated_username,
      std::move(key_challenge_service_),
      base::BindOnce(&AsyncChallengeCredentialAuthBlock::CreateContinue,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void AsyncChallengeCredentialAuthBlock::CreateContinue(
    CreateCallback callback,
    TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
        result) {
  if (!result.ok()) {
    LOG(ERROR) << __func__ << ": Failed to obtain challenge-response passkey.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockServiceGenerateFailedInCreate))
            .Wrap(std::move(result).status()),
        nullptr, nullptr);
    return;
  }

  ChallengeCredentialsHelper::GenerateNewOrDecryptResult result_val =
      std::move(result).value();
  std::unique_ptr<structure::SignatureChallengeInfo> signature_challenge_info =
      result_val.info();
  std::unique_ptr<brillo::SecureBlob> passkey = result_val.passkey();
  DCHECK(passkey);

  // We only need passkey for the AuthInput.
  AuthInput auth_input = {.user_input = std::move(*passkey)};

  auto key_blobs = std::make_unique<KeyBlobs>();
  auto scrypt_auth_state = std::make_unique<AuthBlockState>();

  LibScryptCompatAuthBlock scrypt_auth_block;
  CryptoStatus error = scrypt_auth_block.Create(
      auth_input, scrypt_auth_state.get(), key_blobs.get());

  if (!error.ok()) {
    LOG(ERROR) << __func__
               << "scrypt creation failed for challenge credential.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockCannotCreateScryptInCreate))
            .Wrap(std::move(error)),
        nullptr, nullptr);
    return;
  }

  if (auto* scrypt_state = std::get_if<LibScryptCompatAuthBlockState>(
          &scrypt_auth_state->state)) {
    ChallengeCredentialAuthBlockState cc_state = {
        .scrypt_state = std::move(*scrypt_state),
        .keyset_challenge_info = std::move(*signature_challenge_info),
    };

    auto auth_block_state = std::make_unique<AuthBlockState>(
        AuthBlockState{.state = std::move(cc_state)});

    std::move(callback).Run(OkStatus<CryptohomeCryptoError>(),
                            std::move(key_blobs), std::move(auth_block_state));
  } else {
    // This should never happen, but handling it anyway on the safe side.
    NOTREACHED() << "scrypt derivation failed for challenge credential.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockScryptDerivationFailedInCreate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
  }
}

void AsyncChallengeCredentialAuthBlock::Derive(const AuthInput& auth_input,
                                               const AuthBlockState& state,
                                               DeriveCallback callback) {
  if (!auth_input.challenge_credential_auth_input.has_value()) {
    LOG(ERROR) << __func__ << ": No valid challenge credential auth input.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAsyncChalCredAuthBlockNoInputAuthInDerive),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr);
    return;
  }

  if (!key_challenge_service_) {
    LOG(ERROR) << __func__ << ": No valid key challenge service.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAsyncChalCredAuthBlockNoKeyServiceInDerive),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr);
    return;
  }

  const ChallengeCredentialAuthBlockState* cc_state =
      std::get_if<ChallengeCredentialAuthBlockState>(&state.state);
  if (cc_state == nullptr) {
    LOG(ERROR) << __func__
               << "Invalid state for challenge credential AuthBlock.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockInvalidBlockStateInDerive),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_FATAL),
        nullptr);
    return;
  }

  if (!cc_state->keyset_challenge_info.has_value()) {
    LOG(ERROR)
        << __func__
        << "No signature challenge info in challenge credential AuthBlock.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockNoChallengeInfoInDerive),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr);
    return;
  }

  const structure::SignatureChallengeInfo& keyset_challenge_info =
      cc_state->keyset_challenge_info.value();
  if (!keyset_challenge_info.salt_signature_algorithm.has_value()) {
    LOG(ERROR)
        << __func__
        << "No signature algorithm info in challenge credential AuthBlock.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockNoAlgorithmInfoInDerive),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr);
    return;
  }

  structure::ChallengePublicKeyInfo public_key_info{
      .public_key_spki_der = keyset_challenge_info.public_key_spki_der,
      .signature_algorithm = auth_input.challenge_credential_auth_input.value()
                                 .challenge_signature_algorithms,
  };

  AuthBlockState scrypt_state = {.state = cc_state->scrypt_state};

  challenge_credentials_helper_->Decrypt(
      std::move(account_id_), std::move(public_key_info),
      cc_state->keyset_challenge_info.value(),
      std::move(key_challenge_service_),
      base::BindOnce(&AsyncChallengeCredentialAuthBlock::DeriveContinue,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(scrypt_state)));
  return;
}

void AsyncChallengeCredentialAuthBlock::DeriveContinue(
    DeriveCallback callback,
    const AuthBlockState& scrypt_state,
    TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
        result) {
  if (!result.ok()) {
    LOG(ERROR) << __func__ << ": Failed to obtain challenge-response passkey.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockServiceDeriveFailedInDerive))
            .Wrap(std::move(result).status()),
        nullptr);
    return;
  }

  ChallengeCredentialsHelper::GenerateNewOrDecryptResult result_val =
      std::move(result).value();
  std::unique_ptr<brillo::SecureBlob> passkey = result_val.passkey();
  DCHECK(passkey);

  // We only need passkey for the LibScryptCompatAuthBlock AuthInput.
  AuthInput auth_input = {.user_input = std::move(*passkey)};

  LibScryptCompatAuthBlock scrypt_auth_block;
  auto key_blobs = std::make_unique<KeyBlobs>();
  CryptoStatus error =
      scrypt_auth_block.Derive(auth_input, scrypt_state, key_blobs.get());

  if (!error.ok()) {
    LOG(ERROR) << __func__
               << "scrypt derivation failed for challenge credential.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAsyncChalCredAuthBlockScryptDeriveFailedInDerive))
            .Wrap(std::move(error)),
        nullptr);
    return;
  }

  std::move(callback).Run(OkStatus<CryptohomeCryptoError>(),
                          std::move(key_blobs));
  return;
}

}  // namespace cryptohome
