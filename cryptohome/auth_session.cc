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
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_input_utils.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/vault_keyset.h"

using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::CreateSecureRandomBlob;
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
    CryptohomeStatusOr<std::unique_ptr<AuthFactor>> auth_factor =
        auth_factor_manager->LoadAuthFactor(obfuscated_username,
                                            auth_factor_type, label);
    if (!auth_factor.ok()) {
      LOG(WARNING) << "Skipping malformed auth factor " << label;
      continue;
    }
    label_to_auth_factor.emplace(label, std::move(auth_factor).value());
  }
  return label_to_auth_factor;
}

cryptorecovery::RequestMetadata RequestMetadataFromProto(
    const user_data_auth::GetRecoveryRequestRequest& request) {
  cryptorecovery::RequestMetadata result;

  result.requestor_user_id = request.requestor_user_id();
  switch (request.requestor_user_id_type()) {
    case user_data_auth::GetRecoveryRequestRequest_UserType_GAIA_ID:
      result.requestor_user_id_type = cryptorecovery::UserType::kGaiaId;
      break;
    case user_data_auth::GetRecoveryRequestRequest_UserType_UNKNOWN:
    default:
      result.requestor_user_id_type = cryptorecovery::UserType::kUnknown;
      break;
  }

  result.auth_claim = cryptorecovery::AuthClaim{
      .gaia_access_token = request.gaia_access_token(),
      .gaia_reauth_proof_token = request.gaia_reauth_proof_token(),
  };

  return result;
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

  // Decide on USS vs VaultKeyset based on what is on the disk for the user.
  // If at least one VK exists, don't take USS path even if the experiment
  // is enabled.
  // TODO(b/223916443): We assume user has either VaultKeyset or USS until the
  // USS migration is started. If for some reason both exists on the disk,
  // unused one will be ignored.
  user_exists_ = keyset_management_->UserExists(obfuscated_username_);
  if (user_exists_) {
    keyset_management_->GetVaultKeysetLabelsAndData(obfuscated_username_,
                                                    &key_label_data_);
    user_has_configured_credential_ = !key_label_data_.empty();
  }
  if (!user_has_configured_credential_) {
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

CryptohomeStatus AuthSession::ExtendTimer(
    const base::TimeDelta extension_duration) {
  // Check to make sure that the AuthSesion is still valid before we stop the
  // timer.
  if (status_ == AuthStatus::kAuthStatusTimedOut) {
    // AuthSession timed out before timer_.Stop() could be called.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionTimedOutInExtend),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
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
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus AuthSession::OnUserCreated() {
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
      // TODO(b/229834676): Migrate UserSecretStash and wrap the resulting
      // error.
      if (!user_secret_stash_) {
        LOG(ERROR) << "User secret stash creation failed";
        return MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateUSSFailedInOnUserCreated),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
      }
      user_secret_stash_main_key_ = UserSecretStash::CreateRandomMainKey();
    }
  }

  return OkStatus<CryptohomeError>();
}

void AuthSession::AddCredentials(
    const user_data_auth::AddCredentialsRequest& request,
    base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
        on_done) {
  user_data_auth::AddCredentialsReply reply;
  CHECK(request.authorization().key().has_data());
  MountStatusOr<std::unique_ptr<Credentials>> credentials_or_err =
      GetCredentials(request.authorization());
  if (!credentials_or_err.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionGetCredFailedInAddCred))
            .Wrap(std::move(credentials_or_err).status()));
    return;
  }

  std::unique_ptr<Credentials> credentials =
      std::move(credentials_or_err).value();

  if (user_has_configured_credential_) {  // AddKeyset
    // Can't add kiosk key for an existing user.
    if (credentials->key_data().type() == KeyData::KEY_TYPE_KIOSK) {
      LOG(WARNING) << "Add Credentials: tried adding kiosk auth for user";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeMountError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionKioskKeyNotAllowedInAddCred),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              MOUNT_ERROR_UNPRIVILEGED_KEY));
      return;
    }

    // At this point we have to have keyset since we have to be authed.
    if (!vault_keyset_) {
      LOG(ERROR)
          << "Add Credentials: tried adding credential before authenticating";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionNotAuthedYetInAddCred),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
    }
  } else {  // AddInitialKeyset
    // If AuthSession is not configured as an ephemeral user, then we save the
    // key to the disk.
    if (is_ephemeral_user_) {
      ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
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
  }

  CreateKeyBlobsToAddKeyset(*credentials.get(),
                            /*initial_keyset=*/!user_has_configured_credential_,
                            std::move(on_done));
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
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInAddKeyset),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }
  if (auth_block_type == AuthBlockType::kChallengeCredential) {
    LOG(ERROR) << "AddCredentials: ChallengeCredential not supported";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionChalCredUnsupportedInAddKeyset),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
    return;
  }

  // Create and initialize fields for auth_input. |auth_state|
  // will be the input to AuthSession::AddVaultKeyset(), which calls
  // VaultKeyset::Encrypt().
  std::optional<brillo::SecureBlob> reset_secret;
  AuthBlock::CreateCallback create_callback;
  if (initial_keyset) {  // AddInitialKeyset operation
    if (auth_block_type == AuthBlockType::kPinWeaver) {
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionPinweaverUnsupportedInAddKeyset),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
      return;
    }
    // For the AddInitialKeyset operation, credential type is never le
    // credentials. So |reset_secret| is given to be std::nullopt.
    reset_secret = std::nullopt;
    create_callback = base::BindOnce(
        &AuthSession::AddVaultKeyset, weak_factory_.GetWeakPtr(),
        credentials.key_data(), credentials.challenge_credentials_keyset_info(),
        std::move(on_done));
  } else {  // AddKeyset operation
    // Create and initialize fields for auth_input.
    if (auth_block_type == AuthBlockType::kPinWeaver) {
      reset_secret = vault_keyset_->GetOrGenerateResetSecret();
    }
    create_callback = base::BindOnce(
        &AuthSession::AddVaultKeyset, weak_factory_.GetWeakPtr(),
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
    DCHECK(!callback_error.ok());
    // TODO(b/229830217): Change the CreateCallback to pass StatusChainOr of
    // (key_blobs, auth_state) and error instead.
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInCallbackInAddKeyset),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlobs derivation failed before adding initial keyset.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInAddKeyset),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(callback_error)));
    return;
  }

  if (user_has_configured_credential_) {  // AddKeyset
    user_data_auth::CryptohomeErrorCode error =
        static_cast<user_data_auth::CryptohomeErrorCode>(
            keyset_management_->AddKeysetWithKeyBlobs(
                obfuscated_username_, key_data, *vault_keyset_.get(),
                std::move(*key_blobs.get()), std::move(auth_state),
                true /*clobber*/));
    // TODO(b/229825202): Migrate Keyset Management and wrap the returned error.
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(kLocAuthSessionAddFailedInAddKeyset),
                       ErrorActionSet({ErrorAction::kReboot}), error));
    return;
  } else {  // AddInitialKeyset
    if (!file_system_keyset_.has_value()) {
      LOG(ERROR) << "AddInitialKeyset: file_system_keyset is invalid.";
      ReplyWithError(std::move(on_done), reply,
                     MakeStatus<CryptohomeError>(
                         CRYPTOHOME_ERR_LOC(kLocAuthSessionNoFSKeyInAddKeyset),
                         ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                                         ErrorAction::kReboot}),
                         user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
      return;
    }
    if (!challenge_credentials_keyset_info.has_value()) {
      LOG(ERROR)
          << "AddInitialKeyset: challenge_credentials_keyset_info is invalid.";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionNoChallengeInfoInAddKeyset),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                              ErrorAction::kReboot}),
              user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
      return;
    }
    CryptohomeStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
        keyset_management_->AddInitialKeysetWithKeyBlobs(
            obfuscated_username_, key_data,
            challenge_credentials_keyset_info.value(),
            file_system_keyset_.value(), std::move(*key_blobs.get()),
            std::move(auth_state));
    if (!vk_status.ok()) {
      vault_keyset_ = nullptr;
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionAddInitialFailedInAddKeyset),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                              ErrorAction::kReboot}),
              user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
      return;
    }
    vault_keyset_ = std::move(vk_status).value();

    // Flip the flag, so that our future invocations go through AddKeyset()
    // and not AddInitialKeyset().
    user_has_configured_credential_ = true;
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    return;
  }
}

