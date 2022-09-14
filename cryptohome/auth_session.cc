// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/flat_set.h>
#include <base/containers/span.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_input_utils.h"
#include "cryptohome/credential_verifier_factory.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/signature_sealing/structures_proto.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/user_session_map.h"
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

constexpr base::StringPiece IntentToDebugString(AuthIntent intent) {
  switch (intent) {
    case AuthIntent::kDecrypt:
      return "decrypt";
    case AuthIntent::kVerifyOnly:
      return "verify-only";
  }
}

std::string IntentSetToDebugString(const base::flat_set<AuthIntent>& intents) {
  std::vector<base::StringPiece> strings;
  strings.reserve(intents.size());
  for (auto intent : intents) {
    strings.push_back(IntentToDebugString(intent));
  }
  return base::JoinString(strings, ",");
}

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
    AuthIntent intent,
    base::OnceCallback<void(const base::UnguessableToken&)> on_timeout,
    Crypto* crypto,
    Platform* platform,
    UserSessionMap* user_session_map,
    KeysetManagement* keyset_management,
    AuthBlockUtility* auth_block_utility,
    AuthFactorManager* auth_factor_manager,
    UserSecretStashStorage* user_secret_stash_storage,
    bool enable_create_backup_vk_with_uss)
    : username_(username),
      obfuscated_username_(SanitizeUserName(username_)),
      token_(base::UnguessableToken::Create()),
      serialized_token_(
          AuthSession::GetSerializedStringFromToken(token_).value_or("")),
      is_ephemeral_user_(flags & AUTH_SESSION_FLAGS_EPHEMERAL_USER),
      auth_intent_(intent),
      on_timeout_(std::move(on_timeout)),
      crypto_(crypto),
      platform_(platform),
      user_session_map_(user_session_map),
      keyset_management_(keyset_management),
      auth_block_utility_(auth_block_utility),
      auth_factor_manager_(auth_factor_manager),
      user_secret_stash_storage_(user_secret_stash_storage),
      enable_create_backup_vk_with_uss_(enable_create_backup_vk_with_uss) {
  // Preconditions.
  DCHECK(!serialized_token_.empty());
  DCHECK(crypto_);
  DCHECK(platform_);
  DCHECK(user_session_map_);
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
  authorized_intents_.clear();
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
            << is_ephemeral_user_
            << " intent=" << IntentToDebugString(auth_intent_)
            << " user_exists=" << user_exists_
            << " keys=" << base::JoinString(keys, ",") << ".";
}

void AuthSession::SetAuthSessionAsAuthenticated(
    base::span<const AuthIntent> new_authorized_intents) {
  if (new_authorized_intents.empty()) {
    NOTREACHED() << "Empty intent set cannot be authorized";
    return;
  }
  authorized_intents_.insert(new_authorized_intents.begin(),
                             new_authorized_intents.end());
  if (authorized_intents_.contains(AuthIntent::kDecrypt)) {
    status_ = AuthStatus::kAuthStatusAuthenticated;
    // Record time of authentication for metric keeping.
    authenticated_time_ = base::TimeTicks::Now();
  }
  LOG(INFO) << "AuthSession: authorized for "
            << IntentSetToDebugString(authorized_intents_) << ".";
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
  // Since this function is called for a new user, it is safe to put the
  // AuthSession in an authenticated state.
  SetAuthSessionAsAuthenticated(kAllAuthIntents);
  user_exists_ = true;

  if (!is_ephemeral_user_) {
    // Creating file_system_keyset to the prepareVault call next.
    if (!file_system_keyset_.has_value()) {
      file_system_keyset_ = FileSystemKeyset::CreateRandom();
    }
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

void AuthSession::AddVaultKeyset(
    const KeyData& key_data,
    AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
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
    std::move(on_done).Run(
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
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionAddFailedInAddKeyset),
          ErrorActionSet({ErrorAction::kReboot}), error));
      return;
    }
    LOG(INFO) << "AuthSession: added additional keyset " << key_data.label()
              << ".";
  } else {  // AddInitialKeyset
    if (!file_system_keyset_.has_value()) {
      LOG(ERROR) << "AddInitialKeyset: file_system_keyset is invalid.";
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNoFSKeyInAddKeyset),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
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
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionAddInitialFailedInAddKeyset),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
          user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
      return;
    }
    LOG(INFO) << "AuthSession: added initial keyset " << key_data.label()
              << ".";
    vault_keyset_ = std::move(vk_status).value();
  }

  std::unique_ptr<AuthFactor> added_auth_factor =
      converter_->VaultKeysetToAuthFactor(username_, key_data.label());
  std::optional<AuthFactorType> auth_factor_type;
  if (added_auth_factor) {
    auth_factor_type = added_auth_factor->type();
    label_to_auth_factor_.emplace(key_data.label(),
                                  std::move(added_auth_factor));
  } else {
    LOG(WARNING) << "Failed to convert added keyset to AuthFactor.";
  }

  // Flip the flag, so that our future invocations go through AddKeyset() and
  // not AddInitialKeyset(). Create a verifier if applicable.
  if (!user_has_configured_credential_ && auth_input.user_input.has_value()) {
    // TODO(emaxx): This is overly permissive by creating the verifier even when
    // AuthFactor conversion failed and the actual factor type is unknown; we
    // should eventually forbid proceeding with a `nullopt` type here.
    SetCredentialVerifier(auth_factor_type, key_data.label(),
                          auth_input.user_input.value());
  }
  user_has_configured_credential_ = true;

  // Report timer for how long AuthSession operation takes.
  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::CreateKeyBlobsToAddKeyset(
    const AuthInput& auth_input,
    const KeyData& key_data,
    bool initial_keyset,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done) {
  AuthBlockType auth_block_type;
  bool is_le_credential = key_data.policy().low_entropy_credential();
  bool is_challenge_credential =
      key_data.type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE;

  // Generate KeyBlobs and AuthBlockState used for VaultKeyset encryption.
  auth_block_type = auth_block_utility_->GetAuthBlockTypeForCreation(
      is_le_credential, /*is_recovery=*/false, is_challenge_credential);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
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
  if (auth_block_type == AuthBlockType::kPinWeaver) {
    if (initial_keyset) {
      // The initial keyset cannot be a PIN, when using vault keysets.
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionPinweaverUnsupportedInAddKeyset),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
      return;
    }
    // Since this is not the initial keyset, there should now be a valid
    // authenticated VaultKeyset.
    DCHECK(vault_keyset_);
  }

  auto create_callback = base::BindOnce(
      &AuthSession::AddVaultKeyset, weak_factory_.GetWeakPtr(), key_data,
      auth_input, std::move(auth_session_performance_timer),
      std::move(on_done));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
}

