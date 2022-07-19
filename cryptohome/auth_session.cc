// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <cryptohome/scrypt_verifier.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block.h"
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
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/signature_sealing/structures_proto.h"
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
using hwsec_foundation::HmacSha256;
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
// Message to use when generating a secret for hibernate.
constexpr char kHibernateSecretHmacMessage[] = "AuthTimeHibernateSecret";

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

  // TODO(hardikgoyal): make a factory function for AuthSession so the
  // constructor doesn't need to do work
  auth_session_creation_time_ = base::TimeTicks::Now();

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

  RecordAuthSessionStart();

  // If the Auth Session is started for an ephemeral user, we always start in an
  // authenticated state.
  if (is_ephemeral_user_) {
    SetAuthSessionAsAuthenticated();
  }
}

AuthSession::~AuthSession() {
  std::string append_string = is_ephemeral_user_ ? ".Ephemeral" : ".Persistent";
  ReportTimerDuration(kAuthSessionTotalLifetimeTimer,
                      auth_session_creation_time_, append_string);
  ReportTimerDuration(kAuthSessionAuthenticatedLifetimeTimer,
                      authenticated_time_, append_string);
}

void AuthSession::AuthSessionTimedOut() {
  LOG(INFO) << "AuthSession: timed out.";
  status_ = AuthStatus::kAuthStatusTimedOut;
  // After this call back to |UserDataAuth|, |this| object will be deleted.
  std::move(on_timeout_).Run(token_);
}

void AuthSession::RecordAuthSessionStart() const {
  std::vector<std::string> keys;
  for (const auto& [label, key_data] : key_label_data_) {
    bool is_le_credential = key_data.policy().low_entropy_credential();
    keys.push_back(base::StringPrintf("%s(type %d%s)", label.c_str(),
                                      static_cast<int>(key_data.type()),
                                      is_le_credential ? " le" : ""));
  }
  LOG(INFO) << "AuthSession: started with is_ephemeral_user="
            << is_ephemeral_user_ << " user_exists=" << user_exists_
            << " keys=" << base::JoinString(keys, ",") << ".";
}

void AuthSession::SetAuthSessionAsAuthenticated() {
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    status_ = AuthStatus::kAuthStatusAuthenticated;
    // Record time of authentication for metric keeping.
    authenticated_time_ = base::TimeTicks::Now();
    LOG(INFO) << "AuthSession: authenticated.";
  }
  SetTimeoutTimer(kAuthSessionTimeout);
}

void AuthSession::SetTimeoutTimer(const base::TimeDelta& delay) {
  DCHECK_GT(delay, base::Minutes(0));

  // |.start_time| and |.timer| need to be set at the same time.
  timeout_timer_start_time_ = base::TimeTicks::Now();
  timeout_timer_.Start(FROM_HERE, delay,
                       base::BindOnce(&AuthSession::AuthSessionTimedOut,
                                      base::Unretained(this)));
}

CryptohomeStatus AuthSession::ExtendTimeoutTimer(
    const base::TimeDelta extension_duration) {
  // Check to make sure that the AuthSesion is still valid before we stop the
  // timer.
  if (status_ == AuthStatus::kAuthStatusTimedOut) {
    // AuthSession timed out before timeout_timer_.Stop() could be called.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionTimedOutInExtend),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  }

  // Calculate time remaining and add extension_duration to it.
  auto extended_delay = GetRemainingTime() + extension_duration;
  SetTimeoutTimer(extended_delay);
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
      CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> uss_status =
          UserSecretStash::CreateRandom(file_system_keyset_.value());
      if (!uss_status.ok()) {
        LOG(ERROR) << "User secret stash creation failed";
        return MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateUSSFailedInOnUserCreated),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
      }
      user_secret_stash_ = std::move(uss_status).value();
      user_secret_stash_main_key_ = UserSecretStash::CreateRandomMainKey();
    }
  }

  return OkStatus<CryptohomeError>();
}

