// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>
#include <chromeos/constants/cryptohome.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/challenge_credential_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

namespace {

struct AuthBlockFlags {
  int32_t require_flags;
  int32_t refuse_flags;
  AuthBlockType auth_block_type;
};

constexpr AuthBlockFlags kPinWeaverFlags = {
    .require_flags = SerializedVaultKeyset::LE_CREDENTIAL,
    .refuse_flags = 0,
    .auth_block_type = AuthBlockType::kPinWeaver,
};

constexpr AuthBlockFlags kChallengeCredentialFlags = {
    .require_flags = SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED,
    .refuse_flags = 0,
    .auth_block_type = AuthBlockType::kChallengeCredential,
};

constexpr AuthBlockFlags kDoubleWrappedCompatFlags = {
    .require_flags = SerializedVaultKeyset::SCRYPT_WRAPPED |
                     SerializedVaultKeyset::TPM_WRAPPED,
    .refuse_flags = 0,
    .auth_block_type = AuthBlockType::kDoubleWrappedCompat,
};

constexpr AuthBlockFlags kLibScryptCompatFlags = {
    .require_flags = SerializedVaultKeyset::SCRYPT_WRAPPED,
    .refuse_flags = SerializedVaultKeyset::TPM_WRAPPED,
    .auth_block_type = AuthBlockType::kLibScryptCompat,
};

constexpr AuthBlockFlags kTpmNotBoundToPcrFlags = {
    .require_flags = SerializedVaultKeyset::TPM_WRAPPED,
    .refuse_flags = SerializedVaultKeyset::SCRYPT_WRAPPED |
                    SerializedVaultKeyset::PCR_BOUND |
                    SerializedVaultKeyset::ECC,
    .auth_block_type = AuthBlockType::kTpmNotBoundToPcr,
};

constexpr AuthBlockFlags kTpmBoundToPcrFlags = {
    .require_flags =
        SerializedVaultKeyset::TPM_WRAPPED | SerializedVaultKeyset::PCR_BOUND,
    .refuse_flags =
        SerializedVaultKeyset::SCRYPT_WRAPPED | SerializedVaultKeyset::ECC,
    .auth_block_type = AuthBlockType::kTpmBoundToPcr,
};

constexpr AuthBlockFlags kTpmEccFlags = {
    .require_flags = SerializedVaultKeyset::TPM_WRAPPED |
                     SerializedVaultKeyset::SCRYPT_DERIVED |
                     SerializedVaultKeyset::PCR_BOUND |
                     SerializedVaultKeyset::ECC,
    .refuse_flags = SerializedVaultKeyset::SCRYPT_WRAPPED,
    .auth_block_type = AuthBlockType::kTpmEcc,
};

constexpr AuthBlockFlags auth_block_flags[] = {
    kPinWeaverFlags,       kChallengeCredentialFlags, kDoubleWrappedCompatFlags,
    kLibScryptCompatFlags, kTpmNotBoundToPcrFlags,    kTpmBoundToPcrFlags,
    kTpmEccFlags};

bool MatchFlags(AuthBlockFlags AuthBlockFlags, int32_t flags) {
  return (flags & AuthBlockFlags.require_flags) ==
             AuthBlockFlags.require_flags &&
         (flags & AuthBlockFlags.refuse_flags) == 0;
}

}  // namespace

AuthBlockUtilityImpl::AuthBlockUtilityImpl(KeysetManagement* keyset_management,
                                           Crypto* crypto,
                                           Platform* platform)
    : keyset_management_(keyset_management),
      crypto_(crypto),
      platform_(platform) {
  DCHECK(keyset_management);
  DCHECK(crypto_);
  DCHECK(platform_);
}

AuthBlockUtilityImpl::~AuthBlockUtilityImpl() = default;

bool AuthBlockUtilityImpl::GetLockedToSingleUser() {
  return platform_->FileExists(base::FilePath(kLockedToSingleUserFile));
}

