// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>
#include <cryptohome/scrypt_verifier.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_input_utils.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/vault_keyset.h"

using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

// Size of the values used serialization of UnguessableToken.
constexpr int kSizeOfSerializedValueInToken = sizeof(uint64_t);
// Number of uint64 used serialization of UnguessableToken.
constexpr int kNumberOfSerializedValuesInToken = 2;
// Offset where the high value is used in Serialized string.
constexpr int kHighTokenOffset = 0;
// Offset where the low value is used in Serialized string.
constexpr int kLowTokenOffset = kSizeOfSerializedValueInToken;
// AuthSession will time out if it is active after this time interval.
constexpr base::TimeDelta kAuthSessionTimeout = base::Minutes(5);

using user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;

namespace {

// Loads all configured auth factors for the given user from the disk. Malformed
// factors are logged and skipped.
std::map<std::string, std::unique_ptr<AuthFactor>> LoadAllAuthFactors(
    const std::string& obfuscated_username,
    AuthFactorManager* auth_factor_manager) {
  std::map<std::string, std::unique_ptr<AuthFactor>> label_to_auth_factor;
  for (const auto& [label, auth_factor_type] :
       auth_factor_manager->ListAuthFactors(obfuscated_username)) {
    std::unique_ptr<AuthFactor> auth_factor =
        auth_factor_manager->LoadAuthFactor(obfuscated_username,
                                            auth_factor_type, label);
    if (!auth_factor) {
      LOG(WARNING) << "Skipping malformed auth factor " << label;
      continue;
    }
    label_to_auth_factor.emplace(label, std::move(auth_factor));
  }
  return label_to_auth_factor;
}

}  // namespace

AuthSession::AuthSession(
    std::string username,
    unsigned int flags,
    base::OnceCallback<void(const base::UnguessableToken&)> on_timeout,
    Crypto* crypto,
    KeysetManagement* keyset_management,
    AuthBlockUtility* auth_block_utility,
    AuthFactorManager* auth_factor_manager,
    UserSecretStashStorage* user_secret_stash_storage)
    : username_(username),
      obfuscated_username_(SanitizeUserName(username_)),
      token_(base::UnguessableToken::Create()),
      serialized_token_(
          AuthSession::GetSerializedStringFromToken(token_).value_or("")),
      is_ephemeral_user_(flags & AUTH_SESSION_FLAGS_EPHEMERAL_USER),
      on_timeout_(std::move(on_timeout)),
      crypto_(crypto),
      keyset_management_(keyset_management),
      auth_block_utility_(auth_block_utility),
      auth_factor_manager_(auth_factor_manager),
      user_secret_stash_storage_(user_secret_stash_storage) {
  // Preconditions.
  DCHECK(!serialized_token_.empty());
  DCHECK(crypto_);
  DCHECK(keyset_management_);
  DCHECK(auth_block_utility_);
  DCHECK(auth_factor_manager_);
  DCHECK(user_secret_stash_storage_);

  LOG(INFO) << "AuthSession Flags: is_ephemeral_user_  " << is_ephemeral_user_;

  // TODO(hardikgoyal): make a factory function for AuthSession so the
  // constructor doesn't need to do work
  start_time_ = base::TimeTicks::Now();

  converter_ =
      std::make_unique<AuthFactorVaultKeysetConverter>(keyset_management);

  // Decide on USS vs VaultKeyset based on USS experiment file
  // and existence of VaultKeysets. If the experiment is enabled and there is no
  // VK on the disk follow USS path. If at least one VK exists, don't take USS
  // path even if the experiment is enabled.
  // TODO(b/223916443): We assume user has either VaultKeyset or USS until the
  // USS migration is started. If for some reason both exists on the disk,
  // unused one will be ignored.
  user_exists_ = keyset_management_->UserExists(obfuscated_username_);
  if (user_exists_) {
    keyset_management_->GetVaultKeysetLabelsAndData(obfuscated_username_,
                                                    &key_label_data_);
    user_has_configured_credential_ = !key_label_data_.empty();
  }
  if (IsUserSecretStashExperimentEnabled() &&
      !user_has_configured_credential_) {
    label_to_auth_factor_ =
        LoadAllAuthFactors(obfuscated_username_, auth_factor_manager_);
    user_has_configured_auth_factor_ = !label_to_auth_factor_.empty();
  } else {
    converter_->VaultKeysetsToAuthFactors(username_, label_to_auth_factor_);
  }

  // If the Auth Session is started for an ephemeral user, we always start in an
  // authenticated state.
  if (is_ephemeral_user_) {
    SetAuthSessionAsAuthenticated();
  }
}