template <typename AddKeyReply>
void AuthSession::AddVaultKeyset(
    const KeyData& key_data,
    AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const AddKeyReply&)> on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  AddKeyReply reply;
  // callback_error, key_blobs and auth_state are returned by
  // AuthBlock::CreateCallback.
  if (!callback_error.ok() || key_blobs == nullptr || auth_state == nullptr) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInCallbackInAddKeyset),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlobs derivation failed before adding keyset.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInAddKeyset),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(callback_error)));
    return;
  }

  if (user_has_configured_credential_) {  // AddKeyset
    // TODO(b/229825202): Migrate Keyset Management and wrap the returned error.
    user_data_auth::CryptohomeErrorCode error =
        static_cast<user_data_auth::CryptohomeErrorCode>(
            keyset_management_->AddKeysetWithKeyBlobs(
                obfuscated_username_, key_data, *vault_keyset_.get(),
                std::move(*key_blobs.get()), std::move(auth_state),
                true /*clobber*/));
    if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionAddFailedInAddKeyset),
              ErrorActionSet({ErrorAction::kReboot}), error));
      return;
    }
    LOG(INFO) << "AuthSession: added additional keyset " << key_data.label()
              << ".";
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
    CryptohomeStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
        keyset_management_->AddInitialKeysetWithKeyBlobs(
            obfuscated_username_, key_data,
            /*challenge_credentials_keyset_info*/ std::nullopt,
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
    LOG(INFO) << "AuthSession: added initial keyset " << key_data.label()
              << ".";
    vault_keyset_ = std::move(vk_status).value();

    // Flip the flag, so that our future invocations go through AddKeyset()
    // and not AddInitialKeyset().
    if (auth_input.user_input.has_value()) {
      SetCredentialVerifier(auth_input.user_input.value());
    }
    user_has_configured_credential_ = true;
  }

  std::unique_ptr<AuthFactor> added_auth_factor =
      converter_->VaultKeysetToAuthFactor(username_, key_data.label());
  if (added_auth_factor) {
    LOG(INFO) << "Label to AuthFactor map is created for the keyset.";
    label_to_auth_factor_.emplace(key_data.label(),
                                  std::move(added_auth_factor));
  }

  // Report timer for how long AuthSession operation takes.
  ReportTimerDuration(auth_session_performance_timer.get());
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

template <typename AddKeyReply>
void AuthSession::CreateKeyBlobsToAddKeyset(
    const cryptohome::AuthorizationRequest& authorization,
    AuthInput auth_input,
    const KeyData& key_data,
    bool initial_keyset,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const AddKeyReply&)> on_done) {
  AddKeyReply reply;
  AuthBlockType auth_block_type;
  bool is_le_credential = key_data.policy().low_entropy_credential();
  bool is_challenge_credential =
      key_data.type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE;

  // Generate KeyBlobs and AuthBlockState used for VaultKeyset encryption.
  auth_block_type = auth_block_utility_->GetAuthBlockTypeForCreation(
      is_le_credential, /*is_recovery=*/false, is_challenge_credential,
      AuthFactorStorageType::kVaultKeyset);
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

  // Parameterize the AuthSession performance timer by AuthBlockType
  auth_session_performance_timer->auth_block_type = auth_block_type;

  // |auth_state| will be the input to AuthSession::AddVaultKeyset(),
  // which calls VaultKeyset::Encrypt().
  if (initial_keyset && auth_block_type == AuthBlockType::kPinWeaver) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionPinweaverUnsupportedInAddKeyset),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
    return;
  }

  if (auth_block_type == AuthBlockType::kChallengeCredential) {
    if (!ConstructAuthInputForChallengeCredentials(authorization, auth_input)) {
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionAddCredentialInvalidAuthInput),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
    }
  }

  auto create_callback = base::BindOnce(
      &AuthSession::AddVaultKeyset<AddKeyReply>, weak_factory_.GetWeakPtr(),
      key_data, auth_input, std::move(auth_session_performance_timer),
      std::move(on_done));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
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

  // Record current time for timing for how long AddCredentials will take.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAddCredentialsTimer);

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
  } else if (is_ephemeral_user_) {
    // If AuthSession is configured as an ephemeral user, then we do not save
    // the key to the disk.
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    return;
  } else {  // AddInitialKeyset
    DCHECK(!vault_keyset_);
    if (!file_system_keyset_.has_value()) {
      // Creating file_system_keyset to the prepareVault call next.
      // This is needed to support the old case where authentication happened
      // before creation of user and will be temporary as it is an intermediate
      // milestone.
      file_system_keyset_ = FileSystemKeyset::CreateRandom();
    }
  }
  // |reset_secret| is updated in CreateKeyBlobsToAddKeyset() if the key type
  // is LE credentials.
  AuthInput auth_input = {credentials->passkey(),
                          /*locked_to_single_user=*/std::nullopt,
                          obfuscated_username_, std::nullopt /*reset_secret*/,
                          /*reset_seed*/ std::nullopt};
  if (user_has_configured_credential_) {
    auth_input.reset_seed = vault_keyset_->GetResetSeed();
  }

  bool is_initial_keyset = !user_has_configured_credential_;

  CreateKeyBlobsToAddKeyset<user_data_auth::AddCredentialsReply>(
      request.authorization(), auth_input, credentials->key_data(),
      is_initial_keyset, std::move(auth_session_performance_timer),
      std::move(on_done));
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
      is_le_credential, /*is_recovery=*/false, is_challenge_credential,
      AuthFactorStorageType::kVaultKeyset);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInUpdate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  // Report timer for how long UpdateCredentials operation takes and
  // record current time for timing for how long UpdateCredentials will take.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionUpdateCredentialsTimer, auth_block_type);

  // Create and initialize fields for auth_input.
  AuthInput auth_input = {credentials.passkey(),
                          /*locked_to_single_user=*/std::nullopt,
                          obfuscated_username_, /*reset_secret*/ std::nullopt,
                          /*reset_seed*/ std::nullopt};

  if (vault_keyset_) {
    auth_input.reset_seed = vault_keyset_->GetResetSeed();
  }

  AuthBlock::CreateCallback create_callback = base::BindOnce(
      &AuthSession::UpdateVaultKeyset, weak_factory_.GetWeakPtr(),
      credentials.key_data(), auth_input,
      std::move(auth_session_performance_timer), std::move(on_done));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
}