void AuthSession::UpdateCredential(
    const user_data_auth::UpdateCredentialRequest& request,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done) {
  user_data_auth::UpdateCredentialReply reply;
  CHECK(request.authorization().key().has_data());
  MountStatusOr<std::unique_ptr<Credentials>> credentials_or_err =
      GetCredentials(request.authorization());
  if (!credentials_or_err.ok()) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(kLocAuthSessionGetCredFailedInUpdate))
                       .Wrap(std::move(credentials_or_err).status()));
    return;
  }

  std::unique_ptr<Credentials> credentials =
      std::move(credentials_or_err).value();

  // Can't update kiosk key for an existing user.
  if (credentials->key_data().type() == KeyData::KEY_TYPE_KIOSK) {
    LOG(ERROR) << "Add Credentials: tried adding kiosk auth for user";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeMountError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionUnsupportedKioskKeyInUpdate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            MOUNT_ERROR_UNPRIVILEGED_KEY));
    return;
  }

  // To update a key, we need to ensure that the existing label and the new
  // label match.
  if (credentials->key_data().label() != request.old_credential_label()) {
    LOG(ERROR) << "AuthorizationRequest does not have a matching label";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(kLocAuthSessionLabelMismatchInUpdate),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
                       user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // At this point we have to have keyset since we have to be authed.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionUnauthedInUpdate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
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
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInUpdate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }
  if (auth_block_type == AuthBlockType::kChallengeCredential) {
    LOG(ERROR) << "UpdateCredentials: ChallengeCredential not supported";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionChalCredUnsupportedInUpdate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
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

  AuthBlock::CreateCallback create_callback = base::BindOnce(
      &AuthSession::UpdateVaultKeyset, weak_factory_.GetWeakPtr(),
      credentials.key_data(), std::move(on_done));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
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
    DCHECK(!callback_error.ok());
    // TODO(b/229830217): Change the CreateCallback to pass StatusOr of
    // (key_blobs, auth_state) and error instead.
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInCallbackInUpdateKeyset),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlobs derivation failed before updating keyset.";
    CryptohomeStatus cryptohome_error = std::move(callback_error);
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInUpdateKeyset))
            .Wrap(std::move(callback_error)));
    return;
  }
  user_data_auth::CryptohomeErrorCode error_code =
      static_cast<user_data_auth::CryptohomeErrorCode>(
          keyset_management_->UpdateKeysetWithKeyBlobs(
              obfuscated_username_, key_data, *vault_keyset_.get(),
              std::move(*key_blobs.get()), std::move(auth_state)));
  // TODO(b/229825202): Migrate Keyset Management and wrap the returned error.
  if (error_code != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocAuthSessionUpdateWithBlobFailedInUpdateKeyset),
                       ErrorActionSet({ErrorAction::kReboot,
                                       ErrorAction::kDevCheckUnexpectedState}),
                       error_code));
  } else {
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
  }
}

