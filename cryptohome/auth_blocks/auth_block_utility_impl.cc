// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/generic.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"

using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

AuthBlockUtilityImpl::AuthBlockUtilityImpl(
    KeysetManagement* keyset_management,
    Crypto* crypto,
    libstorage::Platform* platform,
    AsyncInitFeatures* features,
    AsyncInitPtr<ChallengeCredentialsHelper> challenge_credentials_helper,
    KeyChallengeServiceFactory* key_challenge_service_factory,
    AsyncInitPtr<BiometricsAuthBlockService> bio_service)
    : keyset_management_(keyset_management),
      crypto_(crypto),
      platform_(platform),
      features_(features),
      challenge_credentials_helper_(challenge_credentials_helper),
      key_challenge_service_factory_(key_challenge_service_factory),
      bio_service_(bio_service) {
  CHECK(keyset_management_);
  CHECK(crypto_);
  CHECK(platform_);
  CHECK(features_);
}

bool AuthBlockUtilityImpl::GetLockedToSingleUser() const {
  return platform_->FileExists(base::FilePath(kLockedToSingleUserFile));
}

void AuthBlockUtilityImpl::CreateKeyBlobsWithAuthBlock(
    AuthBlockType auth_block_type,
    const AuthInput& auth_input,
    const AuthFactorMetadata& auth_factor_metadata,
    AuthBlock::CreateCallback create_callback) {
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      GetAuthBlockWithType(auth_block_type, auth_input);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    std::move(create_callback)
        .Run(MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthBlockUtilNoAuthBlockInCreateKeyBlobsAsync))
                 .Wrap(std::move(auth_block).err_status()),
             nullptr, nullptr);
    return;
  }
  ReportCreateAuthBlock(auth_block_type);

  // This lambda functions to keep the auth_block reference valid until
  // the results are returned through create_callback.
  AuthBlock* auth_block_ptr = auth_block->get();
  auto managed_callback = base::BindOnce(
      [](std::unique_ptr<AuthBlock> owned_auth_block,
         AuthBlock::CreateCallback callback, CryptohomeStatus error,
         std::unique_ptr<KeyBlobs> key_blobs,
         std::unique_ptr<AuthBlockState> auth_block_state) {
        std::move(callback).Run(std::move(error), std::move(key_blobs),
                                std::move(auth_block_state));
      },
      std::move(auth_block.value()), std::move(create_callback));
  auth_block_ptr->Create(auth_input, auth_factor_metadata,
                         std::move(managed_callback));
}

void AuthBlockUtilityImpl::DeriveKeyBlobsWithAuthBlock(
    AuthBlockType auth_block_type,
    const AuthInput& auth_input,
    const AuthBlockState& auth_state,
    AuthBlock::DeriveCallback derive_callback) {
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      GetAuthBlockWithType(auth_block_type, auth_input);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    std::move(derive_callback)
        .Run(MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthBlockUtilNoAuthBlockInDeriveKeyBlobsAsync))
                 .Wrap(std::move(auth_block).err_status()),
             nullptr, std::nullopt);
    return;
  }
  ReportDeriveAuthBlock(auth_block_type);

  // This lambda functions to keep the auth_block reference valid until
  // the results are returned through derive_callback.
  AuthBlock* auth_block_ptr = auth_block->get();
  auto managed_callback = base::BindOnce(
      [](std::unique_ptr<AuthBlock> owned_auth_block,
         AuthBlock::DeriveCallback callback, CryptohomeStatus error,
         std::unique_ptr<KeyBlobs> key_blobs,
         std::optional<AuthBlock::SuggestedAction> suggested_action) {
        std::move(callback).Run(std::move(error), std::move(key_blobs),
                                suggested_action);
      },
      std::move(auth_block.value()), std::move(derive_callback));

  auth_block_ptr->Derive(auth_input, auth_state, std::move(managed_callback));
}

void AuthBlockUtilityImpl::SelectAuthFactorWithAuthBlock(
    AuthBlockType auth_block_type,
    const AuthInput& auth_input,
    std::vector<AuthFactor> auth_factors,
    AuthBlock::SelectFactorCallback select_callback) {
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      GetAuthBlockWithType(auth_block_type, auth_input);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    std::move(select_callback)
        .Run(MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthBlockUtilNoAuthBlockInSelectAuthFactor))
                 .Wrap(std::move(auth_block).err_status()),
             std::nullopt, std::nullopt);
    return;
  }
  ReportSelectFactorAuthBlock(auth_block_type);

  // This lambda functions to keep the auth_block reference valid until
  // the results are returned through select_callback.
  AuthBlock* auth_block_ptr = auth_block->get();
  auto managed_callback = base::BindOnce(
      [](std::unique_ptr<AuthBlock> owned_auth_block,
         AuthBlock::SelectFactorCallback callback, CryptohomeStatus error,
         std::optional<AuthInput> auth_input,
         std::optional<AuthFactor> auth_factor) {
        std::move(callback).Run(std::move(error), std::move(auth_input),
                                std::move(auth_factor));
      },
      std::move(auth_block.value()), std::move(select_callback));

  auth_block_ptr->SelectFactor(auth_input, std::move(auth_factors),
                               std::move(managed_callback));
}