void AuthSession::UpdateVaultKeyset(
    const KeyData& key_data,
    AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  user_data_auth::UpdateCredentialReply reply;
  if (!callback_error.ok() || key_blobs == nullptr || auth_state == nullptr) {
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
    if (auth_input.user_input.has_value()) {
      SetCredentialVerifier(auth_input.user_input.value());
    }
    ReportTimerDuration(auth_session_performance_timer.get());
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
  }
}

template <typename AuthenticateReply>
bool AuthSession::AuthenticateViaVaultKeyset(
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const AuthenticateReply&)> on_done) {
  AuthenticateReply reply;
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeForDerivation(key_data_.label(),
                                                         obfuscated_username_);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR) << "Error in obtaining AuthBlock type for key derivation.";
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<error::CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionInvalidBlockTypeInAuthViaVaultKey),
            ErrorActionSet({error::ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return false;
  }

  // Parameterize the AuthSession performance timer by AuthBlockType
  auth_session_performance_timer->auth_block_type = auth_block_type;

  AuthBlockState auth_state;
  if (!auth_block_utility_->GetAuthBlockStateFromVaultKeyset(
          key_data_.label(), obfuscated_username_, auth_state /*Out*/)) {
    LOG(ERROR) << "Error in obtaining AuthBlock state for key derivation.";
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<error::CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionBlockStateMissingInAuthViaVaultKey),
            ErrorActionSet({error::ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return false;
  }

  // Derive KeyBlobs from the existing VaultKeyset, using GetValidKeyset
  // as a callback that loads |vault_keyset_| and resaves if needed.
  AuthBlock::DeriveCallback derive_callback = base::BindOnce(
      &AuthSession::LoadVaultKeysetAndFsKeys<AuthenticateReply>,
      weak_factory_.GetWeakPtr(), auth_input.user_input, auth_block_type,
      std::move(auth_session_performance_timer), std::move(on_done));

  return auth_block_utility_->DeriveKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, auth_state, std::move(derive_callback));
}

template <typename AuthenticateReply>
void AuthSession::LoadVaultKeysetAndFsKeys(
    const std::optional<brillo::SecureBlob> passkey,
    const AuthBlockType& auth_block_type,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const AuthenticateReply&)> on_done,
    CryptoStatus status,
    std::unique_ptr<KeyBlobs> key_blobs) {
  AuthenticateReply reply;
  // The error should be evaluated the same way as it is done in
  // AuthSession::Authenticate(), which directly returns the GetValidKeyset()
  // error. So we are doing a similar error handling here as in
  // KeysetManagement::GetValidKeyset() to preserve the behavior. Empty label
  // case is dropped in here since it is not a valid case anymore.
  if (!status.ok() || !key_blobs) {
    // For LE credentials, if deriving the key blobs failed due to too many
    // attempts, set auth_locked=true in the corresponding keyset. Then save it
    // for future callers who can Load it w/o Decrypt'ing to check that flag.
    // When the pin is entered wrong and AuthBlock fails to derive the KeyBlobs
    // it doesn't make it into the VaultKeyset::Decrypt(); so auth_lock should
    // be set here.
    if (status->local_crypto_error() == CryptoError::CE_CREDENTIAL_LOCKED) {
      // Get the corresponding encrypted vault keyset for the user and the label
      // to set the auth_locked.
      std::unique_ptr<VaultKeyset> vk = keyset_management_->GetVaultKeyset(
          obfuscated_username_, key_data_.label());
      if (vk != nullptr) {
        LOG(INFO) << "PIN is locked out due to too many wrong attempts.";
        vk->SetAuthLocked(true);
        vk->Save(vk->GetSourceFile());
      }
    }
    if (status.ok()) {
      // Maps to the default value of MountError which is
      // MOUNT_ERROR_KEY_FAILURE
      status = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthSessionNullParamInCallbackInLoadVaultKeyset),
          ErrorActionSet({error::ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "Failed to load VaultKeyset since authentication has failed";
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<error::CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveFailedInLoadVaultKeyset))
            .Wrap(std::move(status)));
    return;
  }

  DCHECK(status.ok());

  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      keyset_management_->GetValidKeysetWithKeyBlobs(
          obfuscated_username_, std::move(*key_blobs.get()), key_data_.label());
  if (!vk_status.ok()) {
    vault_keyset_ = nullptr;

    LOG(ERROR) << "Failed to load VaultKeyset and file system keyset.";
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
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
    keyset_management_->ResetLECredentialsWithValidatedVK(*vault_keyset_,
                                                          obfuscated_username_);
  }
  ResaveVaultKeysetIfNeeded(passkey);
  file_system_keyset_ = FileSystemKeyset(*vault_keyset_);

  // Flip the status on the successful authentication.
  SetAuthSessionAsAuthenticated();

  // Set the credential verifier for this credential.
  if (passkey.has_value()) {
    SetCredentialVerifier(passkey.value());
  }

  reply.set_authenticated(GetStatus() == AuthStatus::kAuthStatusAuthenticated);
  ReportTimerDuration(auth_session_performance_timer.get());
  ReplyWithError(std::move(on_done), reply, OkStatus<error::CryptohomeError>());
}