CryptohomeStatus AuthSession::Authenticate(
    const cryptohome::AuthorizationRequest& authorization_request) {
  MountStatusOr<std::unique_ptr<Credentials>> credentials_or_err =
      GetCredentials(authorization_request);
  if (!credentials_or_err.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthSessionGetCredFailedInAuth))
        .Wrap(std::move(credentials_or_err).status());
  }
  if (authorization_request.key().data().type() != KeyData::KEY_TYPE_PASSWORD &&
      authorization_request.key().data().type() != KeyData::KEY_TYPE_KIOSK) {
    // AuthSession::Authenticate is only supported for two types of cases
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnsupportedKeyTypesInAuth),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
  }

  std::unique_ptr<Credentials> credentials =
      std::move(credentials_or_err).value();

  // Store key data in current auth_factor for future use.
  key_data_ = credentials->key_data();

  if (!is_ephemeral_user_) {
    // A persistent mount will always have a persistent key on disk. Here
    // keyset_management tries to fetch that persistent credential.
    MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
        keyset_management_->GetValidKeyset(*credentials);
    if (!vk_status.ok()) {
      return MakeStatus<CryptohomeMountError>(
                 CRYPTOHOME_ERR_LOC(kLocAuthSessionGetValidKeysetFailedInAuth))
          .Wrap(std::move(vk_status).status());
    }
    vault_keyset_ = std::move(vk_status).value();
    file_system_keyset_ = FileSystemKeyset(*vault_keyset_);
    // Add the missing fields in the keyset, if any, and resave.
    CryptohomeStatus status = keyset_management_->ReSaveKeysetIfNeeded(
        *credentials, vault_keyset_.get());
    if (!status.ok()) {
      LOG(INFO) << "Non-fatal error in resaving keyset during authentication: "
                << status;
    }
  }

  // Set the credential verifier for this credential.
  credential_verifier_.reset(new ScryptVerifier());
  credential_verifier_->Set(credentials->passkey());

  SetAuthSessionAsAuthenticated();

  return OkStatus<CryptohomeError>();
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
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInAuthAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return false;
  }

  // Fill up the auth input.
  std::optional<AuthInput> auth_input =
      FromProto(request.auth_input(), obfuscated_username_,
                auth_block_utility_->GetLockedToSingleUser());
  if (!auth_input.has_value()) {
    LOG(ERROR) << "Failed to parse auth input for authenticating auth factor";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionInputParseFailedInAuthAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return false;
  }

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  // If the user has configured AuthFactors, then we proceed with USS flow.
  if (user_has_configured_auth_factor_) {
    AuthFactor auth_factor = *label_to_auth_factor_iter->second;

    CryptohomeStatus status = AuthenticateViaUserSecretStash(
        request.auth_factor_label(), auth_input.value(), auth_factor);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to authenticate auth session via factor "
                 << request.auth_factor_label();
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionUSSAuthFailedInAuthAuthFactor))
              .Wrap(std::move(status)));
      return false;
    }

    // Reset LE Credential counter if the current AutFactor is not an
    // LECredential.
    ResetLECredentials();

    // Flip the status on the successful authentication.
    status_ = AuthStatus::kAuthStatusAuthenticated;
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    return true;
  }

  // If user does not have USS AuthFactors, then we switch to authentication
  // with Vaultkeyset. Status is flipped on the successful authentication.
  error = converter_->PopulateKeyDataForVK(
      username_, request.auth_factor_label(), key_data_);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to authenticate auth session via vk-factor "
               << request.auth_factor_label();
    // TODO(b/229834676): Migrate The USS VKK converter then wrap the error.
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionVKConverterFailedInAuthAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}), error));
    return false;
  }
  return AuthenticateViaVaultKeyset(auth_input.value(), std::move(on_done));
}