void AuthSession::AddCredentials(
    const user_data_auth::AddCredentialsRequest& request,
    StatusCallback on_done) {
  CHECK(request.authorization().key().has_data());
  MountStatusOr<std::unique_ptr<Credentials>> credentials_or_err =
      GetCredentials(request.authorization());
  if (!credentials_or_err.ok()) {
    std::move(on_done).Run(
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
      std::move(on_done).Run(MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionKioskKeyNotAllowedInAddCred),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          MOUNT_ERROR_UNPRIVILEGED_KEY));
      return;
    }

    // At this point we have to have keyset since we have to be authed.
    if (!vault_keyset_) {
      LOG(ERROR)
          << "Add Credentials: tried adding credential before authenticating";
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNotAuthedYetInAddCred),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
    }
  } else if (is_ephemeral_user_) {
    // If AuthSession is configured as an ephemeral user, then we do not save
    // the key to the disk.
    std::move(on_done).Run(OkStatus<CryptohomeError>());
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
  AuthInput auth_input = {
      .user_input = credentials->passkey(),
      .locked_to_single_user = std::nullopt,
      .obfuscated_username = obfuscated_username_,
      .reset_secret = std::nullopt,
      .reset_seed = std::nullopt,
      .cryptohome_recovery_auth_input = std::nullopt,
      .challenge_credential_auth_input =
          CreateChallengeCredentialAuthInput(request.authorization())};
  if (user_has_configured_credential_) {
    auth_input.reset_seed = vault_keyset_->GetResetSeed();
  }

  bool is_initial_keyset = !user_has_configured_credential_;

  CreateKeyBlobsToAddKeyset(
      auth_input, credentials->key_data(), is_initial_keyset,
      std::move(auth_session_performance_timer), std::move(on_done));
}

void AuthSession::UpdateCredential(
    const user_data_auth::UpdateCredentialRequest& request,
    StatusCallback on_done) {
  CHECK(request.authorization().key().has_data());
  MountStatusOr<std::unique_ptr<Credentials>> credentials_or_err =
      GetCredentials(request.authorization());
  if (!credentials_or_err.ok()) {
    std::move(on_done).Run(
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
    std::move(on_done).Run(MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnsupportedKioskKeyInUpdate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        MOUNT_ERROR_UNPRIVILEGED_KEY));
    return;
  }

  // To update a key, we need to ensure that the existing label and the new
  // label match.
  if (credentials->key_data().label() != request.old_credential_label()) {
    LOG(ERROR) << "AuthorizationRequest does not have a matching label";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionLabelMismatchInUpdate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // At this point we have to have keyset since we have to be authed.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnauthedInUpdate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  CreateKeyBlobsToUpdateKeyset(*credentials.get(), std::move(on_done));
}

void AuthSession::CreateKeyBlobsToUpdateKeyset(const Credentials& credentials,
                                               StatusCallback on_done) {
  bool is_le_credential =
      credentials.key_data().policy().low_entropy_credential();
  bool is_challenge_credential =
      credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE;

  AuthBlockType auth_block_type;
  auth_block_type = auth_block_utility_->GetAuthBlockTypeForCreation(
      is_le_credential, /*is_recovery=*/false, is_challenge_credential);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
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
  AuthInput auth_input = {.user_input = credentials.passkey(),
                          .locked_to_single_user = std::nullopt,
                          .obfuscated_username = obfuscated_username_,
                          .reset_secret = std::nullopt,
                          .reset_seed = std::nullopt,
                          .cryptohome_recovery_auth_input = std::nullopt,
                          .challenge_credential_auth_input = std::nullopt};

  if (vault_keyset_ && vault_keyset_->HasWrappedResetSeed()) {
    auth_input.reset_seed = vault_keyset_->GetResetSeed();
  }

  AuthBlock::CreateCallback create_callback = base::BindOnce(
      &AuthSession::UpdateVaultKeyset, weak_factory_.GetWeakPtr(),
      /*request_auth_factor_type=*/std::nullopt, credentials.key_data(),
      auth_input, std::move(auth_session_performance_timer),
      std::move(on_done));
  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
}

void AuthSession::UpdateVaultKeyset(
    std::optional<AuthFactorType> auth_factor_type,
    const KeyData& key_data,
    AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
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
    std::move(on_done).Run(
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
  if (error_code != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUpdateWithBlobFailedInUpdateKeyset),
        ErrorActionSet(
            {ErrorAction::kReboot, ErrorAction::kDevCheckUnexpectedState}),
        error_code));
    return;
  }

  // Add the new secret to the AuthSession's credential verifier. On successful
  // completion of the UpdateAuthFactor this will be passed to UserSession's
  // credential verifier to cache the secret for future lightweight
  // verifications.
  if (auth_input.user_input.has_value()) {
    SetCredentialVerifier(auth_factor_type, key_data.label(),
                          auth_input.user_input.value());
  }

  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