void AuthSession::Authenticate(
    const cryptohome::AuthorizationRequest& authorization_request,
    base::OnceCallback<
        void(const user_data_auth::AuthenticateAuthSessionReply&)> on_done) {
  LOG(INFO) << "AuthSession: authentication attempt via "
            << authorization_request.key().data().label() << ".";

  MountStatusOr<std::unique_ptr<Credentials>> credentials_or_err =
      GetCredentials(authorization_request);

  user_data_auth::AuthenticateAuthSessionReply reply;
  CryptohomeStatus err = OkStatus<CryptohomeError>();

  if (!credentials_or_err.ok()) {
    err = MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionGetCredFailedInAuth))
              .Wrap(std::move(credentials_or_err).status());
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
    ReplyWithError(std::move(on_done), reply, std::move(err));
    return;
  }

  if (authorization_request.key().data().type() != KeyData::KEY_TYPE_PASSWORD &&
      authorization_request.key().data().type() != KeyData::KEY_TYPE_KIOSK &&
      authorization_request.key().data().type() !=
          KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    // AuthSession::Authenticate is only supported for three types of cases
    err = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnsupportedKeyTypesInAuth),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
    ReplyWithError(std::move(on_done), reply, std::move(err));
    return;
  }

  std::unique_ptr<Credentials> credentials =
      std::move(credentials_or_err).value();

  if (credentials->key_data().label().empty()) {
    LOG(ERROR) << "Authenticate: Credentials key_data.label() is empty.";
    err = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionEmptyKeyLabelInAuth),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
    ReplyWithError(std::move(on_done), reply, std::move(err));
    return;
  }

  // Store key data in current auth_factor for future use.
  key_data_ = credentials->key_data();

  // Record current time for timing for how long Authenticate will take.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAuthenticateTimer);

  if (is_ephemeral_user_) {  // Ephemeral mount.
    // For ephemeral session, just authenticate the session,
    // no need to derive KeyBlobs.
    // Set the credential verifier for this credential.
    SetCredentialVerifier(credentials->passkey());

    // SetAuthSessionAsAuthenticated() should already have been called
    // in the constructor by this point.
    reply.set_authenticated(GetStatus() ==
                            AuthStatus::kAuthStatusAuthenticated);
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    return;
  }
  // Persistent mount.
  // A persistent mount will always have a persistent key on disk. Here
  // keyset_management tries to fetch that persistent credential.
  // TODO(dlunev): fix conditional error when we switch to StatusOr.
  AuthInput auth_input = {credentials->passkey(),
                          /*locked_to_single_user=*/
                          auth_block_utility_->GetLockedToSingleUser()};
  if (authorization_request.key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    if (!ConstructAuthInputForChallengeCredentials(authorization_request,
                                                   auth_input)) {
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionAuthenticateInvalidAuthInput),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
    }
  }
  AuthenticateViaVaultKeyset<user_data_auth::AuthenticateAuthSessionReply>(
      auth_input, std::move(auth_session_performance_timer),
      std::move(on_done));
}

const FileSystemKeyset& AuthSession::file_system_keyset() const {
  DCHECK(file_system_keyset_.has_value());
  return file_system_keyset_.value();
}

bool AuthSession::AuthenticateAuthFactor(
    const user_data_auth::AuthenticateAuthFactorRequest& request,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done) {
  LOG(INFO) << "AuthSession: authentication attempt via "
            << request.auth_factor_label() << " factor.";

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
      CreateAuthInput(request.auth_input(), obfuscated_username_,
                      auth_block_utility_->GetLockedToSingleUser(),
                      cryptohome_recovery_ephemeral_pub_key_);
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

    // Record current time for timing for how long AuthenticateAuthFactor will
    // take.
    auto auth_session_performance_timer =
        std::make_unique<AuthSessionPerformanceTimer>(
            kAuthSessionAuthenticateAuthFactorUSSTimer);

    AuthenticateViaUserSecretStash(request.auth_factor_label(),
                                   auth_input.value(),
                                   std::move(auth_session_performance_timer),
                                   auth_factor, std::move(on_done));
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
  // Record current time for timing for how long AuthenticateAuthFactor will
  // take.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAuthenticateAuthFactorVKTimer);

  return AuthenticateViaVaultKeyset<
      user_data_auth::AuthenticateAuthFactorReply>(
      auth_input.value(), std::move(auth_session_performance_timer),
      std::move(on_done));
}