CryptoError AuthBlockUtilityImpl::CreateKeyBlobsWithAuthBlock(
    AuthBlockType auth_block_type,
    const Credentials& credentials,
    const std::optional<brillo::SecureBlob>& reset_secret,
    AuthBlockState& out_state,
    KeyBlobs& out_key_blobs) {
  std::unique_ptr<SyncAuthBlock> auth_block =
      GetAuthBlockWithType(auth_block_type);
  if (!auth_block) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    return CryptoError::CE_OTHER_CRYPTO;
  }
  ReportCreateAuthBlock(auth_block_type);

  // |reset_secret| is not processed in the AuthBlocks, the value is copied to
  // the |out_key_blobs| directly. |reset_secret| will be added to the
  // |out_key_blobs| in the VaultKeyset, if missing.
  AuthInput user_input = {
      credentials.passkey(),
      /*locked_to_single_user*=*/std::nullopt,
      brillo::cryptohome::home::SanitizeUserName(credentials.username()),
      reset_secret};

  CryptoError error =
      auth_block->Create(user_input, &out_state, &out_key_blobs);
  if (error != CryptoError::CE_NONE) {
    LOG(ERROR) << "Failed to create per credential secret: " << error;
    return error;
  }

  ReportWrappingKeyDerivationType(auth_block->derivation_type(),
                                  CryptohomePhase::kCreated);

  return error;
}

CryptoError AuthBlockUtilityImpl::DeriveKeyBlobsWithAuthBlock(
    AuthBlockType auth_block_type,
    const Credentials& credentials,
    const AuthBlockState& auth_state,
    KeyBlobs& out_key_blobs) {
  DCHECK_NE(auth_block_type, AuthBlockType::kMaxValue);

  CryptoError error = CryptoError::CE_NONE;

  AuthInput auth_input = {credentials.passkey(),
                          /*locked_to_single_user=*/std::nullopt};

  auth_input.locked_to_single_user = GetLockedToSingleUser();

  std::unique_ptr<SyncAuthBlock> auth_block =
      GetAuthBlockWithType(auth_block_type);
  if (!auth_block) {
    LOG(ERROR) << "Keyset wrapped with unknown method.";
    return CryptoError::CE_OTHER_CRYPTO;
  }
  ReportDeriveAuthBlock(auth_block_type);

  error = auth_block->Derive(auth_input, auth_state, &out_key_blobs);
  if (error == CryptoError::CE_NONE) {
    ReportWrappingKeyDerivationType(auth_block->derivation_type(),
                                    CryptohomePhase::kMounted);
    return CryptoError::CE_NONE;
  }
  LOG(ERROR) << "Failed to derive per credential secret: " << error;

  // For LE credentials, if deriving the key blobs failed due to too many
  // attempts, set auth_locked=true in the corresponding keyset. Then save it
  // for future callers who can Load it w/o Decrypt'ing to check that flag.
  // When the pin is entered wrong and AuthBlock fails to derive the KeyBlobs
  // it doesn't make it into the VaultKeyset::Decrypt(); so auth_lock should
  // be set here.
  if (auth_block_type == AuthBlockType::kPinWeaver &&
      error == CryptoError::CE_TPM_DEFEND_LOCK) {
    // Get the corresponding encrypted vault keyset for the user and the label
    // to set the auth_locked.
    std::unique_ptr<VaultKeyset> vk = keyset_management_->GetVaultKeyset(
        brillo::cryptohome::home::SanitizeUserName(credentials.username()),
        credentials.key_data().label());

    if (vk == nullptr) {
      LOG(ERROR)
          << "No vault keyset is found on disk for the given label. Cannot "
             "decide on the AuthBlock type without vault keyset metadata.";
      return CryptoError::CE_OTHER_CRYPTO;
    }
    vk->SetAuthLocked(true);
    vk->Save(vk->GetSourceFile());
  }
  return error;
}