bool AuthSession::AuthenticateViaVaultKeyset(
    std::optional<AuthFactorType> request_auth_factor_type,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done) {
  AuthBlockState auth_state;
  if (!auth_block_utility_->GetAuthBlockStateFromVaultKeyset(
          key_data_.label(), obfuscated_username_, auth_state /*Out*/)) {
    LOG(ERROR) << "Error in obtaining AuthBlock state for key derivation.";
    std::move(on_done).Run(MakeStatus<error::CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionBlockStateMissingInAuthViaVaultKey),
        ErrorActionSet({error::ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return false;
  }

  // Determine the auth block type to use.
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeFromState(auth_state);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR) << "Failed to determine auth block type from auth block state";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInAuthViaVaultKey),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return false;
  }

  // Parameterize the AuthSession performance timer by AuthBlockType
  auth_session_performance_timer->auth_block_type = auth_block_type;

  // Derive KeyBlobs from the existing VaultKeyset, using GetValidKeyset
  // as a callback that loads |vault_keyset_| and resaves if needed.
  AuthBlock::DeriveCallback derive_callback = base::BindOnce(
      &AuthSession::LoadVaultKeysetAndFsKeys, weak_factory_.GetWeakPtr(),
      request_auth_factor_type, auth_input.user_input, auth_block_type,
      std::move(auth_session_performance_timer), std::move(on_done));

  return auth_block_utility_->DeriveKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, auth_state, std::move(derive_callback));
}

void AuthSession::LoadVaultKeysetAndFsKeys(
    std::optional<AuthFactorType> request_auth_factor_type,
    const std::optional<brillo::SecureBlob> passkey,
    const AuthBlockType& auth_block_type,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptoStatus status,
    std::unique_ptr<KeyBlobs> key_blobs) {
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
    std::move(on_done).Run(
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
    std::move(on_done).Run(
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
  SetAuthSessionAsAuthenticated(kAllAuthIntents);

  // Set the credential verifier for this credential.
  if (passkey.has_value()) {
    SetCredentialVerifier(request_auth_factor_type, key_data_.label(),
                          passkey.value());
  }

  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<error::CryptohomeError>());
}

void AuthSession::Authenticate(
    const cryptohome::AuthorizationRequest& authorization_request,
    StatusCallback on_done) {
  LOG(INFO) << "AuthSession: authentication attempt via "
            << authorization_request.key().data().label() << ".";

  MountStatusOr<std::unique_ptr<Credentials>> credentials_or_err =
      GetCredentials(authorization_request);

  if (!credentials_or_err.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionGetCredFailedInAuth))
            .Wrap(std::move(credentials_or_err).status()));
    return;
  }

  if (authorization_request.key().data().type() != KeyData::KEY_TYPE_PASSWORD &&
      authorization_request.key().data().type() != KeyData::KEY_TYPE_KIOSK &&
      authorization_request.key().data().type() !=
          KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    // AuthSession::Authenticate is only supported for three types of cases
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnsupportedKeyTypesInAuth),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
    return;
  }

  std::unique_ptr<Credentials> credentials =
      std::move(credentials_or_err).value();

  if (credentials->key_data().label().empty()) {
    LOG(ERROR) << "Authenticate: Credentials key_data.label() is empty.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionEmptyKeyLabelInAuth),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
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
    SetCredentialVerifier(/*auth_factor_type=*/std::nullopt, key_data_.label(),
                          credentials->passkey());

    // SetAuthSessionAsAuthenticated() should already have been called
    // in the constructor by this point.
    std::move(on_done).Run(OkStatus<CryptohomeError>());
    return;
  }
  // Persistent mount.
  // A persistent mount will always have a persistent key on disk. Here
  // keyset_management tries to fetch that persistent credential.
  // TODO(dlunev): fix conditional error when we switch to StatusOr.
  AuthInput auth_input = {
      .user_input = credentials->passkey(),
      .locked_to_single_user = auth_block_utility_->GetLockedToSingleUser(),
      .obfuscated_username = std::nullopt,
      .reset_secret = std::nullopt,
      .reset_seed = std::nullopt,
      .cryptohome_recovery_auth_input = std::nullopt,
      .challenge_credential_auth_input =
          CreateChallengeCredentialAuthInput(authorization_request)};

  AuthenticateViaVaultKeyset(
      /*request_auth_factor_type=*/std::nullopt, auth_input,
      std::move(auth_session_performance_timer), std::move(on_done));
}

const FileSystemKeyset& AuthSession::file_system_keyset() const {
  DCHECK(file_system_keyset_.has_value());
  return file_system_keyset_.value();
}