void AuthSession::AuthSessionTimedOut() {
  status_ = AuthStatus::kAuthStatusTimedOut;
  // After this call back to |UserDataAuth|, |this| object will be deleted.
  std::move(on_timeout_).Run(token_);
}

void AuthSession::SetAuthSessionAsAuthenticated() {
  status_ = AuthStatus::kAuthStatusAuthenticated;
  timer_.Start(FROM_HERE, kAuthSessionTimeout,
               base::BindOnce(&AuthSession::AuthSessionTimedOut,
                              base::Unretained(this)));
}

user_data_auth::CryptohomeErrorCode AuthSession::ExtendTimer(
    const base::TimeDelta extension_duration) {
  // Check to make sure that the AuthSesion is still valid before we stop the
  // timer.
  if (status_ == AuthStatus::kAuthStatusTimedOut) {
    // AuthSession timed out before timer_.Stop() could be called.
    return user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN;
  }

  timer_.Stop();
  // Calculate time remaining and add kAuthSessionExtensionInMinutes to it.
  auto time_passed = base::TimeTicks::Now() - start_time_;
  auto extended_delay =
      (timer_.GetCurrentDelay() - time_passed) + extension_duration;
  timer_.Start(FROM_HERE, extended_delay,
               base::BindOnce(&AuthSession::AuthSessionTimedOut,
                              base::Unretained(this)));
  // Update start_time_.
  start_time_ = base::TimeTicks::Now();
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode AuthSession::OnUserCreated() {
  if (!is_ephemeral_user_) {
    // Creating file_system_keyset to the prepareVault call next.
    if (!file_system_keyset_.has_value()) {
      file_system_keyset_ = FileSystemKeyset::CreateRandom();
    }
    // Since this function is called for a new user, it is safe to put the
    // AuthSession in an authenticated state.
    SetAuthSessionAsAuthenticated();
    user_exists_ = true;
    if (IsUserSecretStashExperimentEnabled()) {
      // Check invariants.
      DCHECK(!user_secret_stash_);
      DCHECK(!user_secret_stash_main_key_.has_value());
      DCHECK(file_system_keyset_.has_value());
      // The USS experiment is on, hence create the USS for the newly created
      // non-ephemeral user. Keep the USS in memory: it will be persisted after
      // the first auth factor gets added.
      user_secret_stash_ =
          UserSecretStash::CreateRandom(file_system_keyset_.value());
      if (!user_secret_stash_) {
        LOG(ERROR) << "User secret stash creation failed";
        return user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_MOUNT_FATAL;
      }
      user_secret_stash_main_key_ = UserSecretStash::CreateRandomMainKey();
    }
  }

  return user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
}

void AuthSession::AddCredentials(
    const user_data_auth::AddCredentialsRequest& request,
    base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
        on_done) {
  MountError code;
  user_data_auth::AddCredentialsReply reply;
  CHECK(request.authorization().key().has_data());
  auto credentials = GetCredentials(request.authorization(), &code);
  if (!credentials) {
    reply.set_error(MountErrorToCryptohomeError(code));
    std::move(on_done).Run(reply);
    return;
  }

  if (user_has_configured_credential_) {
    // Can't add kiosk key for an existing user.
    if (credentials->key_data().type() == KeyData::KEY_TYPE_KIOSK) {
      LOG(WARNING) << "Add Credentials: tried adding kiosk auth for user";
      reply.set_error(
          MountErrorToCryptohomeError(MOUNT_ERROR_UNPRIVILEGED_KEY));
      std::move(on_done).Run(reply);
      return;
    }

    // At this point we have to have keyset since we have to be authed.
    if (!vault_keyset_) {
      LOG(ERROR)
          << "Add Credentials: tried adding credential before authenticating";
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      std::move(on_done).Run(reply);
      return;
    }

    CreateKeyBlobsToAddKeyset(*credentials.get(),
                              /*initial_keyset=*/false, std::move(on_done));
    return;
  }

  // If AuthSession is not configured as an ephemeral user, then we save the
  // key to the disk.
  if (is_ephemeral_user_) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    std::move(on_done).Run(reply);
    return;
  }

  DCHECK(!vault_keyset_);
  if (!file_system_keyset_.has_value()) {
    // Creating file_system_keyset to the prepareVault call next.
    // This is needed to support the old case where authentication happened
    // before creation of user and will be temporary as it is an intermediate
    // milestone.
    file_system_keyset_ = FileSystemKeyset::CreateRandom();
  }

  CreateKeyBlobsToAddKeyset(*credentials.get(),
                            /*initial_keyset=*/true, std::move(on_done));
  return;
}