bool AuthSession::GetRecoveryRequest(
    user_data_auth::GetRecoveryRequestRequest request,
    base::OnceCallback<void(const user_data_auth::GetRecoveryRequestReply&)>
        on_done) {
  user_data_auth::GetRecoveryRequestReply reply;

  // Check the factor exists.
  auto label_to_auth_factor_iter =
      label_to_auth_factor_.find(request.auth_factor_label());
  if (label_to_auth_factor_iter == label_to_auth_factor_.end()) {
    LOG(ERROR) << "Authentication key not found: "
               << request.auth_factor_label();
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocAuthSessionFactorNotFoundInGetRecoveryRequest),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return false;
  }

  // Read CryptohomeRecoveryAuthBlockState.
  AuthFactor* auth_factor = label_to_auth_factor_iter->second.get();
  auto* state = std::get_if<::cryptohome::CryptohomeRecoveryAuthBlockState>(
      &(auth_factor->auth_block_state().state));
  if (!state) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocNoRecoveryAuthBlockStateInGetRecoveryRequest),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return false;
  }

  brillo::SecureBlob ephemeral_pub_key, recovery_request;
  // GenerateRecoveryRequest will set:
  // - `recovery_request` on the `reply` object
  // - `ephemeral_pub_key` which is saved in AuthSession and retrieved during
  // the `AuthenticateAuthFactor` call.
  CryptoStatus status = auth_block_utility_->GenerateRecoveryRequest(
      RequestMetadataFromProto(request),
      brillo::BlobFromString(request.epoch_response()), *state, crypto_->tpm(),
      &recovery_request, &ephemeral_pub_key);
  if (!status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCryptoFailedInGenerateRecoveryRequest))
            .Wrap(std::move(status)));
    return false;
  }

  cryptohome_recovery_ephemeral_pub_key_ = ephemeral_pub_key;
  reply.set_recovery_request(recovery_request.to_string());
  std::move(on_done).Run(reply);
  return true;
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
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionInvalidBlockTypeInAuthViaVaultKey),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return false;
  }

  AuthBlockState auth_state;
  if (!auth_block_utility_->GetAuthBlockStateFromVaultKeyset(
          key_data_.label(), obfuscated_username_, auth_state)) {
    LOG(ERROR) << "Error in obtaining AuthBlock state for key derivation.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionBlockStateMissingInAuthViaVaultKey),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return false;
  }

  // Authenticate and derive KeyBlobs.
  return auth_block_utility_->DeriveKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, auth_state,
      base::BindOnce(&AuthSession::LoadVaultKeysetAndFsKeys,
                     weak_factory_.GetWeakPtr(), auth_input.user_input,
                     std::move(on_done)));
}