bool AuthSession::AuthenticateAuthFactor(
    const user_data_auth::AuthenticateAuthFactorRequest& request,
    StatusCallback on_done) {
  LOG(INFO) << "AuthSession: " << IntentToDebugString(auth_intent_)
            << " authentication attempt via " << request.auth_factor_label()
            << " factor.";
  // Check the factor exists either with USS or VaultKeyset.
  auto label_to_auth_factor_iter =
      label_to_auth_factor_.find(request.auth_factor_label());

  {
    std::optional<AuthFactorType> auth_factor_type =
        DetermineFactorTypeFromAuthInput(request.auth_input());
    if (!auth_factor_type.has_value()) {
      LOG(ERROR) << "Unexpected AuthInput type.";
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNoAuthFactorTypeInAuthAuthFactor),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return false;
    }

    // Ensure that if a label is supplied, the requested type matches what we
    // have on disk for the user.
    // Note that if we cannot find a stored AuthFactor then this test is
    // skipped. This can happen in the case of ephemeral users now, later with
    // legacy fingerprint check for verification intent.
    if (label_to_auth_factor_iter != label_to_auth_factor_.end() &&
        auth_factor_type != label_to_auth_factor_iter->second->type()) {
      LOG(ERROR) << "Unexpected mismatch in type from label and auth_input.";
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionMismatchedAuthTypes),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return false;
    }
    // If suitable, attempt lightweight authentication via a credential
    // verifier.
    if (auth_intent_ == AuthIntent::kVerifyOnly &&
        IsCredentialVerifierSupported(auth_factor_type.value()) &&
        AuthenticateViaCredentialVerifier(request.auth_input())) {
      const AuthIntent lightweight_intents[] = {AuthIntent::kVerifyOnly};
      SetAuthSessionAsAuthenticated(lightweight_intents);
      std::move(on_done).Run(OkStatus<CryptohomeError>());
      return true;
    }
  }

  if (label_to_auth_factor_iter == label_to_auth_factor_.end()) {
    LOG(ERROR) << "Authentication key not found: "
               << request.auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInAuthAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return false;
  }
  const AuthFactor& auth_factor = *label_to_auth_factor_iter->second;

  // Fill up the auth input.
  CryptohomeStatusOr<AuthInput> auth_input_status =
      CreateAuthInputForAuthentication(request.auth_input(),
                                       auth_factor.metadata());
  if (!auth_input_status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionInputParseFailedInAuthAuthFactor))
            .Wrap(std::move(auth_input_status).status()));
    return false;
  }
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  // If the user has configured AuthFactors, then we proceed with USS flow.
  if (user_has_configured_auth_factor_) {
    // Record current time for timing for how long AuthenticateAuthFactor will
    // take.
    auto auth_session_performance_timer =
        std::make_unique<AuthSessionPerformanceTimer>(
            kAuthSessionAuthenticateAuthFactorUSSTimer);

    AuthenticateViaUserSecretStash(request.auth_factor_label(),
                                   auth_input_status.value(),
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
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionVKConverterFailedInAuthAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}), error));
    return false;
  }
  // Record current time for timing for how long AuthenticateAuthFactor will
  // take.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAuthenticateAuthFactorVKTimer);

  return AuthenticateViaVaultKeyset(
      auth_factor.type(), auth_input_status.value(),
      std::move(auth_session_performance_timer), std::move(on_done));
}

void AuthSession::RemoveAuthFactor(
    const user_data_auth::RemoveAuthFactorRequest& request,
    StatusCallback on_done) {
  user_data_auth::RemoveAuthFactorReply reply;

  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnauthedInRemoveAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  if (user_secret_stash_) {
    // TODO(b/236869367): Wrap the error when it is not a OKStatus.
    std::move(on_done).Run(
        RemoveAuthFactorViaUserSecretStash(request.auth_factor_label()));
    return;
  }

  // TODO(b/236869367): Implement for VaultKeyset users.
  std::move(on_done).Run(MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(
          kLocAuthSessionVaultKeysetNotImplementedInRemoveAuthFactor),
      ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
      CryptoError::CE_OTHER_CRYPTO,
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
}

CryptohomeStatus AuthSession::RemoveAuthFactorViaUserSecretStash(
    const std::string& auth_factor_label) {
  // Preconditions.
  DCHECK(user_secret_stash_);
  DCHECK(user_secret_stash_main_key_.has_value());

  user_data_auth::RemoveAuthFactorReply reply;

  auto label_to_auth_factor_iter =
      label_to_auth_factor_.find(auth_factor_label);
  if (label_to_auth_factor_iter == label_to_auth_factor_.end()) {
    LOG(ERROR) << "AuthSession: Key to remove not found: " << auth_factor_label;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInRemoveAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }

  if (label_to_auth_factor_.size() == 1) {
    LOG(ERROR) << "AuthSession: Cannot remove the last auth factor.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionLastFactorInRemoveAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_REMOVE_CREDENTIALS_FAILED);
  }

  AuthFactor auth_factor = *label_to_auth_factor_iter->second;
  CryptohomeStatus status = auth_factor_manager_->RemoveAuthFactor(
      obfuscated_username_, auth_factor, auth_block_utility_);
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to remove auth factor.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionRemoveFactorFailedInRemoveAuthFactor),
               user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
        .Wrap(std::move(status));
  }

  // Remove the auth factor from the map.
  label_to_auth_factor_.erase(label_to_auth_factor_iter);

  status = RemoveAuthFactorFromUssInMemory(auth_factor_label);
  if (!status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionRemoveFromUssFailedInRemoveAuthFactor),
               user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
        .Wrap(std::move(status));
  }

  CryptohomeStatusOr<brillo::Blob> encrypted_uss_container =
      user_secret_stash_->GetEncryptedContainer(
          user_secret_stash_main_key_.value());
  if (!encrypted_uss_container.ok()) {
    LOG(ERROR) << "AuthSession: Failed to encrypt user secret stash after auth "
                  "factor removal.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionEncryptFailedInRemoveAuthFactor),
               user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
        .Wrap(std::move(encrypted_uss_container).status());
  }
  status = user_secret_stash_storage_->Persist(encrypted_uss_container.value(),
                                               obfuscated_username_);
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to persist user secret stash after auth "
                  "factor removal.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionPersistUSSFailedInRemoveAuthFactor),
               user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
        .Wrap(std::move(status));
  }

  return OkStatus<CryptohomeError>();
}