void AuthSession::RemoveAuthFactor(
    const user_data_auth::RemoveAuthFactorRequest& request,
    base::OnceCallback<void(const user_data_auth::RemoveAuthFactorReply&)>
        on_done) {
  if (user_secret_stash_) {
    RemoveAuthFactorViaUserSecretStash(request.auth_factor_label(),
                                       std::move(on_done));
    return;
  }
  // TODO(b/236869367): Implement for VaultKeyset users.
  user_data_auth::RemoveAuthFactorReply reply;
  ReplyWithError(
      std::move(on_done), reply,
      MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthSessionVaultKeysetNotImplementedInRemoveAuthFactor),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
}

void AuthSession::RemoveAuthFactorViaUserSecretStash(
    const std::string& auth_factor_label,
    base::OnceCallback<void(const user_data_auth::RemoveAuthFactorReply&)>
        on_done) {
  // Preconditions.
  DCHECK(user_secret_stash_);
  DCHECK(user_secret_stash_main_key_.has_value());

  user_data_auth::RemoveAuthFactorReply reply;

  auto label_to_auth_factor_iter =
      label_to_auth_factor_.find(auth_factor_label);
  if (label_to_auth_factor_iter == label_to_auth_factor_.end()) {
    LOG(ERROR) << "AuthSession: Key to remove not found: " << auth_factor_label;
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInRemoveAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  if (label_to_auth_factor_.size() == 1) {
    LOG(ERROR) << "AuthSession: Cannot remove the last auth factor.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionLastFactorInRemoveAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_REMOVE_CREDENTIALS_FAILED));
    return;
  }

  AuthFactor auth_factor = *label_to_auth_factor_iter->second;
  CryptohomeStatus status = auth_factor_manager_->RemoveAuthFactor(
      obfuscated_username_, auth_factor, auth_block_utility_);
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to remove auth factor.";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocAuthSessionRemoveFactorFailedInRemoveAuthFactor),
                       user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
                       .Wrap(std::move(status)));
    return;
  }

  // Remove the auth factor from the map.
  label_to_auth_factor_.erase(label_to_auth_factor_iter);

  // Remove the auth factor from USS.
  if (!user_secret_stash_->RemoveWrappedMainKey(
          /*wrapping_id=*/auth_factor_label)) {
    LOG(ERROR)
        << "AuthSession: Failed to remove auth factor from user secret stash.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionRemoveMainKeyFailedInRemoveAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED));
    return;
  }
  // Note: we may or may not have a reset secret for this auth factor -
  // therefore we don't check the return value.
  user_secret_stash_->RemoveResetSecretForLabel(auth_factor_label);
  CryptohomeStatusOr<brillo::Blob> encrypted_uss_container =
      user_secret_stash_->GetEncryptedContainer(
          user_secret_stash_main_key_.value());
  if (!encrypted_uss_container.ok()) {
    LOG(ERROR) << "AuthSession: Failed to encrypt user secret stash after auth "
                  "factor removal.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionEncryptFailedInRemoveAuthFactor),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
            .Wrap(std::move(encrypted_uss_container).status()));
    return;
  }
  status = user_secret_stash_storage_->Persist(encrypted_uss_container.value(),
                                               obfuscated_username_);
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to persist user secret stash after auth "
                  "factor removal.";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocAuthSessionPersistUSSFailedInRemoveAuthFactor),
                       user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
                       .Wrap(std::move(status)));
    return;
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
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
  if (auth_factor->type() != AuthFactorType::kCryptohomeRecovery) {
    LOG(ERROR) << "GetRecoveryRequest can be called only for "
                  "kCryptohomeRecovery auth factor";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocWrongAuthFactorInGetRecoveryRequest),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return false;
  }

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
      obfuscated_username_, RequestMetadataFromProto(request),
      brillo::BlobFromString(request.epoch_response()), *state,
      crypto_->GetRecoveryCryptoBackend(), &recovery_request,
      &ephemeral_pub_key);
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