void AuthSession::LoadVaultKeysetAndFsKeys(
    const std::optional<brillo::SecureBlob> passkey,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs) {
  user_data_auth::AuthenticateAuthFactorReply reply;

  // The error should be evaluated the same way as it is done in
  // AuthSession::Authenticate(), which directly returns the GetValidKeyset()
  // error. So we are doing a similar error handling here as in
  // KeysetManagement::GetValidKeyset() to preserve the behavior. Empty label
  // case is dropped in here since it is not a valid case anymore.
  if (!key_blobs) {
    DCHECK(!callback_error.ok());
    // TODO(b/229830217): Change the CreateCallback to pass StatusChainOr of
    // (key_blobs, auth_state) and error instead.
    if (callback_error.ok()) {
      // Maps to the default value of MountError which is
      // MOUNT_ERROR_KEY_FAILURE
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthSessionNullParamInCallbackInLoadVaultKeyset),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "Failed to load VaultKeyset since key blobs has not been "
                  "derived.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveFailedInLoadVaultKeyset))
            .Wrap(std::move(callback_error)));
    return;
  }

  DCHECK(callback_error.ok());

  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      keyset_management_->GetValidKeysetWithKeyBlobs(
          obfuscated_username_, std::move(*key_blobs.get()), key_data_.label());
  if (!vk_status.ok()) {
    vault_keyset_ = nullptr;

    LOG(ERROR) << "Failed to load VaultKeyset and file system keyset.";

    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeMountError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionGetValidKeysetFailedInLoadVaultKeyset))
            .Wrap(std::move(vk_status).status()));
    return;
  }

  vault_keyset_ = std::move(vk_status).value();

  // Authentication is successfully completed. Reset LE Credential counter if
  // the current AutFactor is not an LECredential.
  if (!vault_keyset_->IsLECredential()) {
    keyset_management_->ResetLECredentials(std::nullopt, *vault_keyset_,
                                           obfuscated_username_);
  }
  ResaveVaultKeysetIfNeeded(passkey);
  file_system_keyset_ = FileSystemKeyset(*vault_keyset_);

  // Flip the status on the successful authentication.
  SetAuthSessionAsAuthenticated();

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void AuthSession::ResaveVaultKeysetIfNeeded(
    const std::optional<brillo::SecureBlob> user_input) {
  // Check whether an update is needed for the VaultKeyset. If the user setup
  // their account and the TPM was not owned, re-save it with the TPM.
  VaultKeyset updated_vault_keyset = *vault_keyset_.get();
  if (!keyset_management_->ShouldReSaveKeyset(&updated_vault_keyset)) {
    // No change is needed for |vault_keyset_|
    return;
  }

  // KeyBlobs needs to be re-created since there maybe a change in the
  // AuthBlock type with the change in TPM state. Don't abort on failure.
  // Only password and pin type credentials are evaluated for resave. Therefore
  // we don't need the asynchronous KeyBlob creation.
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeForCreation(
          vault_keyset_->IsLECredential(), /*is_challenge_credential*/ false);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR)
        << "Error in creating obtaining AuthBlockType, can't resave keyset.";
    return;
  }
  std::optional<brillo::SecureBlob> reset_secret;
  if (auth_block_type == AuthBlockType::kPinWeaver) {
    reset_secret = vault_keyset_->GetOrGenerateResetSecret();
  }
  // Create and initialize fields for AuthInput.
  AuthInput auth_input = {user_input,
                          /*locked_to_single_user=*/std::nullopt,
                          obfuscated_username_, reset_secret};
  AuthBlock::CreateCallback create_callback =
      base::BindOnce(&AuthSession::ResaveKeysetOnKeyBlobsGenerated,
                     base::Unretained(this), std::move(updated_vault_keyset));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input,
      /*CreateCallback*/ std::move(create_callback));
}