CryptoStatusOr<AuthBlockType>
AuthBlockUtilityImpl::SelectAuthBlockTypeForCreation(
    base::span<const AuthBlockType> block_types) const {
  // Default status if there are no entries in the returned priority list.
  CryptoStatus status = MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilEmptyListInGetAuthBlockWithType),
      ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
      CryptoError::CE_OTHER_CRYPTO);
  for (AuthBlockType candidate_type : block_types) {
    status = IsAuthBlockSupported(candidate_type);
    if (status.ok()) {
      return candidate_type;
    }
  }
  // No suitable block was found. As we need to return only one error, take it
  // from the last attempted candidate (it's likely the most permissive one), if
  // any.
  return MakeStatus<CryptohomeCryptoError>(
             CRYPTOHOME_ERR_LOC(
                 kLocAuthBlockUtilNoSupportedInGetAuthBlockWithType),
             ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}))
      .Wrap(std::move(status));
}

CryptoStatus AuthBlockUtilityImpl::IsAuthBlockSupported(
    AuthBlockType auth_block_type) const {
  GenericAuthBlockFunctions generic(
      platform_, features_, challenge_credentials_helper_,
      key_challenge_service_factory_, bio_service_, crypto_);
  return generic.IsSupported(auth_block_type);
}

CryptoStatusOr<std::unique_ptr<AuthBlock>>
AuthBlockUtilityImpl::GetAuthBlockWithType(AuthBlockType auth_block_type,
                                           const AuthInput& auth_input) {
  if (auto status = IsAuthBlockSupported(auth_block_type); !status.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthBlockUtilNotSupportedInGetAuthBlockWithType))
        .Wrap(std::move(status));
  }
  GenericAuthBlockFunctions generic(
      platform_, features_, challenge_credentials_helper_,
      key_challenge_service_factory_, bio_service_, crypto_);
  auto auth_block = generic.GetAuthBlockWithType(auth_block_type, auth_input);
  if (!auth_block) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthBlockUtilUnknownUnsupportedInGetAuthBlockWithType),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  return auth_block;
}

std::optional<AuthBlockType> AuthBlockUtilityImpl::GetAuthBlockTypeFromState(
    const AuthBlockState& auth_block_state) const {
  GenericAuthBlockFunctions generic(
      platform_, features_, challenge_credentials_helper_,
      key_challenge_service_factory_, bio_service_, crypto_);
  return generic.GetAuthBlockTypeFromState(auth_block_state);
}

void AuthBlockUtilityImpl::PrepareAuthBlockForRemoval(
    const ObfuscatedUsername& obfuscated_username,
    const AuthBlockState& auth_block_state,
    CryptohomeStatusCallback callback) {
  std::optional<AuthBlockType> auth_block_type =
      GetAuthBlockTypeFromState(auth_block_state);
  if (!auth_block_type) {
    LOG(ERROR) << "Unsupported auth factor type.";
    std::move(callback).Run(MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthBlockUtilUnsupportedInPrepareAuthBlockForRemoval),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO));
    return;
  }

  // Should not create ChallengeCredential AuthBlock, no underlying
  // removal of the AuthBlock needed. Because of this, auth_input
  // can be an empty input.
  if (auth_block_type == AuthBlockType::kChallengeCredential) {
    std::move(callback).Run(OkStatus<CryptohomeError>());
    return;
  }

  AuthInput auth_input;
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      GetAuthBlockWithType(*auth_block_type, auth_input);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilNoAuthBlockInPrepareForRemoval))
            .Wrap(std::move(auth_block).err_status()));
    return;
  }

  // This lambda functions to keep the auth_block reference valid until
  // the results are returned through callback.
  AuthBlock* auth_block_ptr = auth_block->get();
  auto managed_callback = base::BindOnce(
      [](std::unique_ptr<AuthBlock> owned_auth_block,
         CryptohomeStatusCallback callback,
         CryptohomeStatus error) { std::move(callback).Run(std::move(error)); },
      std::move(auth_block.value()), std::move(callback));

  auth_block_ptr->PrepareForRemoval(obfuscated_username, auth_block_state,
                                    std::move(managed_callback));
}

}  // namespace cryptohome