void AuthSession::ResaveVaultKeysetIfNeeded(
    const std::optional<brillo::SecureBlob> user_input) {
  // Check whether an update is needed for the VaultKeyset. If the user setup
  // their account and the TPM was not owned, re-save it with the TPM.
  // Also check whether the VaultKeyset has a wrapped reset seed and add reset
  // seed if missing.
  bool needs_update = false;
  VaultKeyset updated_vault_keyset = *vault_keyset_.get();
  if (keyset_management_->ShouldReSaveKeyset(&updated_vault_keyset)) {
    needs_update = true;
  }

  // Adds a reset seed only to the password VaultKeysets.
  if (keyset_management_->AddResetSeedIfMissing(updated_vault_keyset)) {
    needs_update = true;
  }

  if (needs_update == false) {
    // No change is needed for |vault_keyset_|
    return;
  }

  // KeyBlobs needs to be re-created since there maybe a change in the
  // AuthBlock type with the change in TPM state. Don't abort on failure.
  // Only password and pin type credentials are evaluated for resave. Therefore
  // we don't need the asynchronous KeyBlob creation.
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeForCreation(
          vault_keyset_->IsLECredential(), /*is_recovery=*/false,
          /*is_challenge_credential*/ false,
          AuthFactorStorageType::kVaultKeyset);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR)
        << "Error in creating obtaining AuthBlockType, can't resave keyset.";
    return;
  }
  if (auth_block_type == AuthBlockType::kPinWeaver) {
    LOG(ERROR) << "Pinweaver AuthBlock is not supported for resave operation, "
                  "can't resave keyset.";
    return;
  }

  // Create and initialize fields for AuthInput.
  AuthInput auth_input = {user_input,
                          /*locked_to_single_user=*/std::nullopt,
                          obfuscated_username_, /*reset_secret=*/std::nullopt,
                          /*reset_seed=*/std::nullopt};
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