void AuthSession::ResaveKeysetOnKeyBlobsGenerated(
    VaultKeyset updated_vault_keyset,
    CryptoStatus error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  if (!error.ok() || key_blobs == nullptr || auth_block_state == nullptr) {
    LOG(ERROR) << "Error in creating KeyBlobs, can't resave keyset.";
    return;
  }

  CryptohomeStatus status = keyset_management_->ReSaveKeysetWithKeyBlobs(
      updated_vault_keyset, std::move(*key_blobs), std::move(auth_block_state));
  // Updated ketyset is saved on the disk, it is safe to update
  // |vault_keyset_|.
  vault_keyset_ = std::make_unique<VaultKeyset>(updated_vault_keyset);
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

MountStatusOr<std::unique_ptr<Credentials>> AuthSession::GetCredentials(
    const cryptohome::AuthorizationRequest& authorization_request) {
  auto credentials = std::make_unique<Credentials>(
      username_, brillo::SecureBlob(authorization_request.key().secret()));
  credentials->set_key_data(authorization_request.key().data());

  if (authorization_request.key().data().type() == KeyData::KEY_TYPE_KIOSK) {
    if (!credentials->passkey().empty()) {
      LOG(ERROR) << "Non-empty passkey in kiosk key.";
      return MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNonEmptyKioskKeyInGetCred),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          MountError::MOUNT_ERROR_INVALID_ARGS);
    }
    brillo::SecureBlob public_mount_passkey =
        keyset_management_->GetPublicMountPassKey(username_);
    if (public_mount_passkey.empty()) {
      LOG(ERROR) << "Could not get public mount passkey.";
      return MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionEmptyPublicMountKeyInGetCred),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          MountError::MOUNT_ERROR_KEY_FAILURE);
    }
    credentials->set_passkey(public_mount_passkey);
  }

  return credentials;
}

