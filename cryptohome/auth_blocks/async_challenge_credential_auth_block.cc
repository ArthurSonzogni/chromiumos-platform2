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

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

AsyncChallengeCredentialAuthBlock::AsyncChallengeCredentialAuthBlock(
    Tpm* tpm,
    ChallengeCredentialsHelper* challenge_credentials_helper,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    const std::string& account_id)
    : AuthBlock(kSignatureChallengeProtected),
      tpm_(tpm),
      challenge_credentials_helper_(challenge_credentials_helper),
      key_challenge_service_(std::move(key_challenge_service)),
      account_id_(account_id) {
  CHECK(tpm_);
  CHECK(challenge_credentials_helper_);
  CHECK(key_challenge_service_);
}

void AsyncChallengeCredentialAuthBlock::Create(const AuthInput& auth_input,
                                               CreateCallback callback) {
  if (!key_challenge_service_) {
    LOG(ERROR) << __func__ << ": No valid key challenge service.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr, nullptr);
    return;
  }

  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << __func__ << ": No valid obfuscated username.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr, nullptr);
    return;
  }

  if (!auth_input.challenge_credential_auth_input.has_value()) {
    LOG(ERROR) << __func__ << ": No valid challenge credential auth input.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr, nullptr);
    return;
  }

  if (auth_input.challenge_credential_auth_input.value()
          .challenge_signature_algorithms.empty()) {
    LOG(ERROR) << __func__ << ": No valid challenge signature algorithms.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr, nullptr);
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

  std::map<uint32_t, brillo::Blob> default_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, false /* use_extended_pcr */);
  std::map<uint32_t, brillo::Blob> extended_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */);

  challenge_credentials_helper_->GenerateNew(
      std::move(account_id_), std::move(public_key_info),
      std::move(default_pcr_map), std::move(extended_pcr_map),
      std::move(key_challenge_service_),
      base::BindOnce(&AsyncChallengeCredentialAuthBlock::CreateContinue,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void AsyncChallengeCredentialAuthBlock::CreateContinue(
    CreateCallback callback,
    std::unique_ptr<structure::SignatureChallengeInfo> signature_challenge_info,
    std::unique_ptr<brillo::SecureBlob> passkey) {
  if (!passkey) {
    LOG(ERROR) << __func__ << ": Failed to obtain challenge-response passkey.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr, nullptr);
    return;
  }

  // We only need passkey for the AuthInput.
  AuthInput auth_input = {.user_input = std::move(*passkey)};

  auto key_blobs = std::make_unique<KeyBlobs>();
  auto scrypt_auth_state = std::make_unique<AuthBlockState>();

  LibScryptCompatAuthBlock scrypt_auth_block;
  CryptoError error = scrypt_auth_block.Create(
      auth_input, scrypt_auth_state.get(), key_blobs.get());

  if (error != CryptoError::CE_NONE) {
    LOG(ERROR) << __func__
               << "scrypt creation failed for challenge credential.";
    std::move(callback).Run(error, nullptr, nullptr);
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

    std::move(callback).Run(CryptoError::CE_NONE, std::move(key_blobs),
                            std::move(auth_block_state));
  } else {
    // This should never happen, but handling it anyway on the safe side.
    NOTREACHED() << "scrypt derivation failed for challenge credential.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr, nullptr);
  }
}

void AsyncChallengeCredentialAuthBlock::Derive(const AuthInput& auth_input,
                                               const AuthBlockState& state,
                                               DeriveCallback callback) {
  if (!key_challenge_service_) {
    LOG(ERROR) << __func__ << ": No valid key challenge service.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr);
    return;
  }

  const ChallengeCredentialAuthBlockState* cc_state =
      std::get_if<ChallengeCredentialAuthBlockState>(&state.state);
  if (cc_state == nullptr) {
    LOG(ERROR) << __func__
               << "Invalid state for challenge credential AuthBlock.";
    std::move(callback).Run(CryptoError::CE_OTHER_FATAL, nullptr);
    return;
  }

  if (!cc_state->keyset_challenge_info.has_value()) {
    LOG(ERROR)
        << __func__
        << "No signature challenge info in challenge credential AuthBlock.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr);
    return;
  }

  structure::ChallengePublicKeyInfo public_key_info{
      .public_key_spki_der =
          cc_state->keyset_challenge_info.value().public_key_spki_der,
      .signature_algorithm =
          {cc_state->keyset_challenge_info.value().salt_signature_algorithm},
  };

  AuthBlockState scrypt_state = {.state = cc_state->scrypt_state};

  challenge_credentials_helper_->Decrypt(
      std::move(account_id_), std::move(public_key_info),
      cc_state->keyset_challenge_info.value(),
      auth_input.locked_to_single_user.value_or(false),
      std::move(key_challenge_service_),
      base::BindOnce(&AsyncChallengeCredentialAuthBlock::DeriveContinue,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(scrypt_state)));
  return;
}

void AsyncChallengeCredentialAuthBlock::DeriveContinue(
    DeriveCallback callback,
    const AuthBlockState& scrypt_state,
    std::unique_ptr<brillo::SecureBlob> passkey) {
  if (!passkey) {
    LOG(ERROR) << __func__ << ": Failed to obtain challenge-response passkey.";
    std::move(callback).Run(CryptoError::CE_OTHER_CRYPTO, nullptr);
    return;
  }

  // We only need passkey for the LibScryptCompatAuthBlock AuthInput.
  AuthInput auth_input = {.user_input = std::move(*passkey)};

  LibScryptCompatAuthBlock scrypt_auth_block;
  auto key_blobs = std::make_unique<KeyBlobs>();
  CryptoError error =
      scrypt_auth_block.Derive(auth_input, scrypt_state, key_blobs.get());

  if (error != CryptoError::CE_NONE) {
    LOG(ERROR) << __func__
               << "scrypt derivation failed for challenge credential.";
    std::move(callback).Run(error, nullptr);
    return;
  }

  std::move(callback).Run(CryptoError::CE_NONE, std::move(key_blobs));
  return;
}

}  // namespace cryptohome