AuthBlockType AuthBlockUtilityImpl::GetAuthBlockTypeForCreation(
    const Credentials& credentials) const {
  bool is_le_credential =
      credentials.key_data().policy().low_entropy_credential();

  if (is_le_credential) {
    return AuthBlockType::kPinWeaver;
  }

  bool is_challenge_credential =
      credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE;
  if (is_challenge_credential) {
    return AuthBlockType::kChallengeCredential;
  }

  bool use_tpm = crypto_->tpm() && crypto_->tpm()->IsOwned();
  bool with_user_auth = crypto_->CanUnsealWithUserAuth();
  bool has_ecc_key = crypto_->cryptohome_keys_manager() &&
                     crypto_->cryptohome_keys_manager()->HasCryptohomeKey(
                         CryptohomeKeyType::kECC);

  if (use_tpm && with_user_auth && has_ecc_key) {
    return AuthBlockType::kTpmEcc;
  }

  if (use_tpm && with_user_auth && !has_ecc_key) {
    return AuthBlockType::kTpmBoundToPcr;
  }

  if (use_tpm && !with_user_auth) {
    return AuthBlockType::kTpmNotBoundToPcr;
  }

  return AuthBlockType::kLibScryptCompat;
}

AuthBlockType AuthBlockUtilityImpl::GetAuthBlockTypeForDerivation(
    const Credentials& credentials) const {
  std::unique_ptr<VaultKeyset> vk = keyset_management_->GetVaultKeyset(
      brillo::cryptohome::home::SanitizeUserName(credentials.username()),
      credentials.key_data().label());
  // If there is no keyset on the disk for the given user and label (or for the
  // empty label as a wildcard), key derivation type cannot be obtained.
  if (vk == nullptr) {
    LOG(ERROR)
        << "No vault keyset is found on disk for the given label. Cannot "
           "decide on the AuthBlock type without vault keyset metadata.";
    return AuthBlockType::kMaxValue;
  }

  int32_t vk_flags = vk->GetFlags();
  for (auto auth_block_flag : auth_block_flags) {
    if (MatchFlags(auth_block_flag, vk_flags)) {
      return auth_block_flag.auth_block_type;
    }
  }
  return AuthBlockType::kMaxValue;
}

