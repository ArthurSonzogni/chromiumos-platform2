// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/decrypted.h"

#include <map>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_payload.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/encrypted.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;

// Construct a FileSystemKeyset from a given USS payload. Returns an error if
// any of the components of the keyset appear to be missing.
CryptohomeStatusOr<FileSystemKeyset> GetFileSystemKeysetFromPayload(
    const UserSecretStashPayload& payload) {
  if (payload.fek.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFEKInGetFSKeyFromPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (payload.fnek.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFNEKInGetFSKeyFromPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (payload.fek_salt.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK salt";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFEKSaltInGetFSKeyFromPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (payload.fnek_salt.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK salt";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFNEKSaltInGetFSKeyFromPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (payload.fek_sig.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK signature";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFEKSigInGetFSKeyFromPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (payload.fnek_sig.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK signature";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFNEKSigInGetFSKeyFromPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (payload.chaps_key.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no Chaps key";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoChapsKeyInGetFSKeyFromPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  FileSystemKey file_system_key = {
      .fek = payload.fek,
      .fnek = payload.fnek,
      .fek_salt = payload.fek_salt,
      .fnek_salt = payload.fnek_salt,
  };
  FileSystemKeyReference file_system_key_reference = {
      .fek_sig = payload.fek_sig,
      .fnek_sig = payload.fnek_sig,
  };
  return FileSystemKeyset(std::move(file_system_key),
                          std::move(file_system_key_reference),
                          payload.chaps_key);
}

}  // namespace

CryptohomeStatusOr<DecryptedUss> DecryptedUss::FromBlobUsingMainKey(
    const brillo::Blob& flatbuffer, const brillo::SecureBlob& main_key) {
  ASSIGN_OR_RETURN(EncryptedUss encrypted, EncryptedUss::FromBlob(flatbuffer));
  return FromEncryptedUss(std::move(encrypted), main_key);
}

CryptohomeStatusOr<std::tuple<DecryptedUss, brillo::SecureBlob>>
DecryptedUss::FromBlobUsingWrappedKey(const brillo::Blob& flatbuffer,
                                      const std::string& wrapping_id,
                                      const brillo::SecureBlob& wrapping_key) {
  ASSIGN_OR_RETURN(EncryptedUss encrypted, EncryptedUss::FromBlob(flatbuffer));
  ASSIGN_OR_RETURN(brillo::SecureBlob main_key,
                   encrypted.UnwrapMainKey(wrapping_id, wrapping_key));
  ASSIGN_OR_RETURN(DecryptedUss decrypted,
                   FromEncryptedUss(std::move(encrypted), main_key));
  return std::tuple(std::move(decrypted), std::move(main_key));
}

void DecryptedUss::ExtractContents(
    FileSystemKeyset& file_system_keyset,
    std::map<std::string, EncryptedUss::WrappedKeyBlock>& wrapped_key_blocks,
    std::string& created_on_os_version,
    std::map<std::string, brillo::SecureBlob>& reset_secrets,
    std::map<AuthFactorType, brillo::SecureBlob>& rate_limiter_reset_secrets,
    UserMetadata& user_metadata) && {
  file_system_keyset = std::move(file_system_keyset_);
  wrapped_key_blocks = encrypted_.container({}).wrapped_key_blocks;
  created_on_os_version = encrypted_.created_on_os_version();
  reset_secrets = std::move(reset_secrets_);
  rate_limiter_reset_secrets = std::move(rate_limiter_reset_secrets_);
  user_metadata = encrypted_.user_metadata();
}

CryptohomeStatusOr<DecryptedUss> DecryptedUss::FromEncryptedUss(
    EncryptedUss encrypted, const brillo::SecureBlob& main_key) {
  // Use the main key to decrypt the USS payload.
  ASSIGN_OR_RETURN(brillo::SecureBlob serialized_payload,
                   encrypted.DecryptPayload(main_key));

  // Deserialize the decrypted payload into a flatbuffer.
  std::optional<UserSecretStashPayload> payload =
      UserSecretStashPayload::Deserialize(serialized_payload);
  if (!payload) {
    LOG(ERROR) << "Failed to deserialize UserSecretStashPayload";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSDeserializeFailedInFromEncPayload),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  // Extract the filesystem keyset from the payload.
  ASSIGN_OR_RETURN(FileSystemKeyset file_system_keyset,
                   GetFileSystemKeysetFromPayload(*payload),
                   _.LogError() << "UserSecretStashPayload has invalid file "
                                   "system keyset information");

  // Extract the reset secrets from the payload.
  std::map<std::string, brillo::SecureBlob> reset_secrets;
  for (const ResetSecretMapping& item : payload->reset_secrets) {
    auto [iter, was_inserted] = reset_secrets.emplace(
        std::move(item.auth_factor_label), std::move(item.reset_secret));
    if (!was_inserted) {
      LOG(ERROR) << "UserSecretStashPayload contains multiple reset secrets "
                    "for label: "
                 << iter->first;
    }
  }

  // Extract the rate limiter secrets from the payload.
  std::map<AuthFactorType, brillo::SecureBlob> rate_limiter_reset_secrets;
  for (const TypeToResetSecretMapping& item :
       payload->rate_limiter_reset_secrets) {
    if (!item.auth_factor_type) {
      LOG(ERROR)
          << "UserSecretStashPayload contains reset secret with missing type.";
      continue;
    }
    if (*item.auth_factor_type >=
        static_cast<unsigned int>(AuthFactorType::kUnspecified)) {
      LOG(ERROR)
          << "UserSecretStashPayload contains reset secret for invalid type: "
          << *item.auth_factor_type << ".";
      continue;
    }
    AuthFactorType auth_factor_type =
        static_cast<AuthFactorType>(*item.auth_factor_type);
    auto [iter, was_inserted] = rate_limiter_reset_secrets.emplace(
        auth_factor_type, std::move(item.reset_secret));
    if (!was_inserted) {
      LOG(ERROR) << "UserSecretStashPayload contains multiple reset secrets "
                    "for type: "
                 << AuthFactorTypeToString(iter->first) << ".";
    }
  }

  return DecryptedUss(std::move(encrypted), std::move(file_system_keyset),
                      std::move(reset_secrets),
                      std::move(rate_limiter_reset_secrets));
}

DecryptedUss::DecryptedUss(
    EncryptedUss encrypted,
    FileSystemKeyset file_system_keyset,
    std::map<std::string, brillo::SecureBlob> reset_secrets,
    std::map<AuthFactorType, brillo::SecureBlob> rate_limiter_reset_secrets)
    : encrypted_(std::move(encrypted)),
      file_system_keyset_(std::move(file_system_keyset)),
      reset_secrets_(std::move(reset_secrets)),
      rate_limiter_reset_secrets_(std::move(rate_limiter_reset_secrets)) {}

}  // namespace cryptohome