CryptohomeStatus AuthSession::RemoveAuthFactorFromUssInMemory(
    const std::string& auth_factor_label) {
  if (!user_secret_stash_->RemoveWrappedMainKey(
          /*wrapping_id=*/auth_factor_label)) {
    LOG(ERROR)
        << "AuthSession: Failed to remove auth factor from user secret stash.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionRemoveMainKeyFailedInRemoveSecretFromUss),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED);
  }

  // Note: we may or may not have a reset secret for this auth factor -
  // therefore we don't check the return value.
  user_secret_stash_->RemoveResetSecretForLabel(auth_factor_label);

  return OkStatus<CryptohomeError>();
}

void AuthSession::UpdateAuthFactor(
    const user_data_auth::UpdateAuthFactorRequest& request,
    StatusCallback on_done) {
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnauthedInUpdateAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  if (request.auth_factor_label().empty()) {
    LOG(ERROR) << "AuthSession: Old auth factor label is empty.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoOldLabelInUpdateAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  auto label_to_auth_factor_iter =
      label_to_auth_factor_.find(request.auth_factor_label());
  if (label_to_auth_factor_iter == label_to_auth_factor_.end()) {
    LOG(ERROR) << "AuthSession: Key to update not found: "
               << request.auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInUpdateAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  AuthFactorMetadata auth_factor_metadata;
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  if (!GetAuthFactorMetadata(request.auth_factor(), auth_factor_metadata,
                             auth_factor_type, auth_factor_label)) {
    LOG(ERROR)
        << "AuthSession: Failed to parse updated auth factor parameters.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnknownFactorInUpdateAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Auth factor label has to be the same as before.
  if (request.auth_factor_label() != auth_factor_label) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDifferentLabelInUpdateAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Auth factor type has to be the same as before.
  AuthFactor* existing_auth_factor = label_to_auth_factor_iter->second.get();
  if (existing_auth_factor->type() != auth_factor_type) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDifferentTypeInUpdateAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  bool is_le_credential = auth_factor_type == AuthFactorType::kPin;
  bool is_recovery = auth_factor_type == AuthFactorType::kCryptohomeRecovery;
  // Determine the auth block type to use.
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeForCreation(
          is_le_credential, is_recovery,
          /*is_challenge_credential=*/false);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR) << "AuthSession: Error in obtaining AuthBlockType in auth "
                  "factor update.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInUpdateAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  // Create and initialize fields for auth_input.
  CryptohomeStatusOr<AuthInput> auth_input_status = CreateAuthInputForAdding(
      request.auth_input(), auth_factor_type, auth_factor_metadata);
  if (!auth_input_status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionNoInputInUpdateAuthFactor))
            .Wrap(std::move(auth_input_status).status()));
    return;
  }

  if (user_secret_stash_) {
    DCHECK(user_has_configured_auth_factor_);
    auto create_callback = base::BindOnce(
        &AuthSession::UpdateAuthFactorViaUserSecretStash,
        weak_factory_.GetWeakPtr(), auth_factor_type, auth_factor_label,
        auth_factor_metadata, auth_input_status.value(), std::move(on_done));
    auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
        auth_block_type, auth_input_status.value(), std::move(create_callback));
    return;
  }

  return UpdateAuthFactorViaVaultKeyset(
      auth_block_type, auth_factor_type, auth_factor_label,
      auth_input_status.value(), std::move(on_done));
}

void AuthSession::UpdateAuthFactorViaVaultKeyset(
    AuthBlockType auth_block_type,
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    StatusCallback on_done) {
  if (auth_block_type == AuthBlockType::kCryptohomeRecovery) {
    LOG(ERROR) << "Recovery is not enabled for VaultKeysets.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionInvalidBlockTypeInUpdateAuthFactorViaVK),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Report timer for how long UpdateAuthFactor operation takes with VK and
  // record current time for timing for how long UpdateAuthFactor will take.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionUpdateAuthFactorVKTimer, auth_block_type);

  KeyData key_data;
  // AuthFactorMetadata is needed for only smartcards. Since
  // UpdateAuthFactor doesn't operate on smartcards pass an empty metadata,
  // which is not going to be used.
  AuthFactorMetadata auth_factor_metadata;
  user_data_auth::CryptohomeErrorCode error = converter_->AuthFactorToKeyData(
      auth_factor_label, auth_factor_type, auth_factor_metadata, key_data);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionConverterFailsInUpdateFactorViaVK),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}), error));
    return;
  }

  AuthBlock::CreateCallback create_callback = base::BindOnce(
      &AuthSession::UpdateVaultKeyset, weak_factory_.GetWeakPtr(),
      auth_factor_type, key_data, auth_input,
      std::move(auth_session_performance_timer), std::move(on_done));

  auth_block_utility_->CreateKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, std::move(create_callback));
}

void AuthSession::UpdateAuthFactorViaUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    StatusCallback on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  user_data_auth::UpdateAuthFactorReply reply;

  // Check the status of the callback error, to see if the key blob creation was
  // actually successful.
  if (!callback_error.ok() || !key_blobs || !auth_block_state) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInUpdateViaUSS),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlob creation failed before updating auth factor";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInUpdateViaUSS),
            user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
            .Wrap(std::move(callback_error)));
    return;
  }

  // Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs->DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR) << "AuthSession: Failed to derive credential secret for "
                  "updated auth factor.";
    // TODO(b/229834676): Migrate USS and wrap the error.
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveUSSSecretFailedInUpdateViaUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED));
    return;
  }

  // Create the auth factor by combining the metadata with the auth block
  // state.
  auto auth_factor =
      std::make_unique<AuthFactor>(auth_factor_type, auth_factor_label,
                                   auth_factor_metadata, *auth_block_state);

  CryptohomeStatus status = RemoveAuthFactorFromUssInMemory(auth_factor_label);
  if (!status.ok()) {
    LOG(ERROR)
        << "AuthSession: Failed to remove old auth factor secret from USS.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionRemoveFromUSSFailedInUpdateViaUSS),
            user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  status = AddAuthFactorToUssInMemory(*auth_factor, auth_input,
                                      uss_credential_secret.value());
  if (!status.ok()) {
    LOG(ERROR)
        << "AuthSession: Failed to add updated auth factor secret to USS.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionAddToUSSFailedInUpdateViaUSS),
            user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  // Encrypt the updated USS.
  CryptohomeStatusOr<brillo::Blob> encrypted_uss_container =
      user_secret_stash_->GetEncryptedContainer(
          user_secret_stash_main_key_.value());
  if (!encrypted_uss_container.ok()) {
    LOG(ERROR) << "AuthSession: Failed to encrypt user secret stash for auth "
                  "factor update.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionEncryptFailedInUpdateViaUSS),
            user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
            .Wrap(std::move(encrypted_uss_container).status()));
    return;
  }

  // Update/persist the factor.
  status = auth_factor_manager_->UpdateAuthFactor(
      obfuscated_username_, auth_factor_label, *auth_factor,
      auth_block_utility_);
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to update auth factor.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionPersistFactorFailedInUpdateViaUSS),
            user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
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
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionPersistUSSFailedInUpdateViaUSS),
            user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  // Create the credential verifier if applicable.
  if (auth_input.user_input.has_value()) {
    SetCredentialVerifier(auth_factor_type, auth_factor->label(),
                          auth_input.user_input.value());
  }

  LOG(INFO) << "AuthSession: updated auth factor " << auth_factor->label()
            << " in USS.";
  label_to_auth_factor_[auth_factor->label()] = std::move(auth_factor);
  std::move(on_done).Run(OkStatus<CryptohomeError>());
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
      crypto_->GetRecoveryCrypto(), &recovery_request, &ephemeral_pub_key);
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
          /*is_challenge_credential*/ false);
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
  AuthInput auth_input = {.user_input = user_input,
                          .locked_to_single_user = std::nullopt,
                          .obfuscated_username = obfuscated_username_,
                          .reset_secret = std::nullopt,
                          .reset_seed = std::nullopt,
                          .cryptohome_recovery_auth_input = std::nullopt,
                          .challenge_credential_auth_input = std::nullopt};

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

CryptohomeStatusOr<AuthInput> AuthSession::CreateAuthInputForAuthentication(
    const user_data_auth::AuthInput& auth_input_proto,
    const AuthFactorMetadata& auth_factor_metadata) {
  std::optional<AuthInput> auth_input = CreateAuthInput(
      platform_, auth_input_proto, username_, obfuscated_username_,
      auth_block_utility_->GetLockedToSingleUser(),
      cryptohome_recovery_ephemeral_pub_key_, auth_factor_metadata);
  if (!auth_input.has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocCreateFailedInAuthInputForAuth),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  return std::move(auth_input.value());
}

CryptohomeStatusOr<AuthInput> AuthSession::CreateAuthInputForAdding(
    const user_data_auth::AuthInput& auth_input_proto,
    AuthFactorType auth_factor_type,
    const AuthFactorMetadata& auth_factor_metadata) {
  std::optional<AuthInput> auth_input = CreateAuthInput(
      platform_, auth_input_proto, username_, obfuscated_username_,
      auth_block_utility_->GetLockedToSingleUser(),
      cryptohome_recovery_ephemeral_pub_key_, auth_factor_metadata);
  if (!auth_input.has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocCreateFailedInAuthInputForAdd),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  if (!NeedsResetSecret(auth_factor_type)) {
    // The factor is not resettable, so no extra data needed to be filled.
    return std::move(auth_input.value());
  }

  if (user_secret_stash_) {
    // When using USS, every resettable factor gets a unique reset secret.
    auth_input->reset_secret =
        CreateSecureRandomBlob(CRYPTOHOME_RESET_SECRET_LENGTH);
    return std::move(auth_input.value());
  }

  // When using VaultKeyset, reset is implemented via a seed that's shared
  // among all of the user's VKs. Hence copy it from the previously loaded VK.
  if (!vault_keyset_) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocNoVkInAuthInputForAdd),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (!vault_keyset_->HasWrappedResetSeed()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocNoWrappedSeedInAuthInputForAdd),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (vault_keyset_->GetResetSeed().empty()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocEmptySeedInAuthInputForAdd),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  auth_input->reset_seed = vault_keyset_->GetResetSeed();
  return std::move(auth_input.value());
}