std::unique_ptr<SyncAuthBlock> AuthBlockUtilityImpl::GetAuthBlockWithType(
    const AuthBlockType& auth_block_type) {
  switch (auth_block_type) {
    case AuthBlockType::kPinWeaver:
      return std::make_unique<PinWeaverAuthBlock>(
          crypto_->le_manager(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kChallengeCredential:
      return std::make_unique<ChallengeCredentialAuthBlock>();

    case AuthBlockType::kDoubleWrappedCompat:
      return std::make_unique<DoubleWrappedCompatAuthBlock>(
          crypto_->tpm(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kTpmEcc:
      return std::make_unique<TpmEccAuthBlock>(
          crypto_->tpm(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kTpmBoundToPcr:
      return std::make_unique<TpmBoundToPcrAuthBlock>(
          crypto_->tpm(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kTpmNotBoundToPcr:
      return std::make_unique<TpmNotBoundToPcrAuthBlock>(
          crypto_->tpm(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kLibScryptCompat:
      return std::make_unique<LibScryptCompatAuthBlock>();

    case AuthBlockType::kCryptohomeRecovery:
      LOG(ERROR)
          << "CryptohomeRecovery is not a supported AuthBlockType for now.";
      return nullptr;

    case AuthBlockType::kMaxValue:
      LOG(ERROR) << "Unsupported AuthBlockType.";

      return nullptr;
  }
  return nullptr;
}

bool AuthBlockUtilityImpl::GetAuthBlockStateFromVaultKeyset(
    const Credentials& credentials, AuthBlockState& auth_state) {
  std::unique_ptr<VaultKeyset> vault_keyset =
      keyset_management_->GetVaultKeyset(
          brillo::cryptohome::home::SanitizeUserName(credentials.username()),
          credentials.key_data().label());
  // If there is no keyset on the disk for the given user and label (or for the
  // empty label as a wildcard), AuthBlock state cannot be obtained.
  if (vault_keyset == nullptr) {
    LOG(ERROR)
        << "No vault keyset is found on disk for the given label. Cannot "
           "obtain AuthBlockState without vault keyset metadata.";
    return false;
  }

  int32_t vk_flags = vault_keyset->GetFlags();

  // First case, handle a group of users with keysets that were incorrectly
  // flagged as being both TPM and scrypt wrapped.
  if (MatchFlags(kDoubleWrappedCompatFlags, vk_flags)) {
    return vault_keyset->GetDoubleWrappedCompatState(&auth_state);
  }

  if (MatchFlags(kTpmEccFlags, vk_flags)) {
    return vault_keyset->GetTpmEccState(&auth_state);
  }
  if (MatchFlags(kTpmBoundToPcrFlags, vk_flags)) {
    return vault_keyset->GetTpmBoundToPcrState(&auth_state);
  }
  if (MatchFlags(kTpmNotBoundToPcrFlags, vk_flags)) {
    return vault_keyset->GetTpmNotBoundToPcrState(&auth_state);
  }
  if (MatchFlags(kPinWeaverFlags, vk_flags)) {
    return vault_keyset->GetPinWeaverState(&auth_state);
  }
  if (MatchFlags(kChallengeCredentialFlags, vk_flags)) {
    return vault_keyset->GetSignatureChallengeState(&auth_state);
  }
  if (MatchFlags(kLibScryptCompatFlags, vk_flags)) {
    return vault_keyset->GetLibScryptCompatState(&auth_state);
  }
  LOG(ERROR) << "Invalid auth block state type";
  return false;
}

void AuthBlockUtilityImpl::AssignAuthBlockStateToVaultKeyset(
    const AuthBlockState& auth_state, VaultKeyset& vault_keyset) {
  if (const auto* state =
          std::get_if<TpmNotBoundToPcrAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetTpmNotBoundToPcrState(*state);
  } else if (const auto* state =
                 std::get_if<TpmBoundToPcrAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetTpmBoundToPcrState(*state);
  } else if (const auto* state =
                 std::get_if<PinWeaverAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetPinWeaverState(*state);
  } else if (const auto* state = std::get_if<LibScryptCompatAuthBlockState>(
                 &auth_state.state)) {
    vault_keyset.SetLibScryptCompatState(*state);
  } else if (const auto* state = std::get_if<ChallengeCredentialAuthBlockState>(
                 &auth_state.state)) {
    vault_keyset.SetChallengeCredentialState(*state);
  } else if (const auto* state =
                 std::get_if<TpmEccAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetTpmEccState(*state);
  } else {
    LOG(ERROR) << "Invalid auth block state type";
    return;
  }
}

CryptoError AuthBlockUtilityImpl::CreateKeyBlobsWithAuthFactorType(
    AuthFactorType auth_factor_type,
    const AuthInput& auth_input,
    AuthBlockState& out_auth_block_state,
    KeyBlobs& out_key_blobs) {
  if (auth_factor_type != AuthFactorType::kPassword) {
    LOG(ERROR) << "Unsupported auth factor type";
    return CryptoError::CE_OTHER_CRYPTO;
  }
  // TODO(b/216804305): Stop hardcoding the auth block.
  TpmBoundToPcrAuthBlock auth_block(crypto_->tpm(),
                                    crypto_->cryptohome_keys_manager());
  return auth_block.Create(auth_input, &out_auth_block_state, &out_key_blobs);
}

CryptoError AuthBlockUtilityImpl::DeriveKeyBlobs(
    const AuthInput& auth_input,
    const AuthBlockState& auth_block_state,
    KeyBlobs& out_key_blobs) {
  std::unique_ptr<SyncAuthBlock> auth_block;
  if (const auto* state =
          std::get_if<TpmBoundToPcrAuthBlockState>(&auth_block_state.state)) {
    auth_block = std::make_unique<TpmBoundToPcrAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  }
  // TODO(b/216804305): Support other auth blocks.

  if (!auth_block) {
    LOG(ERROR) << "Unsupported auth block";
    return CryptoError::CE_OTHER_CRYPTO;
  }
  return auth_block->Derive(auth_input, auth_block_state, &out_key_blobs);
}

}  // namespace cryptohome
