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
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_input_utils.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/vault_keyset.h"

using brillo::cryptohome::home::SanitizeUserName;

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
  timer_.Start(FROM_HERE, kAuthSessionTimeout,
               base::BindOnce(&AuthSession::AuthSessionTimedOut,
                              base::Unretained(this)));

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
  } else {
    converter_->VaultKeysetsToAuthFactors(username_, label_to_auth_factor_);
  }
}

void AuthSession::AuthSessionTimedOut() {
  status_ = AuthStatus::kAuthStatusTimedOut;
  // After this call back to |UserDataAuth|, |this| object will be deleted.
  std::move(on_timeout_).Run(token_);
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
    status_ = AuthStatus::kAuthStatusAuthenticated;
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

user_data_auth::CryptohomeErrorCode AuthSession::AddCredentials(
    const user_data_auth::AddCredentialsRequest& request) {
  MountError code;
  CHECK(request.authorization().key().has_data());
  auto credentials = GetCredentials(request.authorization(), &code);
  if (!credentials) {
    return MountErrorToCryptohomeError(code);
  }

  if (user_has_configured_credential_) {
    // Can't add kiosk key for an existing user.
    if (credentials->key_data().type() == KeyData::KEY_TYPE_KIOSK) {
      LOG(WARNING) << "Add Credentials: tried adding kiosk auth for user";
      return MountErrorToCryptohomeError(MOUNT_ERROR_UNPRIVILEGED_KEY);
    }

    // At this point we have to have keyset since we have to be authed.
    if (!vault_keyset_) {
      LOG(ERROR)
          << "Add Credentials: tried adding credential before authenticating";
      return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
    }

    return static_cast<user_data_auth::CryptohomeErrorCode>(
        keyset_management_->AddKeyset(*credentials, *vault_keyset_,
                                      true /*clobber*/));
  }

  // If AuthSession is not configured as an ephemeral user, then we save the
  // key to the disk.
  if (is_ephemeral_user_) {
    return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  }

  DCHECK(!vault_keyset_);
  if (!file_system_keyset_.has_value()) {
    // Creating file_system_keyset to the prepareVault call next.
    // This is needed to support the old case where authentication happened
    // before creation of user and will be temporary as it is an intermediate
    // milestone.
    file_system_keyset_ = FileSystemKeyset::CreateRandom();
  }
  // An assumption here is that keyset management saves the user keys on disk.
  vault_keyset_ = keyset_management_->AddInitialKeyset(
      *credentials, file_system_keyset_.value());
  if (!vault_keyset_) {
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  // Flip the flag, so that our future invocations go through AddKeyset() and
  // not AddInitialKeyset().
  user_has_configured_credential_ = true;
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode AuthSession::UpdateCredential(
    const user_data_auth::UpdateCredentialRequest& request) {
  MountError code;
  CHECK(request.authorization().key().has_data());
  auto credentials = GetCredentials(request.authorization(), &code);
  if (!credentials) {
    return MountErrorToCryptohomeError(code);
  }

  // Can't update kiosk key for an existing user.
  if (credentials->key_data().type() == KeyData::KEY_TYPE_KIOSK) {
    LOG(ERROR) << "Add Credentials: tried adding kiosk auth for user";
    return MountErrorToCryptohomeError(MOUNT_ERROR_UNPRIVILEGED_KEY);
  }

  // To update a key, we need to ensure that the existing label and the new
  // label match.
  if (credentials->key_data().label() != request.old_credential_label()) {
    LOG(ERROR) << "AuthorizationRequest does not have a matching label";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // At this point we have to have keyset since we have to be authed.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    return user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION;
  }

  return static_cast<user_data_auth::CryptohomeErrorCode>(
      keyset_management_->UpdateKeyset(*credentials, *vault_keyset_));
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
          error == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : error);
    }
    file_system_keyset_ = FileSystemKeyset(*vault_keyset_);
    // Add the missing fields in the keyset, if any, and resave.
    keyset_management_->ReSaveKeysetIfNeeded(*credentials, vault_keyset_.get());
  }

  // Set the credential verifier for this credential.
  credential_verifier_.reset(new ScryptVerifier());
  credential_verifier_->Set(credentials->passkey());

  status_ = AuthStatus::kAuthStatusAuthenticated;

  return MountErrorToCryptohomeError(code);
}

const FileSystemKeyset& AuthSession::file_system_keyset() const {
  DCHECK(file_system_keyset_.has_value());
  return file_system_keyset_.value();
}

user_data_auth::CryptohomeErrorCode AuthSession::AuthenticateAuthFactor(
    const user_data_auth::AuthenticateAuthFactorRequest& request) {
  // 1. Check the factor exists.
  auto iter = label_to_auth_factor_.find(request.auth_factor_label());
  if (iter == label_to_auth_factor_.end()) {
    LOG(ERROR) << "Authentication key not found: "
               << request.auth_factor_label();
    return user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }
  AuthFactor& auth_factor = *iter->second;

  // 2. Convert the auth input.
  std::optional<AuthInput> auth_input =
      FromProto(request.auth_input(), obfuscated_username_,
                auth_block_utility_->GetLockedToSingleUser());
  if (!auth_input.has_value()) {
    LOG(ERROR) << "Failed to parse auth input for authenticating auth factor";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // 3. Perform the auth block key blobs derivation.
  KeyBlobs key_blobs;
  user_data_auth::CryptohomeErrorCode error = auth_factor.Authenticate(
      auth_input.value(), auth_block_utility_, key_blobs);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to authenticate auth session via factor "
               << request.auth_factor_label();
    return error;
  }

  // 4. If the auth factor is tied with USS, use it to finish the
  // authentication.
  error =
      AuthenticateViaUserSecretStash(request.auth_factor_label(), key_blobs);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to authenticate auth session via factor "
               << request.auth_factor_label();
    return error;
  }

  // 5. Flip the status on the successful authentication.
  status_ = AuthStatus::kAuthStatusAuthenticated;
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
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

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode AuthSession::AuthenticateViaUserSecretStash(
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

}  // namespace cryptohome