void AuthSession::SetCredentialVerifier(
    std::optional<AuthFactorType> auth_factor_type,
    const std::string& auth_factor_label,
    const brillo::SecureBlob& passkey) {
  // Note that we always overwrite the old verifier, even when the creation of
  // the new one failed - to avoid any risk of accepting old credentials.
  credential_verifier_ =
      CreateCredentialVerifier(auth_factor_type, auth_factor_label, passkey);
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

std::optional<ChallengeCredentialAuthInput>
AuthSession::CreateChallengeCredentialAuthInput(
    const cryptohome::AuthorizationRequest& authorization) {
  // There should only ever have 1 challenge response key in the request
  // and having 0 or more than 1 element is considered invalid.
  if (authorization.key().data().challenge_response_key_size() != 1) {
    return std::nullopt;
  }
  const ChallengePublicKeyInfo& public_key_info =
      authorization.key().data().challenge_response_key(0);
  auto struct_public_key_info = cryptohome::proto::FromProto(public_key_info);
  return ChallengeCredentialAuthInput{
      .public_key_spki_der = struct_public_key_info.public_key_spki_der,
      .challenge_signature_algorithms =
          struct_public_key_info.signature_algorithm,
  };
}

void AuthSession::PersistAuthFactorToUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
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
    std::move(on_done).Run(
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
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveUSSSecretFailedInPersistToUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
    return;
  }

  // Create the auth factor by combining the metadata with the auth block state.
  auto auth_factor =
      std::make_unique<AuthFactor>(auth_factor_type, auth_factor_label,
                                   auth_factor_metadata, *auth_block_state);

  CryptohomeStatus status = AddAuthFactorToUssInMemory(
      *auth_factor, auth_input, uss_credential_secret.value());
  if (!status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionAddToUssFailedInPersistToUSS),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  // Encrypt the updated USS.
  CryptohomeStatusOr<brillo::Blob> encrypted_uss_container =
      user_secret_stash_->GetEncryptedContainer(
          user_secret_stash_main_key_.value());
  if (!encrypted_uss_container.ok()) {
    LOG(ERROR)
        << "Failed to encrypt user secret stash after auth factor creation";
    std::move(on_done).Run(
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
    std::move(on_done).Run(
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
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionPersistUSSFailedInPersistToUSS),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  // Create the credential verifier if applicable.
  if (!user_has_configured_auth_factor_ && auth_input.user_input.has_value()) {
    SetCredentialVerifier(auth_factor_type, auth_factor->label(),
                          auth_input.user_input.value());
  }

  LOG(INFO) << "AuthSession: added auth factor " << auth_factor->label()
            << " into USS.";
  label_to_auth_factor_.emplace(auth_factor->label(), std::move(auth_factor));
  user_has_configured_auth_factor_ = true;

  // Report timer for how long AuthSession operation takes.
  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

CryptohomeStatus AuthSession::AddAuthFactorToUssInMemory(
    AuthFactor& auth_factor,
    const AuthInput& auth_input,
    const brillo::SecureBlob& uss_credential_secret) {
  // This wraps the USS Main Key with the credential secret. The wrapping_id
  // field is defined equal to the factor's label.
  CryptohomeStatus status = user_secret_stash_->AddWrappedMainKey(
      user_secret_stash_main_key_.value(),
      /*wrapping_id=*/auth_factor.label(), uss_credential_secret);
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to add created auth factor into user "
                  "secret stash.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionAddMainKeyFailedInAddSecretToUSS),
               user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
        .Wrap(std::move(status));
  }

  if (auth_input.reset_secret.has_value() &&
      !user_secret_stash_->SetResetSecretForLabel(
          auth_factor.label(), auth_input.reset_secret.value())) {
    LOG(ERROR) << "AuthSession: Failed to insert reset secret for auth factor.";
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionAddResetSecretFailedInAddSecretToUSS),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  }

  return OkStatus<CryptohomeError>();
}

void AuthSession::AddAuthFactor(
    const user_data_auth::AddAuthFactorRequest& request,
    StatusCallback on_done) {
  // Preconditions:
  DCHECK_EQ(request.auth_session_id(), serialized_token_);
  // TODO(b/216804305): Verify the auth session is authenticated, after
  // `OnUserCreated()` is changed to mark the session authenticated.
  // At this point AuthSession should be authenticated as it needs
  // FileSystemKeys to wrap the new credentials.
  if (status_ != AuthStatus::kAuthStatusAuthenticated) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
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
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnknownFactorInAddAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  CryptohomeStatusOr<AuthInput> auth_input_status = CreateAuthInputForAdding(
      request.auth_input(), auth_factor_type, auth_factor_metadata);
  if (!auth_input_status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionNoInputInAddAuthFactor))
            .Wrap(std::move(auth_input_status).status()));
    return;
  }

  if (is_ephemeral_user_) {
    // If AuthSession is configured as an ephemeral user, then we do not save
    // the key to the disk.
    AddAuthFactorForEphemeral(auth_factor_type, auth_factor_label,
                              auth_input_status.value(), std::move(on_done));
    return;
  }

  if (user_secret_stash_) {
    // The user has a UserSecretStash (either because it's a new user and the
    // experiment is on or it's an existing user who went through this flow), so
    // proceed with wrapping the USS via the new factor and persisting both.

    // Report timer for how long AddAuthFactor operation takes.
    auto auth_session_performance_timer =
        std::make_unique<AuthSessionPerformanceTimer>(
            kAuthSessionAddAuthFactorUSSTimer);

    AddAuthFactorViaUserSecretStash(
        auth_factor_type, auth_factor_label, auth_factor_metadata,
        auth_input_status.value(), std::move(auth_session_performance_timer),
        std::move(on_done));
    return;
  }

  // Report timer for how long AddAuthFactor operation takes.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAddAuthFactorVKTimer);

  AddAuthFactorViaVaultKeyset(auth_factor_type, auth_factor_label,
                              auth_factor_metadata, auth_input_status.value(),
                              std::move(auth_session_performance_timer),
                              std::move(on_done));
}