CryptohomeStatus AuthSession::AddAuthFactor(
    const user_data_auth::AddAuthFactorRequest& request) {
  // Preconditions:
  DCHECK_EQ(request.auth_session_id(), serialized_token_);

  // TODO(b/216804305): Verify the auth session is authenticated, after
  // `OnUserCreated()` is changed to mark the session authenticated.
  // At this point AuthSession should be authenticated as it needs
  // FileSystemKeys to wrap the new credentials.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnauthedInAddAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
  }

  AuthFactorMetadata auth_factor_metadata;
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  if (!GetAuthFactorMetadata(request.auth_factor(), auth_factor_metadata,
                             auth_factor_type, auth_factor_label)) {
    LOG(ERROR) << "Failed to parse new auth factor parameters";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnknownFactorInAddAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  std::optional<AuthInput> auth_input =
      FromProto(request.auth_input(), obfuscated_username_,
                auth_block_utility_->GetLockedToSingleUser());
  if (!auth_input.has_value()) {
    LOG(ERROR) << "Failed to parse auth input for new auth factor";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoInputInAddAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  if (user_secret_stash_) {
    // The user has a UserSecretStash (either because it's a new user and the
    // experiment is on or it's an existing user who went through this flow), so
    // proceed with wrapping the USS via the new factor and persisting both.

    // Anything backed PinWeaver needs a reset secret. The list of is_le_cred
    // could expand in the future.
    if (NeedsResetSecret(auth_factor_type)) {
      auth_input->reset_secret = std::make_optional<brillo::SecureBlob>(
          CreateSecureRandomBlob(CRYPTOHOME_RESET_SECRET_LENGTH));
    }

    return AddAuthFactorViaUserSecretStash(auth_factor_type, auth_factor_label,
                                           auth_factor_metadata,
                                           auth_input.value());
  }

  // TODO(b/3319388): Implement for the vault keyset case.
  return MakeStatus<CryptohomeError>(
      CRYPTOHOME_ERR_LOC(kLocAuthSessionVKUnsupportedInAddAuthFactor),
      ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
      user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
}

CryptohomeStatus AuthSession::AddAuthFactorViaUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input) {
  // Preconditions.
  DCHECK(user_secret_stash_);
  DCHECK(user_secret_stash_main_key_.has_value());

  // 1. Create a new auth factor in-memory, by executing auth block's Create().
  KeyBlobs key_blobs;
  CryptohomeStatusOr<std::unique_ptr<AuthFactor>> auth_factor_or_status =
      AuthFactor::CreateNew(auth_factor_type, auth_factor_label,
                            auth_factor_metadata, auth_input,
                            auth_block_utility_, key_blobs);
  if (!auth_factor_or_status.ok()) {
    LOG(ERROR) << "Failed to create new auth factor";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionCreateAuthFactorFailedInAddViaUSS),
               user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
        .Wrap(std::move(auth_factor_or_status).status());
  }

  // 2. Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs.DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR) << "Failed to derive credential secret for created auth factor";
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveUSSSecretFailedInAddViaUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  }

  // 3. Add the new factor into the USS in-memory.
  // This wraps the USS Main Key with the credential secret. The wrapping_id
  // field is defined equal to the factor's label.
  if (!user_secret_stash_->AddWrappedMainKey(
          user_secret_stash_main_key_.value(),
          /*wrapping_id=*/auth_factor_label, uss_credential_secret.value())) {
    LOG(ERROR) << "Failed to add created auth factor into user secret stash";
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionAddMainKeyFailedInAddViaUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  }

  if (auth_input.reset_secret.has_value() &&
      !user_secret_stash_->SetResetSecretForLabel(
          auth_factor_label, auth_input.reset_secret.value())) {
    LOG(ERROR) << "Failed to insert reset secret for auth factor";
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionAddResetSecretFailedInAddViaUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  }

  // 4. Encrypt the updated USS.
  std::optional<brillo::Blob> encrypted_uss_container =
      user_secret_stash_->GetEncryptedContainer(
          user_secret_stash_main_key_.value());
  if (!encrypted_uss_container.has_value()) {
    LOG(ERROR)
        << "Failed to encrypt user secret stash after auth factor creation";
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionEncryptFailedInAddViaUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  }

  // 5. Persist the factor.
  // It's important to do this after all steps ##1-4, so that we only start
  // writing files after all validity checks (like the label duplication check).
  std::unique_ptr<AuthFactor> auth_factor =
      std::move(auth_factor_or_status).value();
  CryptohomeStatus status =
      auth_factor_manager_->SaveAuthFactor(obfuscated_username_, *auth_factor);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to persist created auth factor";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionPersistFactorFailedInAddViaUSS),
               user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
        .Wrap(std::move(status));
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
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionPersistUSSFailedInAddViaUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  }

  label_to_auth_factor_.emplace(auth_factor_label, std::move(auth_factor));
  user_has_configured_auth_factor_ = true;

  return OkStatus<CryptohomeError>();
}