void AuthSession::SetCredentialVerifier(const brillo::SecureBlob& passkey) {
  credential_verifier_.reset(new ScryptVerifier());
  credential_verifier_->Set(passkey);
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
    LOG(ERROR) << "AuthSession: incorrect serialized string size: "
               << serialized_token.size() << ".";
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

bool AuthSession::ConstructAuthInputForChallengeCredentials(
    const cryptohome::AuthorizationRequest& authorization,
    AuthInput& auth_input) {
  // There should only ever have 1 challenge response key in the request
  // and having 0 or more than 1 element is considered invalid.
  if (authorization.key().data().challenge_response_key_size() != 1) {
    return false;
  }
  const ChallengePublicKeyInfo& public_key_info =
      authorization.key().data().challenge_response_key(0);
  auto struct_public_key_info = cryptohome::proto::FromProto(public_key_info);
  auth_input.challenge_credential_auth_input = ChallengeCredentialAuthInput{
      .public_key_spki_der = struct_public_key_info.public_key_spki_der,
      .challenge_signature_algorithms =
          struct_public_key_info.signature_algorithm,
  };
  return true;
}

void AuthSession::PersistAuthFactorToUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)> on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  user_data_auth::AddAuthFactorReply reply;

  // Check the status of the callback error, to see if the key blob creation was
  // actually successful.
  if (!callback_error.ok() || !key_blobs || !auth_block_state) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInPersistToUSS),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlob creation failed before persisting USS";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInPersistToUSS),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(callback_error)));
    return;
  }

  // Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs->DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR) << "Failed to derive credential secret for created auth factor";
    // TODO(b/229834676): Migrate USS and wrap the error.
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionDeriveUSSSecretFailedInPersistToUSS),
            ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                            ErrorAction::kDeleteVault}),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
    return;
  }

  // Create the auth factor by combining the metadata with the auth block state.
  auto auth_factor =
      std::make_unique<AuthFactor>(auth_factor_type, auth_factor_label,
                                   auth_factor_metadata, *auth_block_state);

  // Add the new factor into the USS in-memory.
  // This wraps the USS Main Key with the credential secret. The wrapping_id
  // field is defined equal to the factor's label.
  CryptohomeStatus status = user_secret_stash_->AddWrappedMainKey(
      user_secret_stash_main_key_.value(),
      /*wrapping_id=*/auth_factor->label(), uss_credential_secret.value());
  if (!status.ok()) {
    LOG(ERROR) << "Failed to add created auth factor into user secret stash";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionAddMainKeyFailedInPersistToUSS),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  if (auth_input.reset_secret.has_value() &&
      !user_secret_stash_->SetResetSecretForLabel(
          auth_factor->label(), auth_input.reset_secret.value())) {
    LOG(ERROR) << "Failed to insert reset secret for auth factor";
    // TODO(b/229834676): Migrate USS and wrap the error.
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionAddResetSecretFailedInPersistToUSS),
            ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry}),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
    return;
  }

  // Encrypt the updated USS.
  CryptohomeStatusOr<brillo::Blob> encrypted_uss_container =
      user_secret_stash_->GetEncryptedContainer(
          user_secret_stash_main_key_.value());
  if (!encrypted_uss_container.ok()) {
    LOG(ERROR)
        << "Failed to encrypt user secret stash after auth factor creation";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionEncryptFailedInPersistToUSS),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(encrypted_uss_container).status()));
    return;
  }

  // Persist the factor.
  // It's important to do this after all the non-persistent steps so that we
  // only start writing files after all validity checks (like the label
  // duplication check).
  status =
      auth_factor_manager_->SaveAuthFactor(obfuscated_username_, *auth_factor);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to persist created auth factor";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocAuthSessionPersistFactorFailedInPersistToUSS),
                       user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
                       .Wrap(std::move(status)));
    return;
  }

  // Persist the USS.
  // It's important to do this after persisting the factor, to minimize the
  // chance of ending in an inconsistent state on the disk: a created/updated
  // USS and a missing auth factor (note that we're using file system syncs to
  // have best-effort ordering guarantee).
  status = user_secret_stash_storage_->Persist(encrypted_uss_container.value(),
                                               obfuscated_username_);
  if (!status.ok()) {
    LOG(ERROR)
        << "Failed to persist user secret stash after auth factor creation";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionPersistUSSFailedInPersistToUSS),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  LOG(INFO) << "AuthSession: added auth factor " << auth_factor->label()
            << " into USS.";
  label_to_auth_factor_.emplace(auth_factor->label(), std::move(auth_factor));
  user_has_configured_auth_factor_ = true;

  // Report timer for how long AuthSession operation takes.
  ReportTimerDuration(auth_session_performance_timer.get());

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void AuthSession::AddAuthFactor(
    const user_data_auth::AddAuthFactorRequest& request,
    base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)>
        on_done) {
  // Preconditions:
  DCHECK_EQ(request.auth_session_id(), serialized_token_);
  user_data_auth::AddAuthFactorReply reply;
  // TODO(b/216804305): Verify the auth session is authenticated, after
  // `OnUserCreated()` is changed to mark the session authenticated.
  // At this point AuthSession should be authenticated as it needs
  // FileSystemKeys to wrap the new credentials.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionUnauthedInAddAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  AuthFactorMetadata auth_factor_metadata;
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  if (!GetAuthFactorMetadata(request.auth_factor(), auth_factor_metadata,
                             auth_factor_type, auth_factor_label)) {
    LOG(ERROR) << "Failed to parse new auth factor parameters";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionUnknownFactorInAddAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  std::optional<AuthInput> auth_input =
      CreateAuthInput(request.auth_input(), obfuscated_username_,
                      auth_block_utility_->GetLockedToSingleUser(),
                      /*cryptohome_recovery_ephemeral_pub_key=*/std::nullopt);
  if (!auth_input.has_value()) {
    LOG(ERROR) << "Failed to parse auth input for new auth factor";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionNoInputInAddAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  if (user_secret_stash_) {
    // The user has a UserSecretStash (either because it's a new user and the
    // experiment is on or it's an existing user who went through this flow), so
    // proceed with wrapping the USS via the new factor and persisting both.

    // Report timer for how long AuthenticateAuthFactor operation takes.
    auto auth_session_performance_timer =
        std::make_unique<AuthSessionPerformanceTimer>(
            kAuthSessionAddAuthFactorUSSTimer);
    // Anything backed PinWeaver needs a reset secret. The list of is_le_cred
    // could expand in the future.
    if (NeedsResetSecret(auth_factor_type)) {
      auth_input->reset_secret = std::make_optional<brillo::SecureBlob>(
          CreateSecureRandomBlob(CRYPTOHOME_RESET_SECRET_LENGTH));
    }

    AddAuthFactorViaUserSecretStash(auth_factor_type, auth_factor_label,
                                    auth_factor_metadata, auth_input.value(),
                                    std::move(auth_session_performance_timer),
                                    std::move(on_done));
    return;
  }
  // Report timer for how long AuthenticateAuthFactor operation takes.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAddAuthFactorVKTimer);

  AddAuthFactorViaVaultKeyset(
      auth_factor_type, auth_factor_label, auth_input.value(),
      std::move(auth_session_performance_timer), std::move(on_done));
}

void AuthSession::AddAuthFactorViaVaultKeyset(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)>
        on_done) {
  user_data_auth::AddAuthFactorReply reply;
  KeyData key_data;
  user_data_auth::CryptohomeErrorCode error = converter_->AuthFactorToKeyData(
      auth_factor_label, auth_factor_type, key_data);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionVKConverterFailsInAddAuthFactor),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}), error));
    return;
  }

  // TODO(b/223221875): |authorization| needs to be populated with the
  // challenge_response_key from the request once the Challenge Credential
  // support is added to AuthFactor APIs.
  cryptohome::AuthorizationRequest authorization;
  CreateKeyBlobsToAddKeyset<user_data_auth::AddAuthFactorReply>(
      authorization, auth_input, key_data,
      /*initial_keyset*/ !user_has_configured_credential_,
      std::move(auth_session_performance_timer), std::move(on_done));
}