void AuthSession::AddAuthFactorViaVaultKeyset(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done) {
  KeyData key_data;
  user_data_auth::CryptohomeErrorCode error = converter_->AuthFactorToKeyData(
      auth_factor_label, auth_factor_type, auth_factor_metadata, key_data);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionVKConverterFailsInAddAuthFactor),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}), error));
    return;
  }

  CreateKeyBlobsToAddKeyset(auth_input, key_data,
                            /*initial_keyset*/ !user_has_configured_credential_,
                            std::move(auth_session_performance_timer),
                            std::move(on_done));
}

void AuthSession::AddAuthFactorViaUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done) {
  // Preconditions.
  DCHECK(user_secret_stash_);
  DCHECK(user_secret_stash_main_key_.has_value());

  // Determine the auth block type to use.
  bool is_le_credential = auth_factor_type == AuthFactorType::kPin;
  bool is_recovery = auth_factor_type == AuthFactorType::kCryptohomeRecovery;
  bool is_challenge_credential = auth_factor_type == AuthFactorType::kSmartCard;
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeForCreation(
          is_le_credential, is_recovery, is_challenge_credential);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
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

void AuthSession::AddAuthFactorForEphemeral(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    StatusCallback on_done) {
  DCHECK(is_ephemeral_user_);

  if (!auth_input.user_input.has_value()) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocNoUserInputInAddFactorForEphemeral),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  if (credential_verifier_) {
    // Adding more than one factor is not supported for ephemeral users at the
    // moment.
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVerifierAlreadySetInAddFactorForEphemeral),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  SetCredentialVerifier(auth_factor_type, auth_factor_label,
                        auth_input.user_input.value());
  // Check whether the verifier creation failed.
  if (!credential_verifier_) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVerifierSettingErrorInAddFactorForEphemeral),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

bool AuthSession::AuthenticateViaCredentialVerifier(
    const user_data_auth::AuthInput& auth_input) {
  const UserSession* user_session = user_session_map_->Find(username_);
  if (!user_session) {
    return false;
  }
  // TODO(b/244754956): Capture metadata in the verifier and use it here.
  CryptohomeStatusOr<AuthInput> auth_input_status =
      CreateAuthInputForAuthentication(auth_input, AuthFactorMetadata());
  if (!auth_input_status.ok()) {
    return false;
  }
  // Attempt to verify the auth input against the verifier attached to the
  // user's session.
  // TODO(b/240596931): Switch the verifier to using `AuthInput`.
  if (!auth_input_status.value().user_input.has_value()) {
    return false;
  }
  Credentials credentials(username_,
                          auth_input_status.value().user_input.value());
  return user_session->VerifyCredentials(credentials);
}

void AuthSession::AuthenticateViaUserSecretStash(
    const std::string& auth_factor_label,
    const AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    const AuthFactor& auth_factor,
    StatusCallback on_done) {
  // Determine the auth block type to use.
  // TODO(b/223207622): This step is the same for both USS and VaultKeyset other
  // than how the AuthBlock state is obtained, they can be merged.
  AuthBlockType auth_block_type =
      auth_block_utility_->GetAuthBlockTypeFromState(
          auth_factor.auth_block_state());
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR) << "Failed to determine auth block type for the loaded factor "
                  "with label "
               << auth_factor.label();
    std::move(on_done).Run(MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInAuthViaUSS),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO));
    return;
  }

  // Parameterize timer by AuthBlockType.
  auth_session_performance_timer->auth_block_type = auth_block_type;

  // Derive the keyset and then use USS to complete the authentication.
  auto derive_callback = base::BindOnce(
      &AuthSession::LoadUSSMainKeyAndFsKeyset, weak_factory_.GetWeakPtr(),
      auth_factor.type(), auth_factor_label, auth_input,
      std::move(auth_session_performance_timer), std::move(on_done));
  auth_block_utility_->DeriveKeyBlobsWithAuthBlockAsync(
      auth_block_type, auth_input, auth_factor.auth_block_state(),
      std::move(derive_callback));
}

void AuthSession::LoadUSSMainKeyAndFsKeyset(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptoStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs) {
  // Check the status of the callback error, to see if the key blob derivation
  // was actually successful.
  if (!callback_error.ok() || !key_blobs) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInLoadUSS),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlob derivation failed before loading USS";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveFailedInLoadUSS),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED)
            .Wrap(std::move(callback_error)));
    return;
  }

  // Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs->DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR)
        << "Failed to derive credential secret for authenticating auth factor";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
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
    std::move(on_done).Run(
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
    std::move(on_done).Run(
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
  SetAuthSessionAsAuthenticated(kAllAuthIntents);

  // Set the credential verifier for this credential.
  if (auth_input.user_input.has_value()) {
    SetCredentialVerifier(auth_factor_type, auth_factor_label,
                          auth_input.user_input.value());
  }

  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<CryptohomeError>());
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

base::TimeDelta AuthSession::GetRemainingTime() const {
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