CryptohomeStatus AuthSession::AuthenticateViaUserSecretStash(
    const std::string& auth_factor_label,
    const AuthInput auth_input,
    AuthFactor& auth_factor) {
  // TODO(b/223207622): This step is the same for both USS and
  // VaultKeyset other than how the AuthBlock state is obtained. Make the
  // derivation for USS asynchronous and merge these two.
  KeyBlobs key_blobs;
  CryptoStatus crypto_status =
      auth_factor.Authenticate(auth_input, auth_block_utility_, key_blobs);
  if (!crypto_status.ok()) {
    LOG(ERROR) << "Failed to authenticate auth session via factor "
               << auth_factor_label;
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthSessionAuthFactorAuthFailedInAuthUSS))
        .Wrap(std::move(crypto_status));
  }

  // Use USS to finish the authentication.
  CryptohomeStatus status =
      LoadUSSMainKeyAndFsKeyset(auth_factor_label, key_blobs);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to authenticate auth session via factor "
               << auth_factor_label;
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthSessionLoadUSSFailedInAuthUSS))
        .Wrap(std::move(status));
  }
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus AuthSession::LoadUSSMainKeyAndFsKeyset(
    const std::string& auth_factor_label, const KeyBlobs& key_blobs) {
  // 1. Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs.DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR)
        << "Failed to derive credential secret for authenticating auth factor";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveUSSSecretFailedInLoadUSS),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  }

  // 2. Load the USS container with the encrypted payload.
  std::optional<brillo::Blob> encrypted_uss =
      user_secret_stash_storage_->LoadPersisted(obfuscated_username_);
  if (!encrypted_uss.has_value()) {
    LOG(ERROR) << "Failed to load the user secret stash";
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionLoadUSSFailedInLoadUSS),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
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
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDecryptUSSFailedInLoadUSS),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kIncorrectAuth}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }
  user_secret_stash_main_key_ = decrypted_main_key;

  // 4. Populate data fields from the USS.
  file_system_keyset_ = user_secret_stash_->GetFileSystemKeyset();

  return OkStatus<CryptohomeError>();
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

    // Get the reset secret from the USS for this auth factor label.
    const std::string auth_factor_label = iter.first;
    std::optional<brillo::SecureBlob> reset_secret =
        user_secret_stash_->GetResetSecretForLabel(auth_factor_label);
    if (!reset_secret.has_value()) {
      LOG(WARNING) << "No reset secret for auth factor with label "
                   << auth_factor_label << ", and cannot reset credential.";
      continue;
    }

    // Reset the attempt count for the pinweaver leaf.
    // If there is an error, warn for the error in log.
    if (!crypto_->ResetLeCredentialEx(state->le_label.value(),
                                      reset_secret.value(), error)) {
      LOG(WARNING) << "Failed to reset an LE credential: " << error;
    }
  }
}

base::TimeDelta AuthSession::GetRemainingTime() {
  DCHECK(timer_.IsRunning());
  auto time_passed = base::TimeTicks::Now() - start_time_;
  auto time_left = timer_.GetCurrentDelay() - time_passed;
  return time_left;
}

}  // namespace cryptohome