void AuthSession::AddAuthFactorViaUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)>
        on_done) {
  // Preconditions.
  DCHECK(user_secret_stash_);
  DCHECK(user_secret_stash_main_key_.has_value());

  user_data_auth::AddAuthFactorReply reply;

  // Determine the auth block type to use.
  bool is_le_credential = auth_factor_type == AuthFactorType::kPin;
  bool is_recovery = auth_factor_type == AuthFactorType::kCryptohomeRecovery;
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeForCreation(
          is_le_credential, is_recovery,
          /*is_challenge_credential=*/false,
          AuthFactorStorageType::kUserSecretStash);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInAddViaUSS),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  // Parameterize timer by AuthBlockType.
  auth_session_performance_timer->auth_block_type = auth_block_type;

  // Create the keyset and then add it to the USS after it completes.
  auto create_callback = base::BindOnce(
      &AuthSession::PersistAuthFactorToUserSecretStash,
      weak_factory_.GetWeakPtr(), auth_factor_type, auth_factor_label,
      auth_factor_metadata, auth_input,
      std::move(auth_session_performance_timer), std::move(on_done));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
}

void AuthSession::AuthenticateViaUserSecretStash(
    const std::string& auth_factor_label,
    const AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    const AuthFactor& auth_factor,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done) {
  user_data_auth::AuthenticateAuthFactorReply reply;

  // TODO(b/223207622): This step is the same for both USS and
  // VaultKeyset other than how the AuthBlock state is obtained. Make the
  // derivation for USS asynchronous and merge these two.
  auto key_blobs = std::make_unique<KeyBlobs>();
  AuthBlockType auth_block_type;
  CryptoStatus crypto_error = auth_block_utility_->DeriveKeyBlobs(
      auth_input, auth_factor.auth_block_state(), *key_blobs, auth_block_type);
  if (!crypto_error.ok()) {
    LOG(ERROR) << "Auth factor authentication failed: error " << crypto_error;
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeCryptoError>(
                       CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveFailedInAuthUSS))
                       .Wrap(std::move(crypto_error)));
    return;
  }

  // Parameterize timer by AuthBlockType.
  auth_session_performance_timer->auth_block_type = auth_block_type;

  // Use USS to finish the authentication.
  LoadUSSMainKeyAndFsKeyset(
      auth_factor_label, std::move(auth_session_performance_timer),
      std::move(on_done), OkStatus<CryptohomeCryptoError>(),
      std::move(key_blobs));
}

void AuthSession::LoadUSSMainKeyAndFsKeyset(
    const std::string& auth_factor_label,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs) {
  user_data_auth::AuthenticateAuthFactorReply reply;

  // Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs->DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR)
        << "Failed to derive credential secret for authenticating auth factor";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveUSSSecretFailedInLoadUSS),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return;
  }

  // Load the USS container with the encrypted payload.
  CryptohomeStatusOr<brillo::Blob> encrypted_uss =
      user_secret_stash_storage_->LoadPersisted(obfuscated_username_);
  if (!encrypted_uss.ok()) {
    LOG(ERROR) << "Failed to load the user secret stash";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionLoadUSSFailedInLoadUSS),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED)
            .Wrap(std::move(encrypted_uss).status()));
    return;
  }

  // Decrypt the USS payload.
  // This unwraps the USS Main Key with the credential secret, and decrypts the
  // USS payload using the USS Main Key. The wrapping_id field is defined equal
  // to the factor's label.
  brillo::SecureBlob decrypted_main_key;
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
      user_secret_stash_status =
          UserSecretStash::FromEncryptedContainerWithWrappingKey(
              encrypted_uss.value(), /*wrapping_id=*/auth_factor_label,
              /*wrapping_key=*/uss_credential_secret.value(),
              &decrypted_main_key);
  if (!user_secret_stash_status.ok()) {
    LOG(ERROR) << "Failed to decrypt the user secret stash";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDecryptUSSFailedInLoadUSS),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED)
            .Wrap(std::move(user_secret_stash_status).status()));
    return;
  }
  user_secret_stash_ = std::move(user_secret_stash_status).value();
  user_secret_stash_main_key_ = decrypted_main_key;

  // Populate data fields from the USS.
  file_system_keyset_ = user_secret_stash_->GetFileSystemKeyset();

  // Reset LE Credential counter if the current AutFactor is not an
  // LECredential.
  ResetLECredentials();

  // Flip the status on the successful authentication.
  SetAuthSessionAsAuthenticated();

  ReportTimerDuration(auth_session_performance_timer.get());
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
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
  DCHECK(timeout_timer_.IsRunning());
  auto time_passed = base::TimeTicks::Now() - timeout_timer_start_time_;
  auto time_left = timeout_timer_.GetCurrentDelay() - time_passed;
  return time_left;
}

std::unique_ptr<brillo::SecureBlob> AuthSession::GetHibernateSecret() {
  const FileSystemKeyset& fs_keyset = file_system_keyset();
  const std::string message(kHibernateSecretHmacMessage);

  return std::make_unique<brillo::SecureBlob>(HmacSha256(
      brillo::SecureBlob::Combine(fs_keyset.Key().fnek, fs_keyset.Key().fek),
      brillo::Blob(message.cbegin(), message.cend())));
}

}  // namespace cryptohome