void AuthSession::CreateKeyBlobsToAddKeyset(
    const Credentials& credentials,
    bool initial_keyset,
    base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
        on_done) {
  user_data_auth::AddCredentialsReply reply;
  AuthBlockType auth_block_type;
  bool is_le_credential =
      credentials.key_data().policy().low_entropy_credential();
  bool is_challenge_credential =
      credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE;

  // Generate KeyBlobs and AuthBlockState used for VaultKeyset encryption.
  auth_block_type = auth_block_utility_->GetAuthBlockTypeForCreation(
      is_le_credential, is_challenge_credential);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    reply.set_error(static_cast<user_data_auth::CryptohomeErrorCode>(
        CryptohomeErrorCode::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    std::move(on_done).Run(reply);
    return;
  }
  if (auth_block_type == AuthBlockType::kChallengeCredential) {
    LOG(ERROR) << "AddCredentials: ChallengeCredential not supported";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    std::move(on_done).Run(reply);
    return;
  }

  // Create and initialize fields for auth_input. |auth_state|
  // will be the input to AuthSession::AddVaultKeyset(), which calls
  // VaultKeyset::Encrypt().
  std::optional<brillo::SecureBlob> reset_secret;
  AuthBlock::CreateCallback create_callback;
  if (initial_keyset) {  // AddInitialKeyset operation
    if (auth_block_type == AuthBlockType::kPinWeaver) {
      reply.set_error(user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
      std::move(on_done).Run(reply);
      return;
    }
    // For the AddInitialKeyset operation, credential type is never le
    // credentials. So |reset_secret| is given to be std::nullopt.
    reset_secret = std::nullopt;
    create_callback = base::BindOnce(
        &AuthSession::AddVaultKeyset, base::Unretained(this),
        credentials.key_data(), credentials.challenge_credentials_keyset_info(),
        std::move(on_done));
  } else {  // AddKeyset operation
    // Create and initialize fields for auth_input.
    if (auth_block_type == AuthBlockType::kPinWeaver) {
      reset_secret = vault_keyset_->GetOrGenerateResetSecret();
    }
    create_callback = base::BindOnce(
        &AuthSession::AddVaultKeyset, base::Unretained(this),
        credentials.key_data(), std::nullopt, std::move(on_done));
  }
  // |reset_secret| is not processed in the AuthBlocks, the value is copied to
  // the |key_blobs| directly. |reset_secret| will be added to the |key_blobs|
  // in the VaultKeyset, if missing.
  AuthInput auth_input = {credentials.passkey(),
                          /*locked_to_single_user=*/std::nullopt,
                          obfuscated_username_, reset_secret};
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
  return;
}

void AuthSession::AddVaultKeyset(
    const KeyData& key_data,
    const std::optional<SerializedVaultKeyset_SignatureChallengeInfo>&
        challenge_credentials_keyset_info,
    base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
        on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  user_data_auth::AddCredentialsReply reply;
  // callback_error, key_blobs and auth_state are returned by
  // AuthBlock::CreateCallback.
  if (!callback_error.ok() || key_blobs == nullptr || auth_state == nullptr) {
    LOG(ERROR) << "KeyBlobs derivation failed before adding initial keyset.";
    reply.set_error(user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
    std::move(on_done).Run(reply);
    return;
  }

  if (user_has_configured_credential_) {  // AddKeyset
    user_data_auth::CryptohomeErrorCode error =
        static_cast<user_data_auth::CryptohomeErrorCode>(
            keyset_management_->AddKeysetWithKeyBlobs(
                obfuscated_username_, key_data, *vault_keyset_.get(),
                std::move(*key_blobs.get()), std::move(auth_state),
                true /*clobber*/));
    reply.set_error(error);
  } else {  // AddInitialKeyset
    if (!file_system_keyset_.has_value()) {
      LOG(ERROR) << "AddInitialKeyset: file_system_keyset is invalid.";
      reply.set_error(user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
      std::move(on_done).Run(reply);
      return;
    }
    if (!challenge_credentials_keyset_info.has_value()) {
      LOG(ERROR)
          << "AddInitialKeyset: challenge_credentials_keyset_info is invalid.";
      reply.set_error(user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
      std::move(on_done).Run(reply);
      return;
    }
    vault_keyset_ = keyset_management_->AddInitialKeysetWithKeyBlobs(
        obfuscated_username_, key_data,
        challenge_credentials_keyset_info.value(), file_system_keyset_.value(),
        std::move(*key_blobs.get()), std::move(auth_state));
    if (vault_keyset_ == nullptr) {
      reply.set_error(user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
      std::move(on_done).Run(reply);
      return;
    }
    // Flip the flag, so that our future invocations go through AddKeyset()
    // and not AddInitialKeyset().
    user_has_configured_credential_ = true;
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  }
  std::move(on_done).Run(reply);
  return;
}

void AuthSession::UpdateCredential(
    const user_data_auth::UpdateCredentialRequest& request,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done) {
  MountError code;
  user_data_auth::UpdateCredentialReply reply;
  CHECK(request.authorization().key().has_data());
  auto credentials = GetCredentials(request.authorization(), &code);
  if (!credentials) {
    reply.set_error(MountErrorToCryptohomeError(code));
    std::move(on_done).Run(reply);
    return;
  }

  // Can't update kiosk key for an existing user.
  if (credentials->key_data().type() == KeyData::KEY_TYPE_KIOSK) {
    LOG(ERROR) << "Add Credentials: tried adding kiosk auth for user";
    reply.set_error(MountErrorToCryptohomeError(MOUNT_ERROR_UNPRIVILEGED_KEY));
    std::move(on_done).Run(reply);
    return;
  }

  // To update a key, we need to ensure that the existing label and the new
  // label match.
  if (credentials->key_data().label() != request.old_credential_label()) {
    LOG(ERROR) << "AuthorizationRequest does not have a matching label";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  // At this point we have to have keyset since we have to be authed.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    reply.set_error(
        user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
    std::move(on_done).Run(reply);
    return;
  }

  CreateKeyBlobsToUpdateKeyset(*credentials.get(), std::move(on_done));
  return;
}

void AuthSession::CreateKeyBlobsToUpdateKeyset(
    const Credentials& credentials,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done) {
  user_data_auth::UpdateCredentialReply reply;

  bool is_le_credential =
      credentials.key_data().policy().low_entropy_credential();
  bool is_challenge_credential =
      credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE;

  AuthBlockType auth_block_type;
  auth_block_type = auth_block_utility_->GetAuthBlockTypeForCreation(
      is_le_credential, is_challenge_credential);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    reply.set_error(static_cast<user_data_auth::CryptohomeErrorCode>(
        CryptohomeErrorCode::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    std::move(on_done).Run(reply);
    return;
  }
  if (auth_block_type == AuthBlockType::kChallengeCredential) {
    LOG(ERROR) << "UpdateCredentials: ChallengeCredential not supported";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    std::move(on_done).Run(reply);
    return;
  }

  std::optional<brillo::SecureBlob> reset_secret;
  if (auth_block_type == AuthBlockType::kPinWeaver) {
    reset_secret = vault_keyset_->GetOrGenerateResetSecret();
  }

  // Create and initialize fields for auth_input.
  AuthInput auth_input = {credentials.passkey(),
                          /*locked_to_single_user=*/std::nullopt,
                          obfuscated_username_, reset_secret};

  AuthBlock::CreateCallback create_callback =
      base::BindOnce(&AuthSession::UpdateVaultKeyset, base::Unretained(this),
                     credentials.key_data(), std::move(on_done));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
  return;
}

void AuthSession::UpdateVaultKeyset(
    const KeyData& key_data,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  user_data_auth::UpdateCredentialReply reply;
  if (!callback_error.ok() || key_blobs == nullptr || auth_state == nullptr) {
    LOG(ERROR) << "KeyBlobs derivation failed before updating keyset.";
    CryptohomeStatus cryptohome_error = std::move(callback_error);
    reply.set_error(error::LegacyErrorCodeFromStack(cryptohome_error));
    std::move(on_done).Run(reply);
    return;
  }
  user_data_auth::CryptohomeErrorCode error_code =
      static_cast<user_data_auth::CryptohomeErrorCode>(
          keyset_management_->UpdateKeysetWithKeyBlobs(
              obfuscated_username_, key_data, *vault_keyset_.get(),
              std::move(*key_blobs.get()), std::move(auth_state)));
  reply.set_error(error_code);
  std::move(on_done).Run(reply);
  return;
}

user_data_auth::CryptohomeErrorCode AuthSession::Authenticate(
    const cryptohome::AuthorizationRequest& authorization_request) {
  MountError code = MOUNT_ERROR_NONE;

  auto credentials = GetCredentials(authorization_request, &code);
  if (!credentials) {
    return MountErrorToCryptohomeError(code);
  }
  if (authorization_request.key().data().type() != KeyData::KEY_TYPE_PASSWORD &&
      authorization_request.key().data().type() != KeyData::KEY_TYPE_KIOSK) {
    // AuthSession::Authenticate is only supported for two types of cases
    return user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED;
  }

  // Store key data in current auth_factor for future use.
  key_data_ = credentials->key_data();

  if (!is_ephemeral_user_) {
    // A persistent mount will always have a persistent key on disk. Here
    // keyset_management tries to fetch that persistent credential.
    MountError error = MOUNT_ERROR_NONE;
    // TODO(dlunev): fix conditional error when we switch to StatusOr.
    vault_keyset_ = keyset_management_->GetValidKeyset(*credentials, &error);
    if (!vault_keyset_) {
      return MountErrorToCryptohomeError(
          // MountError from GetValidKeyset is MOUNT_ERROR_NONE only if
          // CryptoError::CE_TPM_FATAL is received from VaultKeyset::Decrypt().
          error == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : error);
    }
    file_system_keyset_ = FileSystemKeyset(*vault_keyset_);
    // Add the missing fields in the keyset, if any, and resave.
    keyset_management_->ReSaveKeysetIfNeeded(*credentials, vault_keyset_.get());
  }

  // Set the credential verifier for this credential.
  credential_verifier_.reset(new ScryptVerifier());
  credential_verifier_->Set(credentials->passkey());

  SetAuthSessionAsAuthenticated();
  return MountErrorToCryptohomeError(code);
}

const FileSystemKeyset& AuthSession::file_system_keyset() const {
  DCHECK(file_system_keyset_.has_value());
  return file_system_keyset_.value();
}

bool AuthSession::AuthenticateAuthFactor(
    const user_data_auth::AuthenticateAuthFactorRequest& request,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done) {
  user_data_auth::AuthenticateAuthFactorReply reply;

  // Check the factor exists either with USS or VaultKeyset.
  auto label_to_auth_factor_iter =
      label_to_auth_factor_.find(request.auth_factor_label());
  if (label_to_auth_factor_iter == label_to_auth_factor_.end()) {
    LOG(ERROR) << "Authentication key not found: "
               << request.auth_factor_label();
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    std::move(on_done).Run(reply);
    return false;
  }

  // Fill up the auth input.
  std::optional<AuthInput> auth_input =
      FromProto(request.auth_input(), obfuscated_username_,
                auth_block_utility_->GetLockedToSingleUser());
  if (!auth_input.has_value()) {
    LOG(ERROR) << "Failed to parse auth input for authenticating auth factor";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return false;
  }

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  // If the USS experiment is enabled and there is no VaultKeyset for the
  // user continue authentication with USS.
  if (IsUserSecretStashExperimentEnabled() &&
      user_has_configured_auth_factor_) {
    AuthFactor auth_factor = *label_to_auth_factor_iter->second;

    error = AuthenticateViaUserSecretStash(request.auth_factor_label(),
                                           auth_input.value(), auth_factor);
    if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
      LOG(ERROR) << "Failed to authenticate auth session via factor "
                 << request.auth_factor_label();
      reply.set_error(error);
      std::move(on_done).Run(reply);
      return false;
    }

    // Reset LE Credential counter if the current AutFactor is not an
    // LECredential.
    ResetLECredentials();

    // Flip the status on the successful authentication.
    status_ = AuthStatus::kAuthStatusAuthenticated;
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    std::move(on_done).Run(reply);
    return true;
  }

  // If USS experiment is not enabled or user has VaultKeysets continue
  // authentication with Vaultkeyset. Status is flipped on the successful
  // authentication.
  error = converter_->PopulateKeyDataForVK(
      username_, request.auth_factor_label(), key_data_);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to authenticate auth session via vk-factor "
               << request.auth_factor_label();
    reply.set_error(error);
    std::move(on_done).Run(reply);
    return false;
  }
  return AuthenticateViaVaultKeyset(auth_input.value(), std::move(on_done));
}

bool AuthSession::AuthenticateViaVaultKeyset(
    const AuthInput& auth_input,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done) {
  user_data_auth::AuthenticateAuthFactorReply reply;

  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeForDerivation(key_data_.label(),
                                                         obfuscated_username_);

  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR) << "Error in obtaining AuthBlock type for key derivation.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    std::move(on_done).Run(reply);
    return false;
  }

  AuthBlockState auth_state;
  if (!auth_block_utility_->GetAuthBlockStateFromVaultKeyset(
          key_data_.label(), obfuscated_username_, auth_state)) {
    LOG(ERROR) << "Error in obtaining AuthBlock state for key derivation.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    std::move(on_done).Run(reply);
    return false;
  }

  // Authenticate and derive KeyBlobs.
  return auth_block_utility_->DeriveKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, auth_state,
      base::BindOnce(&AuthSession::LoadVaultKeysetAndFsKeys,
                     base::Unretained(this), std::move(on_done)));
}

void AuthSession::LoadVaultKeysetAndFsKeys(
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done,
    CryptoStatus error,
    std::unique_ptr<KeyBlobs> key_blobs) {
  user_data_auth::AuthenticateAuthFactorReply reply;

  // The error should be evaluated the same way as it is done in
  // AuthSession::Authenticate(), which directly returns the GetValidKeyset()
  // error. So we are doing a similar error handling here as in
  // KeysetManagement::GetValidKeyset() to preserve the behavior. Empty label
  // case is dropped in here since it is not a valid case anymore.
  if (!key_blobs) {
    LOG(ERROR) << "Failed to load VaultKeyset since key blobs has not been "
                  "derived.";
    CryptoError crypto_error = error->local_crypto_error();
    if (error.ok()) {
      // Maps to the default value of MountError which is
      // MOUNT_ERROR_KEY_FAILURE
      crypto_error = CryptoError::CE_OTHER_CRYPTO;
    }
    reply.set_error(CryptoErrorToCryptohomeError(crypto_error));
    std::move(on_done).Run(reply);
    return;
  }

  MountError mount_error;
  vault_keyset_ = keyset_management_->GetValidKeysetWithKeyBlobs(
      obfuscated_username_, std::move(*key_blobs.get()), key_data_.label(),
      &mount_error);
  if (!vault_keyset_) {
    user_data_auth::CryptohomeErrorCode crypto_error =
        MountErrorToCryptohomeError(
            mount_error == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : mount_error);
    LOG(ERROR) << "Failed to load VaultKeyset and file system keyset.";
    reply.set_error(crypto_error);
    std::move(on_done).Run(reply);
  }

  // Authentication is successfully completed. Reset LE Credential counter if
  // the current AutFactor is not an LECredential.
  if (!vault_keyset_->IsLECredential()) {
    keyset_management_->ResetLECredentials(std::nullopt, *vault_keyset_,
                                           obfuscated_username_);
  }

  file_system_keyset_ = FileSystemKeyset(*vault_keyset_);

  // Flip the status on the successful authentication.
  SetAuthSessionAsAuthenticated();
  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::move(on_done).Run(reply);
}

std::unique_ptr<CredentialVerifier> AuthSession::TakeCredentialVerifier() {
  return std::move(credential_verifier_);
}

// static
std::optional<std::string> AuthSession::GetSerializedStringFromToken(
    const base::UnguessableToken& token) {
  if (token == base::UnguessableToken::Null()) {
    LOG(ERROR) << "Invalid UnguessableToken given";
    return std::nullopt;
  }
  std::string serialized_token;
  serialized_token.resize(kSizeOfSerializedValueInToken *
                          kNumberOfSerializedValuesInToken);
  uint64_t high = token.GetHighForSerialization();
  uint64_t low = token.GetLowForSerialization();
  memcpy(&serialized_token[kHighTokenOffset], &high, sizeof(high));
  memcpy(&serialized_token[kLowTokenOffset], &low, sizeof(low));
  return serialized_token;
}

// static
std::optional<base::UnguessableToken> AuthSession::GetTokenFromSerializedString(
    const std::string& serialized_token) {
  if (serialized_token.size() !=
      kSizeOfSerializedValueInToken * kNumberOfSerializedValuesInToken) {
    LOG(ERROR) << "Incorrect serialized string size";
    return std::nullopt;
  }
  uint64_t high, low;
  memcpy(&high, &serialized_token[kHighTokenOffset], sizeof(high));
  memcpy(&low, &serialized_token[kLowTokenOffset], sizeof(low));
  return base::UnguessableToken::Deserialize(high, low);
}

std::unique_ptr<Credentials> AuthSession::GetCredentials(
    const cryptohome::AuthorizationRequest& authorization_request,
    MountError* error) {
  auto credentials = std::make_unique<Credentials>(
      username_, brillo::SecureBlob(authorization_request.key().secret()));
  credentials->set_key_data(authorization_request.key().data());

  if (authorization_request.key().data().type() == KeyData::KEY_TYPE_KIOSK) {
    if (!credentials->passkey().empty()) {
      LOG(ERROR) << "Non-empty passkey in kiosk key.";
      *error = MountError::MOUNT_ERROR_INVALID_ARGS;
      return nullptr;
    }
    brillo::SecureBlob public_mount_passkey =
        keyset_management_->GetPublicMountPassKey(username_);
    if (public_mount_passkey.empty()) {
      LOG(ERROR) << "Could not get public mount passkey.";
      *error = MountError::MOUNT_ERROR_KEY_FAILURE;
      return nullptr;
    }
    credentials->set_passkey(public_mount_passkey);
  }

  return credentials;
}

user_data_auth::CryptohomeErrorCode AuthSession::AddAuthFactor(
    const user_data_auth::AddAuthFactorRequest& request) {
  // Preconditions:
  DCHECK_EQ(request.auth_session_id(), serialized_token_);

  // TODO(b/216804305): Verify the auth session is authenticated, after
  // `OnUserCreated()` is changed to mark the session authenticated.
  // At this point AuthSession should be authenticated as it needs
  // FileSystemKeys to wrap the new credentials.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    return user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION;
  }

  AuthFactorMetadata auth_factor_metadata;
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  if (!GetAuthFactorMetadata(request.auth_factor(), auth_factor_metadata,
                             auth_factor_type, auth_factor_label)) {
    LOG(ERROR) << "Failed to parse new auth factor parameters";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::optional<AuthInput> auth_input =
      FromProto(request.auth_input(), obfuscated_username_,
                auth_block_utility_->GetLockedToSingleUser());
  if (!auth_input.has_value()) {
    LOG(ERROR) << "Failed to parse auth input for new auth factor";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (user_secret_stash_) {
    // The user has a UserSecretStash (either because it's a new user and the
    // experiment is on or it's an existing user who went through this flow), so
    // proceed with wrapping the USS via the new factor and persisting both.

    // 0. Add reset secret to AuthInput required from user secret stash.
    auth_input->reset_secret = user_secret_stash_->GetResetSecret();

    return AddAuthFactorViaUserSecretStash(auth_factor_type, auth_factor_label,
                                           auth_factor_metadata,
                                           auth_input.value());
  }

  // TODO(b/3319388): Implement for the vault keyset case.
  return user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED;
}

user_data_auth::CryptohomeErrorCode
AuthSession::AddAuthFactorViaUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input) {
  // Preconditions.
  DCHECK(user_secret_stash_);
  DCHECK(user_secret_stash_main_key_.has_value());

  // 1. Create a new auth factor in-memory, by executing auth block's Create().
  KeyBlobs key_blobs;
  std::unique_ptr<AuthFactor> auth_factor = AuthFactor::CreateNew(
      auth_factor_type, auth_factor_label, auth_factor_metadata, auth_input,
      auth_block_utility_, key_blobs);
  if (!auth_factor) {
    LOG(ERROR) << "Failed to create new auth factor";
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  // 2. Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs.DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR) << "Failed to derive credential secret for created auth factor";
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  // 3. Add the new factor into the USS in-memory.
  // This wraps the USS Main Key with the credential secret. The wrapping_id
  // field is defined equal to the factor's label.
  if (!user_secret_stash_->AddWrappedMainKey(
          user_secret_stash_main_key_.value(),
          /*wrapping_id=*/auth_factor_label, uss_credential_secret.value())) {
    LOG(ERROR) << "Failed to add created auth factor into user secret stash";
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  // 4. Encrypt the updated USS.
  std::optional<brillo::SecureBlob> encrypted_uss_container =
      user_secret_stash_->GetEncryptedContainer(
          user_secret_stash_main_key_.value());
  if (!encrypted_uss_container.has_value()) {
    LOG(ERROR)
        << "Failed to encrypt user secret stash after auth factor creation";
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  // 5. Persist the factor.
  // It's important to do this after all steps ##1-4, so that we only start
  // writing files after all validity checks (like the label duplication check).
  if (!auth_factor_manager_->SaveAuthFactor(obfuscated_username_,
                                            *auth_factor)) {
    LOG(ERROR) << "Failed to persist created auth factor";
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  // 6. Persist the USS.
  // It's important to do this after #5, to minimize the chance of ending in an
  // inconsistent state on the disk: a created/updated USS and a missing auth
  // factor (note that we're using file system syncs to have best-effort
  // ordering guarantee).
  if (!user_secret_stash_storage_->Persist(encrypted_uss_container.value(),
                                           obfuscated_username_)) {
    LOG(ERROR)
        << "Failed to persist user secret stash after auth factor creation";
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  label_to_auth_factor_.emplace(auth_factor_label, std::move(auth_factor));
  user_has_configured_auth_factor_ = true;

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode AuthSession::AuthenticateViaUserSecretStash(
    const std::string& auth_factor_label,
    const AuthInput auth_input,
    AuthFactor& auth_factor) {
  // TODO(b/223207622): This step is the same for both USS and
  // VaultKeyset other than how the AuthBlock state is obtained. Make the
  // derivation for USS asynchronous and merge these two.
  KeyBlobs key_blobs;
  user_data_auth::CryptohomeErrorCode error =
      auth_factor.Authenticate(auth_input, auth_block_utility_, key_blobs);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to authenticate auth session via factor "
               << auth_factor_label;
    return error;
  }

  // Use USS to finish the authentication.
  error = LoadUSSMainKeyAndFsKeyset(auth_factor_label, key_blobs);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to authenticate auth session via factor "
               << auth_factor_label;
    return error;
  }
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode AuthSession::LoadUSSMainKeyAndFsKeyset(
    const std::string& auth_factor_label, const KeyBlobs& key_blobs) {
  // 1. Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs.DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR)
        << "Failed to derive credential secret for authenticating auth factor";
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  // 2. Load the USS container with the encrypted payload.
  std::optional<brillo::SecureBlob> encrypted_uss =
      user_secret_stash_storage_->LoadPersisted(obfuscated_username_);
  if (!encrypted_uss.has_value()) {
    LOG(ERROR) << "Failed to load the user secret stash";
    return user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  // 3. Decrypt the USS payload.
  // This unwraps the USS Main Key with the credential secret, and decrypts the
  // USS payload using the USS Main Key. The wrapping_id field is defined equal
  // to the factor's label.
  brillo::SecureBlob decrypted_main_key;
  user_secret_stash_ = UserSecretStash::FromEncryptedContainerWithWrappingKey(
      encrypted_uss.value(), /*wrapping_id=*/auth_factor_label,
      /*wrapping_key=*/uss_credential_secret.value(), &decrypted_main_key);
  if (!user_secret_stash_) {
    LOG(ERROR) << "Failed to decrypt the user secret stash";
    return user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }
  user_secret_stash_main_key_ = decrypted_main_key;

  // 4. Populate data fields from the USS.
  file_system_keyset_ = user_secret_stash_->GetFileSystemKeyset();

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

void AuthSession::ResetLECredentials() {
  CryptoError error;
  // Loop through all the AuthFactors.
  for (auto& iter : label_to_auth_factor_) {
    // Look for only pinweaver backed AuthFactors.
    auto* state = std::get_if<::cryptohome::PinWeaverAuthBlockState>(
        &(iter.second->auth_block_state().state));
    if (!state) {
      continue;
    }
    // Ensure that the AuthFactor has le_label.
    if (!state->le_label.has_value()) {
      LOG(WARNING) << "PinWeaver AuthBlock State does not have le_label";
      continue;
    }
    // TODO(b/226957785): Have a reset secret per credential in USS.
    // Reset the attempt count for the pinweaver leaf.
    // If there is an error, warn for the error in log.
    if (!crypto_->ResetLeCredentialEx(state->le_label.value(),
                                      user_secret_stash_->GetResetSecret(),
                                      error)) {
      LOG(WARNING) << "Failed to reset an LE credential: " << error;
    }
  }
}

}  // namespace cryptohome
