// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <absl/cleanup/cleanup.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/flat_set.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_prepare_purpose.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/flatbuffer.h"
#include "cryptohome/auth_factor/protobuf.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/with_driver.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_input_utils.h"
#include "cryptohome/auth_session_protobuf.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/reap.h"
#include "cryptohome/error/reporting.h"
#include "cryptohome/error/utilities.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/signature_sealing/structures_proto.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_policy_file.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/migrator.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_secret_stash/user_secret_stash.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/username.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;
using cryptohome::error::PrimaryActionIs;
using cryptohome::error::ReportCryptohomeOk;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::HmacSha256;
using hwsec_foundation::kAesBlockSize;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;
using user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;

// Size of the values used serialization of UnguessableToken.
constexpr int kSizeOfSerializedValueInToken = sizeof(uint64_t);
// Number of uint64 used serialization of UnguessableToken.
constexpr int kNumberOfSerializedValuesInToken = 2;
// Offset where the high value is used in Serialized string.
constexpr int kHighTokenOffset = 0;
// Offset where the low value is used in Serialized string.
constexpr int kLowTokenOffset = kSizeOfSerializedValueInToken;
// Upper limit of the Size of user specified name.
constexpr int kUserSpecifiedNameSizeLimit = 256;
// Message to use when generating a secret for hibernate.
constexpr char kHibernateSecretHmacMessage[] = "AuthTimeHibernateSecret";
// This is the frequency with which a signal is sent for a locked out user,
// unless the lockout time is less than this.
const base::TimeDelta kAuthFactorStatusUpdateDelay = base::Seconds(30);
// This is the post auth action that means no action needs to be taken.
AuthSession::PostAuthAction kNoPostAction{
    .action_type = AuthSession::PostAuthActionType::kNone,
    .repeat_request = std::nullopt,
};

// Check if a given type of AuthFactor supports Vault Keysets.
constexpr bool IsFactorTypeSupportedByVk(AuthFactorType auth_factor_type) {
  return auth_factor_type == AuthFactorType::kPassword ||
         auth_factor_type == AuthFactorType::kPin ||
         auth_factor_type == AuthFactorType::kSmartCard ||
         auth_factor_type == AuthFactorType::kKiosk;
}

constexpr std::string_view IntentToDebugString(AuthIntent intent) {
  switch (intent) {
    case AuthIntent::kDecrypt:
      return "decrypt";
    case AuthIntent::kVerifyOnly:
      return "verify-only";
    case AuthIntent::kWebAuthn:
      return "webauthn";
  }
}

std::string IntentSetToDebugString(const base::flat_set<AuthIntent>& intents) {
  std::vector<std::string_view> strings;
  strings.reserve(intents.size());
  for (auto intent : intents) {
    strings.push_back(IntentToDebugString(intent));
  }
  return base::JoinString(strings, ",");
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

// Generates a PIN reset secret from the |reset_seed| of the passed password
// VaultKeyset and updates the AuthInput  |reset_seed|, |reset_salt| and
// |reset_secret| values.
CryptohomeStatusOr<AuthInput> UpdateAuthInputWithResetParamsFromPasswordVk(
    const AuthInput& auth_input, const VaultKeyset& vault_keyset) {
  if (!vault_keyset.HasWrappedResetSeed()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUpdateAuthInputNoWrappedSeedInVaultKeyset),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (vault_keyset.GetResetSeed().empty()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUpdateAuthInputResetSeedEmptyInVaultKeyset),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  AuthInput out_auth_input = auth_input;
  out_auth_input.reset_seed = vault_keyset.GetResetSeed();
  out_auth_input.reset_salt = CreateSecureRandomBlob(kAesBlockSize);
  out_auth_input.reset_secret = HmacSha256(out_auth_input.reset_salt.value(),
                                           out_auth_input.reset_seed.value());
  LOG(INFO) << "Reset seed, to generate the reset_secret for the PIN factor, "
               "is obtained from password VaultKeyset with label: "
            << vault_keyset.GetLabel();
  return out_auth_input;
}

// Utility function to force-remove a keyset file for |obfuscated_username|
// identified by |label|.
CryptohomeStatus RemoveKeysetByLabel(
    KeysetManagement& keyset_management,
    const ObfuscatedUsername& obfuscated_username,
    const std::string& label) {
  std::unique_ptr<VaultKeyset> remove_vk =
      keyset_management.GetVaultKeyset(obfuscated_username, label);
  if (!remove_vk.get()) {
    LOG(WARNING) << "RemoveKeysetByLabel: key to remove not found.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionVKNotFoundInRemoveKeysetByLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }

  CryptohomeStatus status = keyset_management.ForceRemoveKeyset(
      obfuscated_username, remove_vk->GetLegacyIndex());
  if (!status.ok()) {
    LOG(ERROR) << "RemoveKeysetByLabel: failed to remove keyset file.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionRemoveFailedInRemoveKeysetByLabel),
               ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
               user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
        .Wrap(std::move(status));
  }
  return OkStatus<CryptohomeError>();
}

// Removes the backup VaultKeyset with the given label. Returns success if
// there's no keyset found.
CryptohomeStatus CleanUpBackupKeyset(
    KeysetManagement& keyset_management,
    const ObfuscatedUsername& obfuscated_username,
    const std::string& label) {
  std::unique_ptr<VaultKeyset> remove_vk =
      keyset_management.GetVaultKeyset(obfuscated_username, label);
  if (!remove_vk.get() || !remove_vk->IsForBackup()) {
    return OkStatus<CryptohomeError>();
  }

  CryptohomeStatus status = keyset_management.RemoveKeysetFile(*remove_vk);
  if (!status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionRemoveFailedInCleanUpBackupKeyset),
               ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
               user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
        .Wrap(std::move(status));
  }
  LOG(INFO) << "Removed backup keyset with label: " << label;
  return OkStatus<CryptohomeError>();
}

// Calculates and returns the reset secret for the PIN VaultKeyset with |label|
// if it exists and has |reset_salt|, returns nullopt otherwise.
std::optional<brillo::SecureBlob> GetResetSecretFromVaultKeyset(
    const brillo::SecureBlob& reset_seed,
    const ObfuscatedUsername& obfuscated_username,
    const std::string& label,
    const KeysetManagement& keyset_management) {
  std::unique_ptr<VaultKeyset> vk =
      keyset_management.GetVaultKeyset(obfuscated_username, label);
  if (vk == nullptr) {
    LOG(WARNING) << "Pin VK for the reset could not be retrieved for " << label
                 << ".";
    return std::nullopt;
  }
  brillo::SecureBlob reset_salt = vk->GetResetSalt();
  if (reset_salt.empty()) {
    LOG(WARNING) << "Reset salt is empty in VK  with label: " << label;
    return std::nullopt;
  }
  std::optional<brillo::SecureBlob> reset_secret =
      HmacSha256(reset_salt, reset_seed);
  LOG(INFO) << "Reset secret for " << label << " is captured from VaultKeyset";
  return reset_secret;
}

// Removes the backup VaultKeysets.
CryptohomeStatus CleanUpAllBackupKeysets(
    KeysetManagement& keyset_management,
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactorMap& auth_factor_map) {
  for (auto item : auth_factor_map) {
    CryptohomeStatus status = CleanUpBackupKeyset(
        keyset_management, obfuscated_username, item.auth_factor().label());
    if (!status.ok()) {
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthSessionRemoveFailedInCleanUpAllBackupKeysets),
                 ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
                 user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
          .Wrap(std::move(status));
    }
  }
  return OkStatus<CryptohomeError>();
}

void ReportRecreateAuthFactorError(CryptohomeStatus status,
                                   AuthFactorType auth_factor_type) {
  const std::array<std::string, 2> error_bucket_paths{
      kCryptohomeErrorRecreateAuthFactorErrorBucket,
      AuthFactorTypeToCamelCaseString(auth_factor_type)};
  ReapAndReportError(std::move(status), error_bucket_paths);
}

void ReportRecreateAuthFactorOk(AuthFactorType auth_factor_type) {
  const std::array<std::string, 2> error_bucket_paths{
      kCryptohomeErrorRecreateAuthFactorErrorBucket,
      AuthFactorTypeToCamelCaseString(auth_factor_type)};
  ReportCryptohomeOk(error_bucket_paths);
}

template <typename... Args>
void ReportAndExecuteCallback(
    base::OnceCallback<void(Args..., CryptohomeStatus)> callback,
    AuthFactorType auth_factor_type,
    const std::string& bucket_name,
    Args... args,
    CryptohomeStatus status) {
  const std::array<std::string, 2> error_bucket_paths{
      bucket_name, AuthFactorTypeToCamelCaseString(auth_factor_type)};
  ReportOperationStatus(status, error_bucket_paths);
  std::move(callback).Run(std::move(args)..., std::move(status));
}

template <typename... Args>
base::OnceCallback<void(Args..., CryptohomeStatus)>
WrapCallbackWithMetricsReporting(
    base::OnceCallback<void(Args..., CryptohomeStatus)> callback,
    AuthFactorType auth_factor_type,
    std::string bucket_name) {
  return base::BindOnce(&ReportAndExecuteCallback<Args...>, std::move(callback),
                        auth_factor_type, std::move(bucket_name));
}

SerializedUserAuthFactorTypePolicy GetAuthFactorPolicyFromUserPolicy(
    const std::optional<SerializedUserPolicy>& user_policy,
    AuthFactorType auth_factor_type) {
  if (!user_policy.has_value()) {
    return SerializedUserAuthFactorTypePolicy(
        {.type = SerializeAuthFactorType(auth_factor_type).value(),
         .enabled_intents = {},
         .disabled_intents = {}});
  }
  for (auto policy : user_policy->auth_factor_type_policy) {
    if (policy.type != std::nullopt &&
        policy.type == SerializeAuthFactorType(auth_factor_type)) {
      return policy;
    }
  }
  return SerializedUserAuthFactorTypePolicy(
      {.type = SerializeAuthFactorType(auth_factor_type).value(),
       .enabled_intents = {},
       .disabled_intents = {}});
}

}  // namespace

std::unique_ptr<AuthSession> AuthSession::Create(Username account_id,
                                                 unsigned int flags,
                                                 AuthIntent intent,
                                                 BackingApis backing_apis) {
  ObfuscatedUsername obfuscated_username = SanitizeUserName(account_id);

  // Try to determine if a user exists in two ways: they have a persistent
  // homedir, or they have an active mount. The latter can happen if the user is
  // ephemeral, in which case there will be no persistent directory but the user
  // still "exists" so long as they remain active.
  bool persistent_user_exists =
      backing_apis.platform->DirectoryExists(UserPath(obfuscated_username));
  UserSession* user_session = backing_apis.user_session_map->Find(account_id);
  bool user_is_active = user_session && user_session->IsActive();
  bool user_exists = persistent_user_exists || user_is_active;

  // If we have an existing persistent user, load all of their auth factors.
  AuthFactorMap auth_factor_map;
  if (persistent_user_exists) {
    // Load the USS so that we can use it to check the validity of any auth
    // factors being loaded.
    std::set<std::string_view> uss_labels;
    UserUssStorage uss_storage(*backing_apis.user_secret_stash_storage,
                               obfuscated_username);
    auto encrypted_uss = EncryptedUss::FromStorage(uss_storage);
    if (encrypted_uss.ok()) {
      uss_labels = encrypted_uss->WrappedMainKeyIds();
    }

    // Load all of the auth factors.
    AuthFactorVaultKeysetConverter converter(backing_apis.keyset_management);
    auth_factor_map = backing_apis.auth_factor_manager->LoadAllAuthFactors(
        obfuscated_username, uss_labels, converter);

    // If only uss factors exists, then we should remove all the backups.
    if (!auth_factor_map.HasFactorWithStorage(
            AuthFactorStorageType::kVaultKeyset)) {
      CryptohomeStatus cleanup_status =
          CleanUpAllBackupKeysets(*backing_apis.keyset_management,
                                  obfuscated_username, auth_factor_map);
      if (!cleanup_status.ok()) {
        LOG(WARNING) << "Cleaning up backup keysets failed.";
        // Error can be ignored.
      }
    }
  }
  // Assumption here is that keyset_management_ will outlive this AuthSession.
  AuthSession::Params params = {
      .username = std::move(account_id),
      .is_ephemeral_user = flags & AUTH_SESSION_FLAGS_EPHEMERAL_USER,
      .intent = intent,
      .auth_factor_status_update_timer =
          std::make_unique<base::WallClockTimer>(),
      .user_exists = user_exists,
      .auth_factor_map = std::move(auth_factor_map)};
  return std::make_unique<AuthSession>(std::move(params), backing_apis);
}

AuthSession::AuthSession(Params params, BackingApis backing_apis)
    : username_(std::move(*params.username)),
      obfuscated_username_(SanitizeUserName(username_)),
      is_ephemeral_user_(*params.is_ephemeral_user),
      auth_intent_(*params.intent),
      auth_factor_status_update_timer_(
          std::move(params.auth_factor_status_update_timer)),
      auth_session_creation_time_(base::TimeTicks::Now()),
      uss_storage_(*backing_apis.user_secret_stash_storage,
                   obfuscated_username_),
      crypto_(backing_apis.crypto),
      platform_(backing_apis.platform),
      user_session_map_(backing_apis.user_session_map),
      verifier_forwarder_(username_, user_session_map_),
      keyset_management_(backing_apis.keyset_management),
      auth_block_utility_(backing_apis.auth_block_utility),
      auth_factor_driver_manager_(backing_apis.auth_factor_driver_manager),
      auth_factor_manager_(backing_apis.auth_factor_manager),
      features_(backing_apis.features),
      converter_(keyset_management_),
      token_(platform_->CreateUnguessableToken()),
      serialized_token_(GetSerializedStringFromToken(token_).value_or("")),
      public_token_(platform_->CreateUnguessableToken()),
      serialized_public_token_(
          GetSerializedStringFromToken(public_token_).value_or("")),
      user_exists_(*params.user_exists),
      auth_factor_map_(std::move(params.auth_factor_map)) {
  CHECK(!serialized_token_.empty());
  CHECK(auth_factor_status_update_timer_);
  CHECK(crypto_);
  CHECK(platform_);
  CHECK(user_session_map_);
  CHECK(keyset_management_);
  CHECK(auth_block_utility_);
  CHECK(auth_factor_manager_);
  CHECK(features_);
  auth_factor_map_.ReportAuthFactorBackingStoreMetrics();
  RecordAuthSessionStart();
}

AuthSession::~AuthSession() {
  std::string append_string = is_ephemeral_user_ ? ".Ephemeral" : ".Persistent";
  ReportTimerDuration(kAuthSessionTotalLifetimeTimer,
                      auth_session_creation_time_, append_string);
  ReportTimerDuration(kAuthSessionAuthenticatedLifetimeTimer,
                      authenticated_time_, append_string);
}

base::flat_set<AuthIntent> AuthSession::authorized_intents() const {
  base::flat_set<AuthIntent> intents;
  // Generic helper that checks an auth_for_* field and adds the intent to
  // intents if it is authorized.
  auto check_auth_for = [&intents](const auto& field) {
    if (field) {
      intents.insert(
          std::remove_reference_t<decltype(field)>::value_type::kIntent);
    }
  };
  check_auth_for(auth_for_decrypt_);
  check_auth_for(auth_for_verify_only_);
  check_auth_for(auth_for_web_authn_);
  return intents;
}

void AuthSession::RecordAuthSessionStart() const {
  std::vector<std::string> factors;
  factors.reserve(auth_factor_map_.size());
  for (AuthFactorMap::ValueView item : auth_factor_map_) {
    factors.push_back(base::StringPrintf(
        "%s(type %d %s)", item.auth_factor().label().c_str(),
        static_cast<int>(item.auth_factor().type()),
        AuthFactorStorageTypeToDebugString(item.storage_type())));
  }
  LOG(INFO) << "AuthSession: started with is_ephemeral_user="
            << is_ephemeral_user_
            << " intent=" << IntentToDebugString(auth_intent_)
            << " user_exists=" << user_exists_
            << " factors=" << base::JoinString(factors, ",") << ".";
}

void AuthSession::SetAuthorizedForIntents(
    base::flat_set<AuthIntent> new_authorized_intents) {
  if (new_authorized_intents.empty()) {
    NOTREACHED() << "Empty intent set cannot be authorized";
    return;
  }

  // Generic helper that sets an auth_for_* field if it's not already set and
  // the intent appears in the given new intents.
  auto set_auth_for = [this, &new_authorized_intents](auto& field) {
    using AuthForType = std::remove_reference_t<decltype(field)>::value_type;
    if (!field && new_authorized_intents.contains(AuthForType::kIntent)) {
      field.emplace(*this, typename AuthForType::Passkey());
    }
  };
  set_auth_for(auth_for_decrypt_);
  set_auth_for(auth_for_verify_only_);
  set_auth_for(auth_for_web_authn_);

  if (auth_for_decrypt_) {
    // Record time of authentication for metric keeping.
    authenticated_time_ = base::TimeTicks::Now();
  }
  LOG(INFO) << "AuthSession: authorized for "
            << IntentSetToDebugString(authorized_intents()) << ".";

  // Trigger all of the on-auth callbacks.
  std::vector<base::OnceClosure> callbacks;
  std::swap(callbacks, on_auth_);
  for (base::OnceClosure& callback : callbacks) {
    std::move(callback).Run();
  }
}

void AuthSession::SetAuthorizedForFullAuthIntents(
    AuthFactorType auth_factor_type,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy) {
  // Determine what intents are allowed for this factor type under full auth.
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  std::vector<AuthIntent> authorized_for;
  for (AuthIntent intent : {AuthIntent::kDecrypt, AuthIntent::kVerifyOnly}) {
    if (factor_driver.IsFullAuthSupported(intent) &&
        IsIntentEnabledBasedOnPolicy(factor_driver, intent,
                                     auth_factor_type_user_policy)) {
      authorized_for.push_back(intent);
    }
  }
  // Authorize the session for the subset of intents we found.
  SetAuthorizedForIntents(authorized_for);
}

void AuthSession::SendAuthFactorStatusUpdateSignal() {
  // If the auth factor status update callback is not set (testing purposes),
  // then we won't need to send a signal.
  if (!auth_factor_status_update_callback_) {
    LOG(ERROR) << "Auth factor status update callback has not been set.";
    return;
  }
  UserPolicyFile user_policy_file(platform_,
                                  GetUserPolicyPath(obfuscated_username_));
  if (!user_policy_file.LoadFromFile().ok()) {
    user_policy_file.UpdateUserPolicy(
        SerializedUserPolicy({.auth_factor_type_policy = {}}));
  }
  auto user_policy = user_policy_file.GetUserPolicy();

  for (AuthFactorMap::ValueView item : auth_factor_map_) {
    const AuthFactor& auth_factor = item.auth_factor();
    AuthFactorDriver& driver =
        auth_factor_driver_manager_->GetDriver(auth_factor.type());
    // Skip this entire process for factors which don't support delays.
    if (!driver.IsDelaySupported()) {
      continue;
    }

    auto auth_factor_proto =
        driver.ConvertToProto(auth_factor.label(), auth_factor.metadata());
    if (!auth_factor_proto) {
      continue;
    }

    user_data_auth::AuthFactorWithStatus auth_factor_with_status;
    *auth_factor_with_status.mutable_auth_factor() =
        std::move(*auth_factor_proto);
    base::flat_set<AuthIntent> supported_intents = GetFullAuthAvailableIntents(
        obfuscated_username_, auth_factor, *auth_factor_driver_manager_,
        GetAuthFactorPolicyFromUserPolicy(user_policy, auth_factor.type()));

    for (const auto& auth_intent : supported_intents) {
      auth_factor_with_status.add_available_for_intents(
          AuthIntentToProto(auth_intent));
    }

    auto delay = driver.GetFactorDelay(obfuscated_username_, auth_factor);
    if (delay.ok()) {
      auth_factor_with_status.mutable_status_info()->set_time_available_in(
          delay->is_max() ? std::numeric_limits<uint64_t>::max()
                          : delay->InMilliseconds());
      auth_factor_status_update_callback_.Run(auth_factor_with_status,
                                              serialized_public_token_);
      if (delay->is_zero()) {
        continue;
      }
      base::TimeDelta next_signal_delay =
          std::min(*delay, kAuthFactorStatusUpdateDelay);
      auth_factor_status_update_timer_->Start(
          FROM_HERE, base::Time::Now() + next_signal_delay,
          base::BindOnce(&AuthSession::SendAuthFactorStatusUpdateSignal,
                         base::Unretained(this)));
    }
  }
}

CryptohomeStatus AuthSession::OnUserCreated() {
  // Since this function is called for a new user, it is safe to put the
  // AuthSession in an authenticated state.
  SetAuthorizedForIntents({AuthIntent::kDecrypt, AuthIntent::kVerifyOnly});
  user_exists_ = true;

  if (!is_ephemeral_user_) {
    // Creating file_system_keyset to the prepareVault call next.
    if (!file_system_keyset_.has_value()) {
      file_system_keyset_ = FileSystemKeyset::CreateRandom();
    }
    if (IsUserSecretStashExperimentEnabled(platform_)) {
      // Check invariants.
      CHECK(!decrypted_uss_);
      CHECK(file_system_keyset_.has_value());
      // The USS experiment is on, hence create the USS for the newly created
      // non-ephemeral user. Keep the USS in memory: it will be persisted after
      // the first auth factor gets added.
      CryptohomeStatusOr<DecryptedUss> new_uss =
          DecryptedUss::CreateWithRandomMainKey(uss_storage_,
                                                *file_system_keyset_);
      if (!new_uss.ok()) {
        LOG(ERROR) << "User secret stash creation failed";
        return MakeStatus<CryptohomeError>(
                   CRYPTOHOME_ERR_LOC(
                       kLocAuthSessionCreateUSSFailedInOnUserCreated),
                   ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                                   PossibleAction::kReboot}),
                   user_data_auth::CryptohomeErrorCode::
                       CRYPTOHOME_ERROR_MOUNT_FATAL)
            .Wrap(std::move(new_uss).err_status());
      }
      decrypted_uss_ = std::move(*new_uss);
    }
  }

  return OkStatus<CryptohomeError>();
}

void AuthSession::RegisterVaultKeysetAuthFactor(AuthFactor auth_factor) {
  auth_factor_map_.Add(std::move(auth_factor),
                       AuthFactorStorageType::kVaultKeyset);
}

void AuthSession::CreateAndPersistVaultKeyset(
    const KeyData& key_data,
    AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  // callback_error, key_blobs and auth_state are returned by
  // AuthBlock::CreateCallback.
  if (!callback_error.ok() || key_blobs == nullptr || auth_state == nullptr) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInCallbackInAddKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
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

  CryptohomeStatus status =
      AddVaultKeyset(key_data.label(), key_data,
                     !auth_factor_map_.HasFactorWithStorage(
                         AuthFactorStorageType::kVaultKeyset),
                     VaultKeysetIntent{.backup = false}, std::move(key_blobs),
                     std::move(auth_state));

  if (!status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionAddVaultKeysetFailedinAddAuthFactor),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  std::optional<AuthFactor> added_auth_factor =
      converter_.VaultKeysetToAuthFactor(obfuscated_username_,
                                         key_data.label());
  // Initialize auth_factor_type with kPassword for CredentailVerifier.
  AuthFactorType auth_factor_type = AuthFactorType::kPassword;
  if (added_auth_factor) {
    auth_factor_type = added_auth_factor->type();
    auth_factor_map_.Add(std::move(*added_auth_factor),
                         AuthFactorStorageType::kVaultKeyset);
  } else {
    LOG(WARNING) << "Failed to convert added keyset to AuthFactor.";
  }

  AddCredentialVerifier(auth_factor_type, key_data.label(), auth_input);

  // Report timer for how long AuthSession operation takes.
  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

CryptohomeStatus AuthSession::AddVaultKeyset(
    const std::string& key_label,
    const KeyData& key_data,
    bool is_initial_keyset,
    VaultKeysetIntent vk_backup_intent,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  CHECK(key_blobs);
  CHECK(auth_state);
  if (is_initial_keyset) {
    if (!file_system_keyset_.has_value()) {
      LOG(ERROR) << "AddInitialKeyset: file_system_keyset is invalid.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNoFSKeyInAddKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kReboot}),
          user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
    }
    // TODO(b/229825202): Migrate KeysetManagement and wrap the returned error.
    CryptohomeStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
        keyset_management_->AddInitialKeyset(
            vk_backup_intent, obfuscated_username_, key_data,
            /*challenge_credentials_keyset_info*/ std::nullopt,
            file_system_keyset_.value(), std::move(*key_blobs.get()),
            std::move(auth_state));
    if (!vk_status.ok()) {
      vault_keyset_ = nullptr;
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionAddInitialFailedInAddKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kReboot}),
          user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
    }
    LOG(INFO) << "AuthSession: added initial keyset " << key_data.label()
              << ".";
    vault_keyset_ = std::move(vk_status).value();
  } else {
    if (!vault_keyset_) {
      // This shouldn't normally happen, but is possible if, e.g., the backup VK
      // is corrupted and the authentication completed via USS.
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNoVkInAddKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
    }
    CryptohomeStatus status = keyset_management_->AddKeyset(
        vk_backup_intent, obfuscated_username_, key_label, key_data,
        *vault_keyset_.get(), std::move(*key_blobs.get()),
        std::move(auth_state), true /*clobber*/);
    if (!status.ok()) {
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(kLocAuthSessionAddFailedInAddKeyset))
          .Wrap(std::move(status));
    }
    LOG(INFO) << "AuthSession: added additional keyset " << key_label << ".";
  }

  return OkStatus<CryptohomeError>();
}

void AuthSession::MigrateToUssDuringUpdateVaultKeyset(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const KeyData& key_data,
    const AuthInput& auth_input,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  // Update can happen only during an authenticated AuthSession.
  CHECK(file_system_keyset_.has_value());

  if (!callback_error.ok() || key_blobs == nullptr ||
      auth_block_state == nullptr) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInCallbackInUpdateKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlobs derivation failed before updating keyset.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInUpdateKeyset))
            .Wrap(std::move(callback_error)));
    return;
  }

  // Add the new secret to the AuthSession's credential verifier. On successful
  // completion of the UpdateAuthFactor this will be passed to UserSession's
  // credential verifier to cache the secret for future lightweight
  // verifications.
  AddCredentialVerifier(auth_factor_type, auth_factor_label, auth_input);

  if (IsUserSecretStashExperimentEnabled(platform_)) {
    UssMigrator migrator(username_);
    // FilesystemKeyset is the same for all VaultKeysets hence the session's
    // |file_system_keyset_| is what we need for the migrator.
    migrator.MigrateVaultKeysetToUss(
        uss_storage_, auth_factor_label, file_system_keyset_.value(),
        base::BindOnce(&AuthSession::OnMigrationUssCreatedForUpdate,
                       weak_factory_.GetWeakPtr(), auth_factor_type,
                       auth_factor_label, auth_factor_metadata, auth_input,
                       std::move(on_done), std::move(callback_error),
                       std::move(key_blobs), std::move(auth_block_state)));
    // Since migration removes the keyset file, we don't update the keyset file.
    return;
  }

  CryptohomeStatus status = keyset_management_->UpdateKeysetWithKeyBlobs(
      VaultKeysetIntent{.backup = false}, obfuscated_username_, key_data,
      *vault_keyset_.get(), std::move(*key_blobs.get()),
      std::move(auth_block_state));
  if (!status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionUpdateWithBlobFailedInUpdateKeyset))
            .Wrap(std::move(status)));
  }
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthenticateViaVaultKeysetAndMigrateToUss(
    AuthFactorType request_auth_factor_type,
    const std::string& key_label,
    const AuthInput& auth_input,
    const AuthFactorMetadata& metadata,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    StatusCallback on_done) {
  // Identify the key via `key_label` instead of `key_data_.label()`, as the
  // latter can be empty for legacy keysets.
  std::unique_ptr<VaultKeyset> vault_keyset =
      keyset_management_->GetVaultKeyset(obfuscated_username_, key_label);
  if (!vault_keyset) {
    LOG(ERROR)
        << "No vault keyset is found on disk for label " << key_label
        << ". Cannot obtain AuthBlockState without vault keyset metadata.";
    std::move(on_done).Run(MakeStatus<error::CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionVaultKeysetMissingInAuthViaVaultKey),
        ErrorActionSet({error::PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return;
  }
  AuthBlockState auth_state;
  if (!GetAuthBlockState(*vault_keyset, auth_state)) {
    LOG(ERROR) << "Error in obtaining AuthBlock state for key derivation.";
    std::move(on_done).Run(MakeStatus<error::CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionBlockStateMissingInAuthViaVaultKey),
        ErrorActionSet({error::PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return;
  }

  // Determine the auth block type to use.
  std::optional<AuthBlockType> auth_block_type =
      auth_block_utility_->GetAuthBlockTypeFromState(auth_state);
  if (!auth_block_type) {
    LOG(ERROR) << "Failed to determine auth block type from auth block state";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInAuthViaVaultKey),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return;
  }

  // Parameterize the AuthSession performance timer by AuthBlockType
  auth_session_performance_timer->auth_block_type = *auth_block_type;

  // Derive KeyBlobs from the existing VaultKeyset, using GetValidKeyset
  // as a callback that loads |vault_keyset_| and resaves if needed.
  AuthBlock::DeriveCallback derive_callback = base::BindOnce(
      &AuthSession::LoadVaultKeysetAndFsKeys, weak_factory_.GetWeakPtr(),
      request_auth_factor_type, auth_input, *auth_block_type, metadata,
      std::move(auth_session_performance_timer),
      std::move(auth_factor_type_user_policy), std::move(on_done));

  auth_block_utility_->DeriveKeyBlobsWithAuthBlock(
      *auth_block_type, auth_input, auth_state, std::move(derive_callback));
}

void AuthSession::LoadVaultKeysetAndFsKeys(
    AuthFactorType request_auth_factor_type,
    const AuthInput& auth_input,
    AuthBlockType auth_block_type,
    const AuthFactorMetadata& metadata,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    StatusCallback on_done,
    CryptohomeStatus status,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::optional<AuthBlock::SuggestedAction> suggested_action) {
  if (!status.ok() || !key_blobs) {
    // For LE credentials, if deriving the key blobs failed due to too many
    // attempts, set auth_locked=true in the corresponding keyset. Then save it
    // for future callers who can Load it w/o Decrypt'ing to check that flag.
    // When the pin is entered wrong and AuthBlock fails to derive the KeyBlobs
    // it doesn't make it into the VaultKeyset::Decrypt(); so auth_lock should
    // be set here.
    if (!status.ok() &&
        PrimaryActionIs(status, error::PrimaryAction::kLeLockedOut)) {
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
          ErrorActionSet({error::PossibleAction::kDevCheckUnexpectedState}),
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

  CHECK(status.ok());

  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      keyset_management_->GetValidKeyset(
          obfuscated_username_, std::move(*key_blobs.get()), key_data_.label());
  if (!vk_status.ok()) {
    vault_keyset_ = nullptr;
    LOG(ERROR) << "Failed to load VaultKeyset and file system keyset.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeMountError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionGetValidKeysetFailedInLoadVaultKeyset))
            .Wrap(std::move(vk_status).err_status()));
    return;
  }
  vault_keyset_ = std::move(vk_status).value();

  // Authentication is successfully completed. Reset LE Credential counter if
  // the current AutFactor is not an LECredential.
  if (!vault_keyset_->IsLECredential()) {
    ResetLECredentials();
  }

  // If there is a change in the AuthBlock type during resave operation it'll be
  // updated.
  AuthBlockType auth_block_type_for_resaved_vk =
      ResaveVaultKeysetIfNeeded(auth_input.user_input, auth_block_type);
  file_system_keyset_ = FileSystemKeyset(*vault_keyset_);

  CryptohomeStatus prepare_status = OkStatus<error::CryptohomeError>();
  if (auth_intent_ == AuthIntent::kWebAuthn) {
    // Even if we failed to prepare WebAuthn secret, file system keyset
    // is already populated and we should proceed to set AuthSession as
    // authenticated. Just return the error status at last.
    prepare_status = PrepareWebAuthnSecret();
    if (!prepare_status.ok()) {
      LOG(ERROR) << "Failed to prepare WebAuthn secret: " << prepare_status;
    }
  }

  if (CryptohomeStatus status = PrepareChapsKey(); !status.ok()) {
    LOG(ERROR) << "Failed to prepare chaps key: " << status;
  }

  // Flip the status on the successful authentication.
  SetAuthorizedForFullAuthIntents(request_auth_factor_type,
                                  auth_factor_type_user_policy);

  // Set the credential verifier for this credential.
  AddCredentialVerifier(request_auth_factor_type, vault_keyset_->GetLabel(),
                        auth_input);

  ReportTimerDuration(auth_session_performance_timer.get());

  if (auth_for_decrypt_ && IsUserSecretStashExperimentEnabled(platform_)) {
    UssMigrator migrator(username_);

    migrator.MigrateVaultKeysetToUss(
        uss_storage_, vault_keyset_->GetLabel(), file_system_keyset_.value(),
        base::BindOnce(
            &AuthSession::OnMigrationUssCreated, weak_factory_.GetWeakPtr(),
            auth_block_type_for_resaved_vk, request_auth_factor_type, metadata,
            auth_input, std::move(prepare_status), std::move(on_done)));
    return;
  }

  std::move(on_done).Run(std::move(prepare_status));
}

void AuthSession::OnMigrationUssCreatedForUpdate(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state,
    std::optional<DecryptedUss> loaded_uss) {
  if (!loaded_uss) {
    LOG(ERROR) << "Uss migration during UpdateVaultKeyset failed for "
                  "VaultKeyset with label: "
               << auth_factor_label;
    // We don't report VK to USS migration status here because it is expected
    // that the actual migration will have already reported a more precise error
    // directly.
    std::move(on_done).Run(OkStatus<CryptohomeError>());
    return;
  }

  decrypted_uss_ = std::move(loaded_uss);

  auto migration_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(kUSSMigrationTimer);

  // Migrating a VaultKeyset to UserSecretStash during UpdateAuthFactor is
  // adding a new KeyBlock to UserSecretStash.
  PersistAuthFactorToUserSecretStashOnMigration(
      auth_factor_type, auth_factor_label, auth_factor_metadata, auth_input,
      std::move(migration_performance_timer), std::move(on_done),
      OkStatus<CryptohomeError>(), std::move(callback_error),
      std::move(key_blobs), std::move(auth_block_state));
}

void AuthSession::OnMigrationUssCreated(
    AuthBlockType auth_block_type,
    AuthFactorType auth_factor_type,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    CryptohomeStatus pre_migration_status,
    StatusCallback on_done,
    std::optional<DecryptedUss> loaded_uss) {
  if (!loaded_uss) {
    LOG(ERROR) << "Uss migration failed for VaultKeyset with label: "
               << key_data_.label();
    // We don't report VK to USS migration status here because it is expected
    // that the actual migration will have already reported a more precise error
    // directly.
    std::move(on_done).Run(std::move(pre_migration_status));
    return;
  }

  decrypted_uss_ = std::move(loaded_uss);

  auto migration_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(kUSSMigrationTimer);

  // During the USS migration of a password credential reset_secret is driven
  // and put into the newly created USS file. This reset_secret is used for
  // unmigrated PIN credential if needed.
  //
  // During the USS migration of a PIN credential reset_secret is added together
  // with the created KeyBlobs, which already includes the reset secret of the
  // migrated PIN. Hence don't abort the password migration if the
  // |reset_secret| can't be added during the password migration.
  if (MigrateResetSecretToUss()) {
    LOG(INFO) << "Reset secret is migrated to UserSecretStash before the "
                 "migration of the PIN VaultKeyset.";
  }

  CryptohomeStatusOr<AuthInput> migration_auth_input_status =
      CreateAuthInputForMigration(auth_input, auth_factor_type);
  if (!migration_auth_input_status.ok()) {
    LOG(ERROR) << "Failed to create migration AuthInput: "
               << migration_auth_input_status.status();
    ReapAndReportError(std::move(migration_auth_input_status).status(),
                       kCryptohomeErrorUssMigrationErrorBucket);
    ReportVkToUssMigrationStatus(VkToUssMigrationStatus::kFailedInput);
    std::move(on_done).Run(std::move(pre_migration_status));
    return;
  }

  // If |vault_keyset_| has an empty label legacy label from GetLabel() is
  // passed for the USS wrapped block.
  auto create_callback = base::BindOnce(
      &AuthSession::PersistAuthFactorToUserSecretStashOnMigration,
      weak_factory_.GetWeakPtr(), auth_factor_type, vault_keyset_->GetLabel(),
      auth_factor_metadata, migration_auth_input_status.value(),
      std::move(migration_performance_timer), std::move(on_done),
      std::move(pre_migration_status));

  auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      auth_block_type, migration_auth_input_status.value(),
      std::move(create_callback));
}

const FileSystemKeyset& AuthSession::file_system_keyset() const {
  CHECK(file_system_keyset_.has_value());
  return file_system_keyset_.value();
}

bool AuthSession::MigrateResetSecretToUss() {
  CHECK(decrypted_uss_);
  if (!vault_keyset_->HasWrappedResetSeed()) {
    // Authenticated VaultKeyset doesn't include a reset seed if it is not a
    // password VaultKeyset";
    return false;
  }

  bool updated = false;
  auto transaction = decrypted_uss_->StartTransaction();
  for (AuthFactorMap::ValueView stored_auth_factor : auth_factor_map_) {
    // Look for only pinweaver and VaultKeyset backed AuthFactors.
    if (stored_auth_factor.storage_type() !=
        AuthFactorStorageType::kVaultKeyset) {
      continue;
    }
    const AuthFactor& auth_factor = stored_auth_factor.auth_factor();
    if (auth_factor.type() != AuthFactorType::kPin) {
      continue;
    }

    // Skip any factors that already have a reset secret in USS.
    if (decrypted_uss_->GetResetSecret(auth_factor.label())) {
      continue;
    }

    // Try and add secret migration to the transaction.
    std::optional<brillo::SecureBlob> reset_secret =
        GetResetSecretFromVaultKeyset(vault_keyset_->GetResetSeed(),
                                      obfuscated_username_, auth_factor.label(),
                                      *keyset_management_);
    if (!reset_secret) {
      LOG(WARNING)
          << "Failed to obtain reset secret to migrate to USS for the factor: "
          << auth_factor.label();
      continue;
    }
    if (transaction
            .InsertResetSecret(auth_factor.label(), std::move(*reset_secret))
            .ok()) {
      updated = true;
    }
  }

  // If updates occurred, attempt to commit them. We only return true both if
  // there were updates and if the commit was successful.
  if (updated) {
    auto status = std::move(transaction).Commit();
    if (status.ok()) {
      return true;
    } else {
      LOG(WARNING) << "Unable to commit persist secret migration to USS: "
                   << status;
    }
  }
  return false;
}

void AuthSession::AuthenticateAuthFactor(
    const AuthenticateAuthFactorRequest& request,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    AuthenticateAuthFactorCallback callback) {
  const std::vector<std::string>& auth_factor_labels =
      request.auth_factor_labels;
  const user_data_auth::AuthInput& auth_input_proto = request.auth_input_proto;
  std::string label_text = auth_factor_labels.empty()
                               ? "(unlabelled)"
                               : base::JoinString(auth_factor_labels, ",");
  LOG(INFO) << "AuthSession: " << IntentToDebugString(auth_intent_)
            << " authentication attempt via " << label_text;
  // Determine the factor type from the request.
  std::optional<AuthFactorType> request_auth_factor_type =
      DetermineFactorTypeFromAuthInput(auth_input_proto);
  if (!request_auth_factor_type.has_value()) {
    LOG(ERROR) << "Unexpected AuthInput type.";
    std::move(callback).Run(
        kNoPostAction,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionNoAuthFactorTypeInAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  auto callback_with_metrics =
      WrapCallbackWithMetricsReporting<const PostAuthAction&>(
          std::move(callback), *request_auth_factor_type,
          kCryptohomeErrorAuthenticateAuthFactorErrorBucket);

  // Currently only lightweight auth might specify a non-null post-auth action,
  // so use the callback pre-bound with null post-auth action in all other
  // places to keep code simple.
  auto [on_done_temp, on_done_with_action] =
      base::SplitOnceCallback(std::move(callback_with_metrics));
  StatusCallback on_done =
      base::BindOnce(std::move(on_done_temp), kNoPostAction);

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(*request_auth_factor_type);
  AuthFactorLabelArity label_arity = factor_driver.GetAuthFactorLabelArity();
  switch (label_arity) {
    case AuthFactorLabelArity::kNone: {
      if (auth_factor_labels.size() > 0) {
        LOG(ERROR) << "Unexpected labels for request auth factor type:"
                   << AuthFactorTypeToString(*request_auth_factor_type);
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionMismatchedZeroLabelSizeAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }
      const CredentialVerifier* verifier = nullptr;
      // Search for a verifier from the User Session, if available.
      const UserSession* user_session = user_session_map_->Find(username_);
      if (user_session && user_session->VerifyUser(obfuscated_username_)) {
        verifier =
            user_session->FindCredentialVerifier(*request_auth_factor_type);
      }
      // A CredentialVerifier must exist if there is no label and the verifier
      // will be used for authentication.
      if (!verifier || !factor_driver.IsLightAuthSupported(auth_intent_) ||
          !IsIntentEnabledBasedOnPolicy(factor_driver, auth_intent_,
                                        auth_factor_type_user_policy) ||
          request.flags.force_full_auth == ForceFullAuthFlag::kForce) {
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionVerifierNotValidInAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
        return;
      }
      CryptohomeStatusOr<AuthInput> auth_input =
          CreateAuthInputForAuthentication(auth_input_proto,
                                           verifier->auth_factor_metadata());
      if (!auth_input.ok()) {
        std::move(on_done).Run(
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocAuthSessionAuthInputParseFailedInAuthAuthFactor))
                .Wrap(std::move(auth_input).err_status()));
        return;
      }
      auto verify_callback = base::BindOnce(
          &AuthSession::CompleteVerifyOnlyAuthentication,
          weak_factory_.GetWeakPtr(), std::move(on_done_with_action), request,
          *request_auth_factor_type);
      verifier->Verify(std::move(*auth_input), std::move(verify_callback));
      return;
    }
    case AuthFactorLabelArity::kSingle: {
      if (auth_factor_labels.size() != 1) {
        LOG(ERROR) << "Unexpected zero or multiple labels for request auth "
                      "factor type:"
                   << AuthFactorTypeToString(*request_auth_factor_type);
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionMismatchedSingleLabelSizeAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }
      // Construct a CredentialVerifier and verify as authentication if the auth
      // intent allows it.
      const CredentialVerifier* verifier = nullptr;
      // Search for a verifier from the User Session, if available.
      const UserSession* user_session = user_session_map_->Find(username_);
      if (user_session && user_session->VerifyUser(obfuscated_username_)) {
        verifier = user_session->FindCredentialVerifier(auth_factor_labels[0]);
      }

      bool restoring_chaps = user_session && user_session->GetPkcs11Token() &&
                             user_session->GetPkcs11Token()->NeedRestore() &&
                             factor_driver.IsFullAuthSupported(auth_intent_);

      // Attempt lightweight authentication via a credential verifier if
      // suitable.
      if (!restoring_chaps && verifier &&
          factor_driver.IsLightAuthSupported(auth_intent_) &&
          IsIntentEnabledBasedOnPolicy(factor_driver, auth_intent_,
                                       auth_factor_type_user_policy) &&
          request.flags.force_full_auth != ForceFullAuthFlag::kForce) {
        CryptohomeStatusOr<AuthInput> auth_input =
            CreateAuthInputForAuthentication(auth_input_proto,
                                             verifier->auth_factor_metadata());
        if (!auth_input.ok()) {
          std::move(on_done).Run(
              MakeStatus<CryptohomeError>(
                  CRYPTOHOME_ERR_LOC(
                      kLocAuthSessionAuthInputParseFailed2InAuthAuthFactor))
                  .Wrap(std::move(auth_input).err_status()));
          return;
        }
        auto verify_callback = base::BindOnce(
            &AuthSession::CompleteVerifyOnlyAuthentication,
            weak_factory_.GetWeakPtr(), std::move(on_done_with_action), request,
            *request_auth_factor_type);
        verifier->Verify(std::move(*auth_input), std::move(verify_callback));
        return;
      }

      // If we get here, we need to use full authentication. Make sure that it
      // is supported for this type of auth factor and intent.
      if (!factor_driver.IsFullAuthSupported(auth_intent_) ||
          !IsIntentEnabledBasedOnPolicy(factor_driver, auth_intent_,
                                        auth_factor_type_user_policy)) {
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionSingleLabelFullAuthNotSupportedAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }

      // Load the auth factor and it should exist for authentication.
      std::optional<AuthFactorMap::ValueView> stored_auth_factor =
          auth_factor_map_.Find(auth_factor_labels[0]);
      if (!stored_auth_factor) {
        // This could happen for 2 reasons, either the user doesn't exist or the
        // auth factor is not available for this user.
        if (!user_exists_) {
          // Attempting to authenticate a user that doesn't exist.
          LOG(ERROR) << "Attempting to authenticate user that doesn't exist: "
                     << username_;
          std::move(on_done).Run(MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionUserNotFoundInAuthAuthFactor),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND));
          return;
        }
        LOG(ERROR) << "Authentication factor not found: "
                   << auth_factor_labels[0];
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
        return;
      }

      AuthFactorMetadata metadata =
          stored_auth_factor->auth_factor().metadata();
      // Ensure that if an auth factor is found, the requested type matches what
      // we have on disk for the user.
      if (*request_auth_factor_type !=
          stored_auth_factor->auth_factor().type()) {
        // We have to special case kiosk keysets, because for old vault keyset
        // factors the underlying data may not be marked as a kiosk and so it
        // will show up as a password auth factor instead. In that case we treat
        // the request as authoritative, and instead fix up the metadata.
        if (stored_auth_factor->storage_type() ==
                AuthFactorStorageType::kVaultKeyset &&
            request_auth_factor_type == AuthFactorType::kKiosk) {
          metadata.metadata = KioskMetadata();
        } else {
          LOG(ERROR)
              << "Unexpected mismatch in type from label and auth_input.";
          std::move(on_done).Run(MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionMismatchedAuthTypes),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_INVALID_ARGUMENT));
          return;
        }
      }

      CryptohomeStatusOr<AuthInput> auth_input =
          CreateAuthInputForAuthentication(auth_input_proto, metadata);
      if (!auth_input.ok()) {
        std::move(on_done).Run(
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocAuthSessionAuthInputParseFailed3InAuthAuthFactor))
                .Wrap(std::move(auth_input).err_status()));
        return;
      }
      AuthenticateViaSingleFactor(
          *request_auth_factor_type, stored_auth_factor->auth_factor().label(),
          std::move(*auth_input), metadata, *stored_auth_factor,
          std::move(auth_factor_type_user_policy), std::move(on_done));
      return;
    }
    case AuthFactorLabelArity::kMultiple: {
      if (auth_factor_labels.size() == 0) {
        LOG(ERROR) << "Unexpected zero label for request auth factor type:"
                   << AuthFactorTypeToString(*request_auth_factor_type);
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionMismatchedMultipLabelSizeAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }

      // If we get here, we need to use full authentication. Make sure that it
      // is supported for this type of auth factor and intent.
      if (!factor_driver.IsFullAuthSupported(auth_intent_) ||
          !IsIntentEnabledBasedOnPolicy(factor_driver, auth_intent_,
                                        auth_factor_type_user_policy)) {
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionMultiLabelFullAuthNotSupportedAuthAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }

      std::vector<AuthFactor> auth_factors;
      // All the auth factors iterated here should have the same auth block
      // type.
      std::optional<AuthBlockType> auth_block_type;
      for (const std::string& label : auth_factor_labels) {
        // Load the auth factor and it should exist for authentication.
        std::optional<AuthFactorMap::ValueView> stored_auth_factor =
            auth_factor_map_.Find(label);
        if (!stored_auth_factor) {
          // This could happen for 2 reasons, either the user doesn't exist or
          // the auth factor is not available for this user.
          if (!user_exists_) {
            // Attempting to authenticate a user that doesn't exist.
            LOG(ERROR) << "Attempting to authenticate user that doesn't exist: "
                       << username_;
            std::move(on_done).Run(MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocAuthSessionUserNotFoundInMultiLabelAuthAuthFactor),
                ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
                user_data_auth::CryptohomeErrorCode::
                    CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND));
            return;
          }
          LOG(ERROR) << "Authentication factor not found: " << label;
          std::move(on_done).Run(MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionFactorNotFoundInMultiLabelAuthAuthFactor),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_KEY_NOT_FOUND));
          return;
        }

        // Ensure that if an auth factor is found, the requested type matches
        // what we have on disk for the user.
        if (*request_auth_factor_type !=
            stored_auth_factor->auth_factor().type()) {
          LOG(ERROR)
              << "Unexpected mismatch in type from label and auth_input.";
          std::move(on_done).Run(MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionMultiLabelMismatchedAuthTypes),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_INVALID_ARGUMENT));
          return;
        }

        std::optional<AuthBlockType> cur_auth_block_type =
            auth_block_utility_->GetAuthBlockTypeFromState(
                stored_auth_factor->auth_factor().auth_block_state());
        if (!cur_auth_block_type.has_value()) {
          LOG(ERROR) << "Failed to determine auth block type.";
          std::move(on_done).Run(MakeStatus<CryptohomeCryptoError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionInvalidBlockTypeInAuthAuthFactor),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
              CryptoError::CE_OTHER_CRYPTO));
          return;
        }
        if (auth_block_type.has_value()) {
          if (cur_auth_block_type.value() != auth_block_type.value()) {
            LOG(ERROR) << "Unexpected mismatch in auth block types in auth "
                          "factor candidates.";
            std::move(on_done).Run(MakeStatus<CryptohomeCryptoError>(
                CRYPTOHOME_ERR_LOC(
                    kLocAuthSessionMismatchedBlockTypesInAuthAuthFactor),
                ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
                CryptoError::CE_OTHER_CRYPTO));
            return;
          }
        } else {
          auth_block_type = cur_auth_block_type.value();
        }

        // Perform the storage type check here because we want to directly call
        // AuthenticateViaUserSecretStash later on.
        if (stored_auth_factor->storage_type() !=
            AuthFactorStorageType::kUserSecretStash) {
          LOG(ERROR) << "Multiple label arity auth factors are only supported "
                        "with USS storage type.";
          std::move(on_done).Run(MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionMultiLabelInvalidStorageType),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_INVALID_ARGUMENT));
          return;
        }

        auth_factors.push_back(stored_auth_factor->auth_factor());
      }
      // auth_block_type is guaranteed to be non-null because we've checked
      // auth_factor_labels's length above, and auth_block_type must be set in
      // the first iteration of the loop.
      CHECK(auth_block_type.has_value());

      CryptohomeStatusOr<AuthInput> auth_input =
          CreateAuthInputForSelectFactor(*request_auth_factor_type);
      if (!auth_input.ok()) {
        std::move(on_done).Run(
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocAuthSessionAuthInputParseFailed4InAuthAuthFactor))
                .Wrap(std::move(auth_input).err_status()));
        return;
      }

      // Record current time for timing for how long AuthenticateAuthFactor will
      // take.
      auto auth_session_performance_timer =
          std::make_unique<AuthSessionPerformanceTimer>(
              kAuthSessionAuthenticateAuthFactorUSSTimer);
      auth_block_utility_->SelectAuthFactorWithAuthBlock(
          auth_block_type.value(), auth_input.value(), std::move(auth_factors),
          base::BindOnce(&AuthSession::AuthenticateViaSelectedAuthFactor,
                         weak_factory_.GetWeakPtr(),
                         std::move(auth_factor_type_user_policy),
                         std::move(on_done),
                         std::move(auth_session_performance_timer)));
      return;
    }
  }
}

void AuthSession::AuthForDecrypt::RemoveAuthFactor(
    const user_data_auth::RemoveAuthFactorRequest& request,
    StatusCallback on_done) {
  auto remove_timer_start = base::TimeTicks::Now();
  const auto& auth_factor_label = request.auth_factor_label();

  std::optional<AuthFactorMap::ValueView> stored_auth_factor =
      session_->auth_factor_map_.Find(auth_factor_label);
  if (!stored_auth_factor) {
    LOG(ERROR) << "AuthSession: Key to remove not found: " << auth_factor_label;
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInRemoveAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  on_done = WrapCallbackWithMetricsReporting(
      std::move(on_done), stored_auth_factor->auth_factor().type(),
      kCryptohomeErrorRemoveAuthFactorErrorBucket);

  if (session_->auth_factor_map_.size() == 1) {
    LOG(ERROR) << "AuthSession: Cannot remove the last auth factor.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionLastFactorInRemoveAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_REMOVE_CREDENTIALS_FAILED));
    return;
  }

  // Authenticated |vault_keyset_| of the current session (backup VaultKeyset or
  // regular VaultKeyset) cannot be removed.
  if (session_->vault_keyset_ &&
      auth_factor_label == session_->vault_keyset_->GetLabel()) {
    LOG(ERROR) << "AuthSession: Cannot remove the authenticated VaultKeyset.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionRemoveSameVKInRemoveAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_REMOVE_CREDENTIALS_FAILED));
    return;
  }

  bool remove_using_vk =
      !session_->decrypted_uss_ ||
      stored_auth_factor->storage_type() == AuthFactorStorageType::kVaultKeyset;

  if (!remove_using_vk) {
    session_->RemoveAuthFactorViaUserSecretStash(
        auth_factor_label, stored_auth_factor->auth_factor(),
        base::BindOnce(&AuthSession::ClearAuthFactorInMemoryObjects,
                       base::Unretained(session_), auth_factor_label,
                       *stored_auth_factor, remove_timer_start,
                       std::move(on_done)));
    return;
  }

  // Remove the VaultKeyset with the given label if it exists from disk
  // regardless of its purpose, i.e backup, regular or migrated. Error is
  // ignored if remove_using_uss was true as the keyset that matters is now
  // deleted.
  CryptohomeStatus remove_status =
      RemoveKeysetByLabel(*session_->keyset_management_,
                          session_->obfuscated_username_, auth_factor_label);
  if (remove_using_vk && !remove_status.ok() &&
      stored_auth_factor->auth_factor().type() !=
          AuthFactorType::kCryptohomeRecovery) {
    LOG(ERROR) << "AuthSession: Failed to remove VaultKeyset.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionRemoveVKFailedInRemoveAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_REMOVE_CREDENTIALS_FAILED));
    return;
  }

  // Remove the AuthFactor from the map.
  session_->auth_factor_map_.Remove(auth_factor_label);
  session_->verifier_forwarder_.ReleaseVerifier(auth_factor_label);

  // Report time taken for a successful remove.
  ReportTimerDuration(kAuthSessionRemoveAuthFactorVKTimer, remove_timer_start,
                      "" /*append_string*/);
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::PrepareUserForRemoval() {
  // All auth factors of the user are being removed when we remove the user, so
  // we should PrepareForRemoval() all auth factors.
  for (AuthFactorMap::ValueView stored_auth_factor : auth_factor_map_) {
    const AuthFactor& auth_factor = stored_auth_factor.auth_factor();
    auto log_status = [](const AuthFactor& auth_factor,
                         CryptohomeStatus remove_status) {
      if (!remove_status.ok()) {
        LOG(WARNING) << "Failed to prepare auth factor " << auth_factor.label()
                     << " for removal: " << remove_status;
      }
    };
    auth_block_utility_->PrepareAuthBlockForRemoval(
        obfuscated_username_, auth_factor.auth_block_state(),
        base::BindOnce(log_status, auth_factor));
  }

  // Remove rate-limiter here, as it won't be removed by any auth factor's
  // removal.
  RemoveRateLimiters();
}

void AuthSession::RemoveRateLimiters() {
  // Currently fingerprint is the only auth factor type using rate
  // limiter, so the field name isn't generic. We'll make it generic to any
  // auth factor types in the future.
  CryptohomeStatusOr<EncryptedUss> encrypted_uss =
      EncryptedUss::FromStorage(uss_storage_);
  if (!encrypted_uss.ok()) {
    LOG(WARNING) << "Failed to load the user metadata.";
    return;
  }
  if (!encrypted_uss->fingerprint_rate_limiter_id().has_value()) {
    return;
  }
  if (!crypto_->RemoveLECredential(
          *encrypted_uss->fingerprint_rate_limiter_id())) {
    LOG(WARNING) << "Failed to remove rate-limiter leaf.";
  }
}

void AuthSession::ClearAuthFactorInMemoryObjects(
    const std::string& auth_factor_label,
    const AuthFactorMap::ValueView& stored_auth_factor,
    const base::TimeTicks& remove_timer_start,
    StatusCallback on_done,
    CryptohomeStatus status) {
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to remove auth factor.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionRemoveAuthFactorViaUserSecretStashFailed),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  // Attempt to remove the keyset with the given label regardless if it
  // exists. Error is logged and ignored.
  CryptohomeStatus remove_status = RemoveKeysetByLabel(
      *keyset_management_, obfuscated_username_, auth_factor_label);
  if (!remove_status.ok() && stored_auth_factor.auth_factor().type() !=
                                 AuthFactorType::kCryptohomeRecovery) {
    LOG(INFO) << "AuthSession: Failed to remove VaultKeyset in USS auth "
                 "factor removal.";
  }

  // Remove the AuthFactor from the map.
  auth_factor_map_.Remove(auth_factor_label);
  verifier_forwarder_.ReleaseVerifier(auth_factor_label);
  ReportTimerDuration(kAuthSessionRemoveAuthFactorUSSTimer, remove_timer_start,
                      "" /*append_string*/);
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::RemoveAuthFactorViaUserSecretStash(
    const std::string& auth_factor_label,
    const AuthFactor& auth_factor,
    StatusCallback on_done) {
  // Preconditions.
  CHECK(decrypted_uss_);

  auth_factor_manager_->RemoveAuthFactor(
      obfuscated_username_, auth_factor, auth_block_utility_,
      base::BindOnce(&AuthSession::ResaveUssWithFactorRemoved,
                     base::Unretained(this), auth_factor_label, auth_factor,
                     std::move(on_done)));
}

void AuthSession::ResaveUssWithFactorRemoved(
    const std::string& auth_factor_label,
    const AuthFactor& auth_factor,
    StatusCallback on_done,
    CryptohomeStatus status) {
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to remove auth factor.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionRemoveFactorFailedInRemoveAuthFactor),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  // At any step after this point if we fail in updating the USS we still report
  // OkStatus as the final result. The AuthFactor itself is already gone and so
  // no matter how the rest of the cleanup goes the removal has happened.

  {
    auto transaction = decrypted_uss_->StartTransaction();
    if (auto status = transaction.RemoveWrappingId(auth_factor_label);
        !status.ok()) {
      LOG(ERROR) << "AuthSession: Failed to remove auth factor from user "
                    "secret stash: "
                 << status;
      std::move(on_done).Run(OkStatus<CryptohomeError>());
      return;
    }
    if (auto status = std::move(transaction).Commit(); !status.ok()) {
      LOG(ERROR)
          << "AuthSession: Failed to persist user secret stash after auth "
             "factor removal: "
          << status;
      std::move(on_done).Run(OkStatus<CryptohomeError>());
      return;
    }
  }

  // Removal from USS completed.
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthForDecrypt::UpdateAuthFactor(
    const user_data_auth::UpdateAuthFactorRequest& request,
    StatusCallback on_done) {
  if (request.auth_factor_label().empty()) {
    LOG(ERROR) << "AuthSession: Old auth factor label is empty.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoOldLabelInUpdateAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  std::optional<AuthFactorMap::ValueView> stored_auth_factor =
      session_->auth_factor_map_.Find(request.auth_factor_label());
  if (!stored_auth_factor) {
    LOG(ERROR) << "AuthSession: Key to update not found: "
               << request.auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInUpdateAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  if (!AuthFactorPropertiesFromProto(request.auth_factor(),
                                     *session_->features_, auth_factor_type,
                                     auth_factor_label, auth_factor_metadata)) {
    LOG(ERROR)
        << "AuthSession: Failed to parse updated auth factor parameters.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnknownFactorInUpdateAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Auth factor label has to be the same as before.
  if (request.auth_factor_label() != auth_factor_label) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDifferentLabelInUpdateAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Auth factor type has to be the same as before.
  if (stored_auth_factor->auth_factor().type() != auth_factor_type) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionDifferentTypeInUpdateAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Determine the auth block type to use.
  const AuthFactorDriver& factor_driver =
      session_->auth_factor_driver_manager_->GetDriver(auth_factor_type);
  CryptoStatusOr<AuthBlockType> auth_block_type =
      session_->auth_block_utility_->SelectAuthBlockTypeForCreation(
          factor_driver.block_types());
  if (!auth_block_type.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionInvalidBlockTypeInUpdateAuthFactor),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
            .Wrap(std::move(auth_block_type).status()));
    return;
  }

  // Create and initialize fields for auth_input.
  CryptohomeStatusOr<AuthInput> auth_input_status =
      session_->CreateAuthInputForAdding(request.auth_input(), auth_factor_type,
                                         auth_factor_metadata);
  if (!auth_input_status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionNoInputInUpdateAuthFactor))
            .Wrap(std::move(auth_input_status).err_status()));
    return;
  }

  // Report timer for how long UpdateAuthFactor operation takes.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          stored_auth_factor->storage_type() ==
                  AuthFactorStorageType::kUserSecretStash
              ? kAuthSessionUpdateAuthFactorUSSTimer
              : kAuthSessionUpdateAuthFactorVKTimer,
          auth_block_type.value());
  auth_session_performance_timer->auth_block_type = auth_block_type.value();

  KeyData key_data;
  // AuthFactorMetadata is needed for only smartcards. Since
  // UpdateAuthFactor doesn't operate on smartcards pass an empty metadata,
  // which is not going to be used.
  user_data_auth::CryptohomeErrorCode error =
      session_->converter_.AuthFactorToKeyData(
          auth_factor_label, auth_factor_type, auth_factor_metadata, key_data);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET &&
      auth_factor_type != AuthFactorType::kCryptohomeRecovery) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionConverterFailsInUpdateFactorViaVK),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}), error));
    return;
  }

  auto create_callback = session_->GetUpdateAuthFactorCallback(
      auth_factor_type, auth_factor_label, auth_factor_metadata, key_data,
      auth_input_status.value(), stored_auth_factor->storage_type(),
      std::move(auth_session_performance_timer), std::move(on_done));

  session_->auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      auth_block_type.value(), auth_input_status.value(),
      std::move(create_callback));
}

AuthBlock::CreateCallback AuthSession::GetUpdateAuthFactorCallback(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const KeyData& key_data,
    const AuthInput& auth_input,
    const AuthFactorStorageType auth_factor_storage_type,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done) {
  switch (auth_factor_storage_type) {
    case AuthFactorStorageType::kUserSecretStash:
      return base::BindOnce(&AuthSession::UpdateAuthFactorViaUserSecretStash,
                            weak_factory_.GetWeakPtr(), auth_factor_type,
                            auth_factor_label, auth_factor_metadata, auth_input,
                            std::move(auth_session_performance_timer),
                            std::move(on_done));

    case AuthFactorStorageType::kVaultKeyset:
      return base::BindOnce(&AuthSession::MigrateToUssDuringUpdateVaultKeyset,
                            weak_factory_.GetWeakPtr(), auth_factor_type,
                            auth_factor_label, auth_factor_metadata, key_data,
                            auth_input, std::move(on_done));
  }
}

void AuthSession::UpdateAuthFactorViaUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  // Check the status of the callback error, to see if the key blob creation was
  // actually successful.
  if (!callback_error.ok() || !key_blobs || !auth_block_state) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInUpdateViaUSS),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
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

  // Create the auth factor by combining the metadata with the auth block
  // state.
  AuthFactor auth_factor(auth_factor_type, auth_factor_label,
                         auth_factor_metadata, *auth_block_state);

  // Update/persist the factor.
  auth_factor_manager_->UpdateAuthFactor(
      obfuscated_username_, auth_factor_label, auth_factor, auth_block_utility_,
      base::BindOnce(
          &AuthSession::ResaveUssWithFactorUpdated, base::Unretained(this),
          auth_factor_type, auth_factor, std::move(key_blobs), auth_input,
          std::move(auth_session_performance_timer), std::move(on_done)));
}

void AuthSession::ResaveUssWithFactorUpdated(
    AuthFactorType auth_factor_type,
    AuthFactor auth_factor,
    std::unique_ptr<KeyBlobs> key_blobs,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptohomeStatus status) {
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

  {
    auto transaction = decrypted_uss_->StartTransaction();

    // Overwrite the existing factor with the new one.
    if (auto status =
            AddAuthFactorToUssTransaction(auth_factor, *key_blobs, transaction);
        !status.ok()) {
      LOG(ERROR)
          << "AuthSession: Failed to add updated auth factor secret to USS.";
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionAddToUSSFailedInUpdateViaUSS),
              user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }

    // Persist the USS.
    // It's important to do this after persisting the factor, to minimize the
    // chance of ending in an inconsistent state on the disk: a created/updated
    // USS and a missing auth factor (note that we're using file system syncs to
    // have best-effort ordering guarantee).
    if (auto status = std::move(transaction).Commit(); !status.ok()) {
      LOG(ERROR)
          << "Failed to persist user secret stash after auth factor creation";
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionPersistUSSFailedInUpdateViaUSS),
              user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }
  }

  // Create the credential verifier if applicable.
  AddCredentialVerifier(auth_factor_type, auth_factor.label(), auth_input);

  LOG(INFO) << "AuthSession: updated auth factor " << auth_factor.label()
            << " in USS.";
  auth_factor_map_.Add(std::move(auth_factor),
                       AuthFactorStorageType::kUserSecretStash);
  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::UpdateAuthFactorMetadata(
    const user_data_auth::UpdateAuthFactorMetadataRequest request,
    StatusCallback on_done) {
  if (request.auth_factor_label().empty()) {
    LOG(ERROR) << "AuthSession: UpdateAuthFactorMetadata request contains "
                  "empty auth factor label.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoLabelInUpdateAuthFactorMetadata),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  std::optional<AuthFactorMap::ValueView> stored_auth_factor =
      auth_factor_map_.Find(request.auth_factor_label());
  if (!stored_auth_factor) {
    LOG(ERROR) << "AuthSession: UpdateAuthFactorMetadata's to-be-updated auth "
                  "factor not found, label: "
               << request.auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionFactorNotFoundInUpdateAuthFactorMetadata),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  if (!AuthFactorPropertiesFromProto(request.auth_factor(), *features_,
                                     auth_factor_type, auth_factor_label,
                                     auth_factor_metadata)) {
    LOG(ERROR)
        << "AuthSession: Failed to parse updated auth factor parameters.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionUnknownFactorInUpdateAuthFactorMetadata),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Auth factor label has to be the same as before.
  if (request.auth_factor_label() != auth_factor_label) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionDifferentLabelInUpdateAuthFactorMetadata),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Auth factor type has to be the same as before.
  if (stored_auth_factor->auth_factor().type() != auth_factor_type) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionDifferentTypeInUpdateAuthFactorMetadata),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  if (auth_factor_metadata.common.user_specified_name.size() >
      kUserSpecifiedNameSizeLimit) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionNameTooLongInUpdateAuthFactorMetadata),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Build the new auth factor with existing auth block state.
  auto auth_factor = std::make_unique<AuthFactor>(
      auth_factor_type, auth_factor_label, auth_factor_metadata,
      stored_auth_factor.value().auth_factor().auth_block_state());
  // Update/persist the factor.
  auto status = auth_factor_manager_->SaveAuthFactorFile(obfuscated_username_,
                                                         *auth_factor);
  if (!status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to save updated auth factor: " << status;
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionFailedSaveInUpdateAuthFactorMetadata))
            .Wrap(std::move(status)));
    return;
  }
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthForDecrypt::RelabelAuthFactor(
    const user_data_auth::RelabelAuthFactorRequest& request,
    StatusCallback on_done) {
  // For ephemeral users we can do a relabel in-memory using only the verifiers.
  if (session_->is_ephemeral_user_) {
    RelabelAuthFactorEphemeral(request, std::move(on_done));
    return;
  }

  // Get the existing auth factor and make sure it's not a vault keyset.
  if (request.auth_factor_label().empty()) {
    LOG(ERROR) << "AuthSession: Old auth factor label is empty.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoOldLabelInRelabelAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  std::optional<AuthFactor> old_auth_factor;
  {
    std::optional<AuthFactorMap::ValueView> stored_auth_factor =
        session_->auth_factor_map_.Find(request.auth_factor_label());
    if (!stored_auth_factor) {
      LOG(ERROR) << "AuthSession: Key to update not found: "
                 << request.auth_factor_label();
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInRelabelAuthFactor),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
      return;
    }
    if (stored_auth_factor->storage_type() ==
        AuthFactorStorageType::kVaultKeyset) {
      LOG(ERROR) << "AuthSession: Vault keyset factors cannot be relabelled: "
                 << request.auth_factor_label();
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthSessionFactorIsVaultKeysetInRelabelAuthFactor),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
      return;
    }
    old_auth_factor = stored_auth_factor->auth_factor();
  }

  // Check that the new label is valid and does not already exist.
  if (!IsValidAuthFactorLabel(request.new_auth_factor_label())) {
    LOG(ERROR) << "AuthSession: New auth factor label is not valid: "
               << request.new_auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidNewLabelInRelabelAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  if (session_->auth_factor_map_.Find(request.new_auth_factor_label())) {
    LOG(ERROR) << "AuthSession: New auth factor label already exists: "
               << request.new_auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionNewLabelAlreadyExistsInRelabelAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Create a copy of the existing factor with the new label and save it. Add a
  // cleanup to undo this if we fail, which we'll cancel if we succeed instead.
  AuthFactor new_auth_factor(
      old_auth_factor->type(), request.new_auth_factor_label(),
      old_auth_factor->metadata(), old_auth_factor->auth_block_state());
  if (auto status = session_->auth_factor_manager_->SaveAuthFactorFile(
          session_->obfuscated_username_, new_auth_factor);
      !status.ok()) {
    LOG(ERROR) << "AuthSession: Unable to save a new copy of the auth factor.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionSaveCopyFailedInRelabelAuthFactor))
            .Wrap(std::move(status)));
    return;
  }
  absl::Cleanup delete_new_aff = [this, &new_auth_factor]() {
    if (auto status = session_->auth_factor_manager_->DeleteAuthFactorFile(
            session_->obfuscated_username_, new_auth_factor);
        !status.ok()) {
      LOG(ERROR)
          << "AuthSession: Unable to delete the auth_factor file with the "
             "new label: "
          << new_auth_factor.label() << ": " << status;
    }
  };

  // Update the USS to move the wrapped key to the new label.
  {
    auto transaction = session_->decrypted_uss_->StartTransaction();
    if (auto status = transaction.RenameWrappingId(
            request.auth_factor_label(), request.new_auth_factor_label());
        !status.ok()) {
      // This shouldn't actually ever happen because we've already checked for
      // collisions but just in case, we still need to handle it.
      LOG(ERROR) << "AuthSession: Unable to rename the factor in USS.";
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionRenameWrappedKeyFailedInRelabelAuthFactor),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
              user_data_auth::CRYPTOHOME_RELABEL_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }
    if (auto status = std::move(transaction).Commit(); !status.ok()) {
      LOG(ERROR)
          << "Failed to persist user secret stash after changing labels from: "
          << request.auth_factor_label()
          << " to: " << request.new_auth_factor_label();
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionPersistUssFailedInRelabelAuthFactor),
              user_data_auth::CRYPTOHOME_RELABEL_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }
  }
  std::move(delete_new_aff).Cancel();
  if (auto verifier = session_->verifier_forwarder_.ReleaseVerifier(
          old_auth_factor->label())) {
    verifier->ChangeLabel(new_auth_factor.label());
    session_->verifier_forwarder_.AddVerifier(std::move(verifier));
  }
  session_->auth_factor_map_.Remove(old_auth_factor->label());
  session_->auth_factor_map_.Add(std::move(new_auth_factor),
                                 AuthFactorStorageType::kUserSecretStash);
  LOG(INFO) << "AuthSession: relabelled auth factor "
            << old_auth_factor->label() << " to "
            << request.new_auth_factor_label();

  // At this point the relabel is committed. If any subsequent cleanup steps
  // fail they don't fail the Relabel operation.

  // Try to clean up the leftover auth factor files.
  if (auto status = session_->auth_factor_manager_->DeleteAuthFactorFile(
          session_->obfuscated_username_, *old_auth_factor);
      !status.ok()) {
    LOG(ERROR) << "AuthSession: Unable to delete the leftover file from the "
                  "original label: "
               << request.auth_factor_label() << ": " << status;
  }

  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthForDecrypt::ReplaceAuthFactor(
    const user_data_auth::ReplaceAuthFactorRequest& request,
    StatusCallback on_done) {
  // For ephemeral users we can do a replace in-memory using only the verifiers.
  if (session_->is_ephemeral_user_) {
    ReplaceAuthFactorEphemeral(request, std::move(on_done));
    return;
  }

  // Report timer for how long ReplaceAuthFactor takes.
  auto perf_timer = std::make_unique<AuthSessionPerformanceTimer>(
      kAuthSessionReplaceAuthFactorTimer);

  // Get the existing auth factor and make sure it's not a vault keyset.
  if (request.auth_factor_label().empty()) {
    LOG(ERROR) << "AuthSession: Old auth factor label is empty.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoOldLabelInReplaceAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  std::optional<AuthFactor> original_auth_factor;
  {
    std::optional<AuthFactorMap::ValueView> stored_auth_factor =
        session_->auth_factor_map_.Find(request.auth_factor_label());
    if (!stored_auth_factor) {
      LOG(ERROR) << "AuthSession: Key to update not found: "
                 << request.auth_factor_label();
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInReplaceAuthFactor),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
      return;
    }
    if (stored_auth_factor->storage_type() ==
        AuthFactorStorageType::kVaultKeyset) {
      LOG(ERROR) << "AuthSession: Vault keyset factors cannot be replaced: "
                 << request.auth_factor_label();
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthSessionFactorIsVaultKeysetInReplaceAuthFactor),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
      return;
    }
    original_auth_factor = stored_auth_factor->auth_factor();
  }

  // Check that the new label is valid and does not already exist.
  if (!IsValidAuthFactorLabel(request.auth_factor().label())) {
    LOG(ERROR) << "AuthSession: New auth factor label is not valid: "
               << request.auth_factor().label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidNewLabelInReplaceAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  if (session_->auth_factor_map_.Find(request.auth_factor().label())) {
    LOG(ERROR) << "AuthSession: New auth factor label already exists: "
               << request.auth_factor().label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionNewLabelAlreadyExistsInReplaceAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Construct the auth factor properties for the replacement.
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  if (!AuthFactorPropertiesFromProto(request.auth_factor(),
                                     *session_->features_, auth_factor_type,
                                     auth_factor_label, auth_factor_metadata)) {
    LOG(ERROR)
        << "AuthSession: Failed to parse updated auth factor parameters.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnknownFactorInReplaceAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  AuthFactorDriver& factor_driver =
      session_->auth_factor_driver_manager_->GetDriver(auth_factor_type);

  // Construct an auth factor input for the replacement.
  CryptohomeStatusOr<AuthInput> auth_input = session_->CreateAuthInputForAdding(
      request.auth_input(), auth_factor_type, auth_factor_metadata);
  if (!auth_input.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionNoInputInReplaceAuthFactor))
            .Wrap(std::move(auth_input).err_status()));
    return;
  }

  // Determine the auth block type to use.
  CryptoStatusOr<AuthBlockType> auth_block_type =
      session_->auth_block_utility_->SelectAuthBlockTypeForCreation(
          factor_driver.block_types());
  if (!auth_block_type.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionInvalidBlockTypeInReplaceAuthFactor),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
            .Wrap(std::move(auth_block_type).status()));
    return;
  }

  // Move onto key blob creation for the replacement.
  auto create_callback = base::BindOnce(
      &AuthSession::AuthForDecrypt::ReplaceAuthFactorIntoUss,
      weak_factory_.GetWeakPtr(), std::move(*original_auth_factor), *auth_input,
      auth_factor_type, std::move(auth_factor_label),
      std::move(auth_factor_metadata), std::move(perf_timer),
      std::move(on_done));
  session_->auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      *auth_block_type, *auth_input, std::move(create_callback));
}

void AuthSession::AuthForDecrypt::RelabelAuthFactorEphemeral(
    const user_data_auth::RelabelAuthFactorRequest& request,
    StatusCallback on_done) {
  // Check that there is a verifier with the existing label.
  if (!session_->verifier_forwarder_.HasVerifier(request.auth_factor_label())) {
    LOG(ERROR) << "AuthSession: Key to update not found: "
               << request.auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionFactorNotFoundInRelabelAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  // Check that the new label is valid and does not already exist.
  if (!IsValidAuthFactorLabel(request.new_auth_factor_label())) {
    LOG(ERROR) << "AuthSession: New auth factor label is not valid: "
               << request.new_auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionInvalidNewLabelInRelabelAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  if (session_->verifier_forwarder_.HasVerifier(
          request.new_auth_factor_label())) {
    LOG(ERROR) << "AuthSession: New auth factor label already exists: "
               << request.new_auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionNewLabelAlreadyExistsInRelabelAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Release, rename and re-add the existing verifier.
  auto verifier = session_->verifier_forwarder_.ReleaseVerifier(
      request.auth_factor_label());
  verifier->ChangeLabel(request.new_auth_factor_label());
  session_->verifier_forwarder_.AddVerifier(std::move(verifier));
  LOG(INFO) << "AuthSession: relabelled credential verifier from "
            << request.auth_factor_label() << " to "
            << request.new_auth_factor_label();
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthForDecrypt::ReplaceAuthFactorEphemeral(
    const user_data_auth::ReplaceAuthFactorRequest& request,
    StatusCallback on_done) {
  // Check that there is a verifier with the existing label.
  if (!session_->verifier_forwarder_.HasVerifier(request.auth_factor_label())) {
    LOG(ERROR) << "AuthSession: Key to update not found: "
               << request.auth_factor_label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionFactorNotFoundInReplaceAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  // Check that the new label is valid and does not already exist.
  if (!IsValidAuthFactorLabel(request.auth_factor().label())) {
    LOG(ERROR) << "AuthSession: New auth factor label is not valid: "
               << request.auth_factor().label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionInvalidNewLabelInReplaceAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  if (session_->verifier_forwarder_.HasVerifier(
          request.auth_factor().label())) {
    LOG(ERROR) << "AuthSession: New auth factor label already exists: "
               << request.auth_factor().label();
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionNewLabelAlreadyExistsInReplaceAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Construct the auth factor properties for the replacement.
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  if (!AuthFactorPropertiesFromProto(request.auth_factor(),
                                     *session_->features_, auth_factor_type,
                                     auth_factor_label, auth_factor_metadata)) {
    LOG(ERROR)
        << "AuthSession: Failed to parse updated auth factor parameters.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionUnknownFactorInReplaceAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  AuthFactorDriver& factor_driver =
      session_->auth_factor_driver_manager_->GetDriver(auth_factor_type);

  // Construct an auth factor input for the replacement.
  CryptohomeStatusOr<AuthInput> auth_input = session_->CreateAuthInputForAdding(
      request.auth_input(), auth_factor_type, auth_factor_metadata);
  if (!auth_input.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionNoInputInReplaceAuthFactorEphemeral))
            .Wrap(std::move(auth_input).err_status()));
    return;
  }

  // Create the replacement verifier.
  auto replacement_verifier =
      factor_driver.CreateCredentialVerifier(auth_factor_label, *auth_input);
  if (!replacement_verifier) {
    LOG(ERROR) << "AuthSession: Unable to create replacement verifier.";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionNoReplacementInReplaceAuthFactorEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_REPLACE_CREDENTIALS_FAILED));
    return;
  }

  // Release, rename and re-add the existing verifier.
  // Release the existing verifier and add the replacement.
  session_->verifier_forwarder_.ReleaseVerifier(request.auth_factor_label());
  session_->verifier_forwarder_.AddVerifier(std::move(replacement_verifier));
  LOG(INFO) << "AuthSession: replaced credential verifier from "
            << request.auth_factor_label() << " with "
            << request.auth_factor().label();
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthForDecrypt::ReplaceAuthFactorIntoUss(
    AuthFactor original_auth_factor,
    AuthInput auth_input,
    AuthFactorType auth_factor_type,
    std::string auth_factor_label,
    AuthFactorMetadata auth_factor_metadata,
    std::unique_ptr<AuthSessionPerformanceTimer> perf_timer,
    StatusCallback on_done,
    CryptohomeStatus error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  // Fail the operation if the Create operation failed or provided no results.
  if (!error.ok() || !key_blobs || !auth_block_state) {
    if (error.ok()) {
      error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInReplaceAfIntoUss),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlob creation failed before persisting USS and "
                  "auth factor with label: "
               << auth_factor_label;
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInReplaceAfIntoUss),
            user_data_auth::CRYPTOHOME_REPLACE_CREDENTIALS_FAILED)
            .Wrap(std::move(error)));
    return;
  }
  AuthFactor replacement_auth_factor(auth_factor_type, auth_factor_label,
                                     auth_factor_metadata, *auth_block_state);

  // Set up a cleanup operation to remove one of the auth factors. This will
  // start out configured to remove the replacement factor, but once the
  // replacement is done it will be switched to clean up the old factor.
  AuthFactor* factor_to_remove = &replacement_auth_factor;
  absl::Cleanup remove_leftover_factor = [this, &factor_to_remove]() {
    // Note that this runs after the operation (on_done) has completed
    // (successfully or not) and so the remove operation just takes a do-nothing
    // callback and we ignore any resulting errors since there's nothing we can
    // do about them at this point.
    session_->auth_factor_manager_->RemoveAuthFactor(
        session_->obfuscated_username_, *factor_to_remove,
        session_->auth_block_utility_, base::DoNothing());
  };

  {
    auto transaction = session_->decrypted_uss_->StartTransaction();

    // Add the new factor into the USS and remove the old one.
    if (CryptohomeStatus status = session_->AddAuthFactorToUssTransaction(
            replacement_auth_factor, *key_blobs, transaction);
        !status.ok()) {
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionAddToUssFailedInReplaceAfIntoUss),
              user_data_auth::CRYPTOHOME_REPLACE_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }
    if (CryptohomeStatus status =
            transaction.RemoveWrappingId(original_auth_factor.label());
        !status.ok()) {
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionRemoveFromUssFailedInReplaceAfIntoUss),
              user_data_auth::CRYPTOHOME_REPLACE_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }

    // Persist the new factor files out.
    if (CryptohomeStatus status =
            session_->auth_factor_manager_->SaveAuthFactorFile(
                session_->obfuscated_username_, replacement_auth_factor);
        !status.ok()) {
      LOG(ERROR) << "Failed to persist replacement auth factor: "
                 << auth_factor_label;
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionPersistFactorFailedInReplaceAfIntoUss),
              user_data_auth::CRYPTOHOME_REPLACE_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }

    // Write out the new USS with the new factor added and the original one
    // removed. If this succeeds the then Replace operation is committed and the
    // overall operation is "complete" once we do all the in-memory swaps.
    if (CryptohomeStatus status = std::move(transaction).Commit();
        !status.ok()) {
      LOG(ERROR) << "Failed to persist user secret stash after the creation of "
                    "auth factor with label: "
                 << auth_factor_label;
      std::move(on_done).Run(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocAuthSessionPersistUssFailedInReplaceAfIntoUss),
              user_data_auth::CRYPTOHOME_REPLACE_CREDENTIALS_FAILED)
              .Wrap(std::move(status)));
      return;
    }
  }

  ReportTimerDuration(perf_timer.get());
  factor_to_remove = &original_auth_factor;
  session_->verifier_forwarder_.ReleaseVerifier(original_auth_factor.label());
  session_->AddCredentialVerifier(auth_factor_type, auth_factor_label,
                                  auth_input);
  session_->auth_factor_map_.Remove(original_auth_factor.label());
  session_->auth_factor_map_.Add(std::move(replacement_auth_factor),
                                 AuthFactorStorageType::kUserSecretStash);
  LOG(INFO) << "AuthSession: replaced auth factor "
            << original_auth_factor.label() << " with new auth factor "
            << auth_factor_label;
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::PrepareAuthFactor(
    const user_data_auth::PrepareAuthFactorRequest& request,
    StatusCallback on_done) {
  std::optional<AuthFactorType> auth_factor_type =
      AuthFactorTypeFromProto(request.auth_factor_type());
  if (!auth_factor_type.has_value()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionInvalidAuthFactorTypeInPrepareAuthFactor),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }

  on_done = WrapCallbackWithMetricsReporting(
      std::move(on_done), *auth_factor_type,
      kCryptohomeErrorPrepareAuthFactorErrorBucket);

  AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(*auth_factor_type);

  std::optional<AuthFactorPreparePurpose> purpose =
      AuthFactorPreparePurposeFromProto(request.purpose());
  if (!purpose.has_value()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidPurposeInPrepareAuthFactor),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }

  if (factor_driver.IsPrepareRequired()) {
    switch (*purpose) {
      case AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor: {
        factor_driver.PrepareForAuthenticate(
            obfuscated_username_,
            base::BindOnce(&AuthSession::OnPrepareAuthFactorDone,
                           weak_factory_.GetWeakPtr(), std::move(on_done)));
        break;
      }
      case AuthFactorPreparePurpose::kPrepareAddAuthFactor: {
        auto* session_decrypt = GetAuthForDecrypt();
        if (!session_decrypt) {
          CryptohomeStatus status = MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocAuthSessionUnauthedInPrepareForAdd),
              ErrorActionSet({PossibleAction::kAuth}),
              user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
          std::move(on_done).Run(std::move(status));
          return;
        }
        session_decrypt->PrepareAuthFactorForAdd(*auth_factor_type,
                                                 std::move(on_done));
        break;
      }
    }

    // If this type of factor supports label-less verifiers, then create one.
    if (auto verifier = factor_driver.CreateCredentialVerifier({}, {})) {
      verifier_forwarder_.AddVerifier(std::move(verifier));
    }
  } else {
    // For auth factor types that do not require PrepareAuthFactor,
    // return an invalid argument error.
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionPrepareBadAuthFactorType),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
  }
}

void AuthSession::AuthForDecrypt::PrepareAuthFactorForAdd(
    AuthFactorType auth_factor_type, StatusCallback on_done) {
  AuthFactorDriver& factor_driver =
      session_->auth_factor_driver_manager_->GetDriver(auth_factor_type);

  if (!session_->decrypted_uss_) {
    // Currently PrepareAuthFactor is only supported for USS.
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoUSSInPrepareAuthFactorForAdd),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }
  if (factor_driver.NeedsRateLimiter()) {
    CryptohomeStatus status = factor_driver.TryCreateRateLimiter(
        session_->obfuscated_username_, *session_->decrypted_uss_);
    if (!status.ok()) {
      std::move(on_done).Run(std::move(status));
      return;
    }
  }
  factor_driver.PrepareForAdd(
      session_->obfuscated_username_,
      base::BindOnce(&AuthSession::OnPrepareAuthFactorDone,
                     session_->weak_factory_.GetWeakPtr(), std::move(on_done)));
}

void AuthSession::OnPrepareAuthFactorDone(
    StatusCallback on_done,
    CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>> token) {
  if (token.ok()) {
    AuthFactorType type = (*token)->auth_factor_type();
    active_auth_factor_tokens_[type] = std::move(*token);
    std::move(on_done).Run(OkStatus<CryptohomeError>());
  } else {
    std::move(on_done).Run(std::move(token).status());
  }
}

void AuthSession::TerminateAuthFactor(
    const user_data_auth::TerminateAuthFactorRequest& request,
    StatusCallback on_done) {
  std::optional<AuthFactorType> auth_factor_type =
      AuthFactorTypeFromProto(request.auth_factor_type());
  if (!auth_factor_type.has_value()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionInvalidAuthFactorTypeInTerminateAuthFactor),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(*auth_factor_type);

  // For auth factor types that do not need Prepare, neither do they need
  // Terminate, return an invalid argument error.
  if (!factor_driver.IsPrepareRequired()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionTerminateBadAuthFactorType),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Throw error if the auth factor is not in the active list.
  auto iter = active_auth_factor_tokens_.find(*auth_factor_type);
  if (iter == active_auth_factor_tokens_.end()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionTerminateInactiveAuthFactor),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Terminate the auth factor and remove it from the active list. We do this
  // removal even if termination fails.
  CryptohomeStatus status = iter->second->Terminate();
  active_auth_factor_tokens_.erase(iter);
  verifier_forwarder_.ReleaseVerifier(*auth_factor_type);
  std::move(on_done).Run(std::move(status));
}

AuthSession::AuthForDecrypt* AuthSession::GetAuthForDecrypt() {
  return auth_for_decrypt_ ? &*auth_for_decrypt_ : nullptr;
}

AuthSession::AuthForVerifyOnly* AuthSession::GetAuthForVerifyOnly() {
  return auth_for_verify_only_ ? &*auth_for_verify_only_ : nullptr;
}

AuthSession::AuthForWebAuthn* AuthSession::GetAuthForWebAuthn() {
  return auth_for_web_authn_ ? &*auth_for_web_authn_ : nullptr;
}

void AuthSession::GetRecoveryRequest(
    user_data_auth::GetRecoveryRequestRequest request,
    base::OnceCallback<void(const user_data_auth::GetRecoveryRequestReply&)>
        on_done) {
  user_data_auth::GetRecoveryRequestReply reply;

  // Check the factor exists.
  std::optional<AuthFactorMap::ValueView> stored_auth_factor =
      auth_factor_map_.Find(request.auth_factor_label());
  if (!stored_auth_factor) {
    LOG(ERROR) << "Authentication key not found: "
               << request.auth_factor_label();
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionFactorNotFoundInGetRecoveryRequest),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  // Read CryptohomeRecoveryAuthBlockState.
  if (stored_auth_factor->auth_factor().type() !=
      AuthFactorType::kCryptohomeRecovery) {
    LOG(ERROR) << "GetRecoveryRequest can be called only for "
                  "kCryptohomeRecovery auth factor";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocWrongAuthFactorInGetRecoveryRequest),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }

  auto* state = std::get_if<::cryptohome::CryptohomeRecoveryAuthBlockState>(
      &(stored_auth_factor->auth_factor().auth_block_state().state));
  if (!state) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocNoRecoveryAuthBlockStateInGetRecoveryRequest),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
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
    if (status->local_legacy_error().has_value()) {
      // Note: the error format should match `cryptohome_recovery_failure` in
      // crash-reporter/anomaly_detector.cc
      LOG(ERROR) << "Cryptohome Recovery GetRecoveryRequest failure, error = "
                 << status->local_legacy_error().value();
    }
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCryptoFailedInGenerateRecoveryRequest))
            .Wrap(std::move(status)));
    return;
  }

  cryptohome_recovery_ephemeral_pub_key_ = ephemeral_pub_key;
  reply.set_recovery_request(recovery_request.to_string());
  std::move(on_done).Run(reply);
}

AuthBlockType AuthSession::ResaveVaultKeysetIfNeeded(
    const std::optional<brillo::SecureBlob> user_input,
    AuthBlockType auth_block_type) {
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
    return auth_block_type;
  }

  // KeyBlobs needs to be re-created since there maybe a change in the
  // AuthBlock type with the change in TPM state. Don't abort on failure.
  // Only password and pin type credentials are evaluated for resave.
  if (vault_keyset_->IsLECredential()) {
    LOG(ERROR) << "Pinweaver AuthBlock is not supported for resave operation, "
                  "can't resave keyset.";
    return auth_block_type;
  }
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(AuthFactorType::kPassword);
  CryptoStatusOr<AuthBlockType> out_auth_block_type =
      auth_block_utility_->SelectAuthBlockTypeForCreation(
          factor_driver.block_types());
  if (!out_auth_block_type.ok()) {
    LOG(ERROR)
        << "Error in creating obtaining AuthBlockType, can't resave keyset: "
        << out_auth_block_type.status();
    return auth_block_type;
  }

  // Create and initialize fields for AuthInput.
  AuthInput auth_input = {.user_input = user_input,
                          .locked_to_single_user = std::nullopt,
                          .username = username_,
                          .obfuscated_username = obfuscated_username_,
                          .reset_secret = std::nullopt,
                          .reset_seed = std::nullopt,
                          .rate_limiter_label = std::nullopt,
                          .cryptohome_recovery_auth_input = std::nullopt,
                          .challenge_credential_auth_input = std::nullopt,
                          .fingerprint_auth_input = std::nullopt};

  AuthBlock::CreateCallback create_callback =
      base::BindOnce(&AuthSession::ResaveKeysetOnKeyBlobsGenerated,
                     base::Unretained(this), std::move(updated_vault_keyset));
  auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      out_auth_block_type.value(), auth_input,
      /*CreateCallback*/ std::move(create_callback));

  return out_auth_block_type.value();
}

void AuthSession::ResaveKeysetOnKeyBlobsGenerated(
    VaultKeyset updated_vault_keyset,
    CryptohomeStatus error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  if (!error.ok() || key_blobs == nullptr || auth_block_state == nullptr) {
    LOG(ERROR) << "Error in creating KeyBlobs, can't resave keyset.";
    return;
  }

  CryptohomeStatus status = keyset_management_->ReSaveKeyset(
      updated_vault_keyset, std::move(*key_blobs), std::move(auth_block_state));
  // Updated ketyset is saved on the disk, it is safe to update
  // |vault_keyset_|.
  vault_keyset_ = std::make_unique<VaultKeyset>(updated_vault_keyset);
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
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  return std::move(auth_input.value());
}

CryptohomeStatusOr<AuthInput> AuthSession::CreateAuthInputForMigration(
    const AuthInput& auth_input, AuthFactorType auth_factor_type) {
  AuthInput migration_auth_input = auth_input;

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  if (!factor_driver.NeedsResetSecret()) {
    // The factor is not resettable, so no extra data needed to be filled.
    return std::move(migration_auth_input);
  }

  if (!vault_keyset_) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocNoVkInAuthInputForMigration),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  // After successful authentication `reset_secret` is available in the
  // decrypted LE VaultKeyset, if the authenticated VaultKeyset is LE.
  brillo::SecureBlob reset_secret = vault_keyset_->GetResetSecret();
  if (!reset_secret.empty()) {
    LOG(INFO) << "Reset secret is obtained from PIN VaultKeyset with label: "
              << vault_keyset_->GetLabel();
    migration_auth_input.reset_secret = std::move(reset_secret);
    return std::move(migration_auth_input);
  }

  // Update of an LE VaultKeyset can happen only after authenticating with a
  // password VaultKeyset, which stores the password VaultKeyset in
  // |vault_keyset_|.
  return UpdateAuthInputWithResetParamsFromPasswordVk(auth_input,
                                                      *vault_keyset_);
}

CryptohomeStatusOr<AuthInput> AuthSession::CreateAuthInputForAdding(
    const user_data_auth::AuthInput& auth_input_proto,
    AuthFactorType auth_factor_type,
    const AuthFactorMetadata& auth_factor_metadata) {
  // Convert the proto to a basic AuthInput.
  std::optional<AuthInput> auth_input = CreateAuthInput(
      platform_, auth_input_proto, username_, obfuscated_username_,
      auth_block_utility_->GetLockedToSingleUser(),
      cryptohome_recovery_ephemeral_pub_key_, auth_factor_metadata);
  if (!auth_input.has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocCreateFailedInAuthInputForAdd),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Delegate the rest of the construction to the other overload.
  return CreateAuthInputForAdding(*std::move(auth_input), auth_factor_type,
                                  auth_factor_metadata);
}

CryptohomeStatusOr<AuthInput> AuthSession::CreateAuthInputForAdding(
    AuthInput auth_input,
    AuthFactorType auth_factor_type,
    const AuthFactorMetadata& auth_factor_metadata) {
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);

  // Types which need rate-limiters are exclusive with those which need
  // per-label reset secrets.
  if (factor_driver.NeedsRateLimiter() && decrypted_uss_) {
    // Currently fingerprint is the only auth factor type using rate limiter, so
    // the interface isn't designed to be generic. We'll make it generic to any
    // auth factor types in the future.
    std::optional<uint64_t> rate_limiter_label =
        decrypted_uss_->encrypted().fingerprint_rate_limiter_id();
    std::optional<brillo::SecureBlob> reset_secret =
        decrypted_uss_->GetRateLimiterResetSecret(auth_factor_type);
    if (!rate_limiter_label.has_value() || !reset_secret.has_value()) {
      LOG(ERROR) << "No existing rate-limiter.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocRateLimiterNoRateLimiterInAuthInputForAdd),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    auth_input.rate_limiter_label = rate_limiter_label;
    auth_input.reset_secret = reset_secret;
    return std::move(auth_input);
  }

  if (factor_driver.NeedsResetSecret()) {
    if (decrypted_uss_) {
      // When using USS, every resettable factor gets a unique reset secret.
      // When USS is not backed up by VaultKeysets this secret needs to be
      // generated independently.
      LOG(INFO) << "Adding random reset secret for UserSecretStash.";
      auth_input.reset_secret =
          CreateSecureRandomBlob(kCryptohomeResetSecretLength);
      return std::move(auth_input);
    }

    // When using VaultKeyset, reset is implemented via a seed that's shared
    // among all of the user's VKs. Hence copy it from the previously loaded VK.
    if (!vault_keyset_) {
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocNoVkInAuthInputForAdd),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }

    return UpdateAuthInputWithResetParamsFromPasswordVk(auth_input,
                                                        *vault_keyset_);
  }

  return std::move(auth_input);
}

CryptohomeStatusOr<AuthInput> AuthSession::CreateAuthInputForSelectFactor(
    AuthFactorType auth_factor_type) {
  AuthInput auth_input{};

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  if (factor_driver.NeedsRateLimiter()) {
    // Load the USS to get the raw user metadata directly.
    CryptohomeStatusOr<EncryptedUss> encrypted_uss =
        EncryptedUss::FromStorage(uss_storage_);
    if (!encrypted_uss.ok()) {
      LOG(ERROR) << "Failed to load the user metadata.";
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthSessionGetMetadataFailedInAuthInputForSelect),
                 user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
          .Wrap(std::move(encrypted_uss).err_status());
    }

    // Currently fingerprint is the only auth factor type using rate
    // limiter, so the field name isn't generic. We'll make it generic to any
    // auth factor types in the future.
    if (!encrypted_uss->fingerprint_rate_limiter_id().has_value()) {
      LOG(ERROR) << "No rate limiter ID in user metadata.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNoRateLimiterInAuthInputForSelect),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kAuth}),
          user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    }

    auth_input.rate_limiter_label =
        *encrypted_uss->fingerprint_rate_limiter_id();
  }

  return auth_input;
}

CredentialVerifier* AuthSession::AddCredentialVerifier(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input) {
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  if (auto new_verifier = factor_driver.CreateCredentialVerifier(
          auth_factor_label, auth_input)) {
    auto* return_ptr = new_verifier.get();
    verifier_forwarder_.AddVerifier(std::move(new_verifier));
    return return_ptr;
  }
  verifier_forwarder_.ReleaseVerifier(auth_factor_label);
  return nullptr;
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
  if (high == 0 && low == 0) {
    LOG(ERROR) << "AuthSession: all-zeroes serialized token is invalid";
    return std::nullopt;
  }
  return base::UnguessableToken::Deserialize(high, low);
}

std::optional<ChallengeCredentialAuthInput>
AuthSession::CreateChallengeCredentialAuthInput(
    const cryptohome::AuthorizationRequest& authorization) {
  // There should only ever have 1 challenge response key in the request
  // and having 0 or more than 1 element is considered invalid.
  if (authorization.key().data().challenge_response_key_size() != 1) {
    return std::nullopt;
  }
  if (!authorization.has_key_delegate() ||
      !authorization.key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response operation without key "
                  "delegate information";
    return std::nullopt;
  }

  const ChallengePublicKeyInfo& public_key_info =
      authorization.key().data().challenge_response_key(0);
  auto struct_public_key_info = cryptohome::proto::FromProto(public_key_info);
  return ChallengeCredentialAuthInput{
      .public_key_spki_der = struct_public_key_info.public_key_spki_der,
      .challenge_signature_algorithms =
          struct_public_key_info.signature_algorithm,
      .dbus_service_name = authorization.key_delegate().dbus_service_name(),
  };
}

void AuthSession::PersistAuthFactorToUserSecretStash(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  CryptohomeStatus status = PersistAuthFactorToUserSecretStashImpl(
      auth_factor_type, auth_factor_label, auth_factor_metadata, auth_input,
      std::move(auth_session_performance_timer), std::move(callback_error),
      std::move(key_blobs), std::move(auth_block_state));

  std::move(on_done).Run(std::move(status));
}

void AuthSession::PersistAuthFactorToUserSecretStashOnMigration(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done,
    CryptohomeStatus pre_migration_status,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  // During the migration existing VaultKeyset should be recreated with the
  // backup VaultKeyset logic.
  CryptohomeStatus status = PersistAuthFactorToUserSecretStashImpl(
      auth_factor_type, auth_factor_label, auth_factor_metadata, auth_input,
      std::move(auth_session_performance_timer), std::move(callback_error),
      std::move(key_blobs), std::move(auth_block_state));
  if (!status.ok()) {
    LOG(ERROR) << "USS migration of VaultKeyset with label "
               << auth_factor_label << " is failed: " << status;
    ReapAndReportError(std::move(status),
                       kCryptohomeErrorUssMigrationErrorBucket);
    ReportVkToUssMigrationStatus(VkToUssMigrationStatus::kFailedPersist);
    std::move(on_done).Run(std::move(pre_migration_status));
    return;
  }

  std::unique_ptr<VaultKeyset> remove_vk = keyset_management_->GetVaultKeyset(
      obfuscated_username_, auth_factor_label);
  if (!remove_vk || !keyset_management_->RemoveKeysetFile(*remove_vk).ok()) {
    LOG(ERROR)
        << "USS migration of VaultKeyset with label " << auth_factor_label
        << " is completed, but failed removing the migrated VaultKeyset.";
    ReportVkToUssMigrationStatus(
        VkToUssMigrationStatus::kFailedRecordingMigrated);
    std::move(on_done).Run(std::move(pre_migration_status));
    return;
  }

  LOG(INFO) << "USS migration completed for VaultKeyset with label: "
            << auth_factor_label;
  ReportVkToUssMigrationStatus(VkToUssMigrationStatus::kSuccess);
  std::move(on_done).Run(std::move(pre_migration_status));
}

CryptohomeStatus AuthSession::PersistAuthFactorToUserSecretStashImpl(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  // Check the status of the callback error, to see if the key blob creation was
  // actually successful.
  if (!callback_error.ok() || !key_blobs || !auth_block_state) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInPersistToUSS),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlob creation failed before persisting USS and "
                  "auth factor with label: "
               << auth_factor_label;
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthSessionCreateFailedInPersistToUSS),
               user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
        .Wrap(std::move(callback_error));
  }

  // Create the auth factor by combining the metadata with the auth block state.
  AuthFactor auth_factor(auth_factor_type, auth_factor_label,
                         auth_factor_metadata, *auth_block_state);

  {
    auto transaction = decrypted_uss_->StartTransaction();

    // Add the factor into the USS.
    if (auto status =
            AddAuthFactorToUssTransaction(auth_factor, *key_blobs, transaction);
        !status.ok()) {
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthSessionAddToUssFailedInPersistToUSS),
                 user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
          .Wrap(std::move(status));
    }

    // Persist the factor.
    // It's important to do this after all the non-persistent steps so that we
    // only start writing files after all validity checks (like the label
    // duplication check).
    if (auto status = auth_factor_manager_->SaveAuthFactorFile(
            obfuscated_username_, auth_factor);
        !status.ok()) {
      LOG(ERROR) << "Failed to persist created auth factor: "
                 << auth_factor_label;
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthSessionPersistFactorFailedInPersistToUSS),
                 user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
          .Wrap(std::move(status));
    }

    // Persist the USS.
    // It's important to do this after persisting the factor, to minimize the
    // chance of ending in an inconsistent state on the disk: a created/updated
    // USS and a missing auth factor (note that we're using file system syncs to
    // have best-effort ordering guarantee).
    if (auto status = std::move(transaction).Commit(); !status.ok()) {
      LOG(ERROR) << "Failed to persist user secret stash after the creation of "
                    "auth factor with label: "
                 << auth_factor_label;
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthSessionPersistUSSFailedInPersistToUSS),
                 user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
          .Wrap(std::move(status));
    }
  }

  // If a USS only factor is added backup keysets should be removed.
  if (!IsFactorTypeSupportedByVk(auth_factor_type)) {
    CryptohomeStatus cleanup_status = CleanUpAllBackupKeysets(
        *keyset_management_, obfuscated_username_, auth_factor_map_);
    if (!cleanup_status.ok()) {
      LOG(ERROR) << "Cleaning up backup keysets failed: " << cleanup_status;
    }
  }

  AddCredentialVerifier(auth_factor_type, auth_factor.label(), auth_input);

  LOG(INFO) << "AuthSession: added auth factor " << auth_factor.label()
            << " into USS.";
  auth_factor_map_.Add(std::move(auth_factor),
                       AuthFactorStorageType::kUserSecretStash);

  // Report timer for how long AuthSession operation takes.
  ReportTimerDuration(auth_session_performance_timer.get());
  return OkStatus<CryptohomeError>();
}

void AuthSession::CompleteVerifyOnlyAuthentication(
    AuthenticateAuthFactorCallback on_done,
    AuthenticateAuthFactorRequest original_request,
    AuthFactorType auth_factor_type,
    CryptohomeStatus error) {
  // If there was no error then the verify was a success.
  if (error.ok()) {
    // Verify-only authentication might satisfy the kWebAuthn AuthIntent for the
    // legacy FP AuthFactorType. In fact, that is the only possible scenario
    // where we reach here with the kWebAuthn AuthIntent.
    if (auth_intent_ == AuthIntent::kWebAuthn) {
      SetAuthorizedForIntents({AuthIntent::kVerifyOnly, AuthIntent::kWebAuthn});
    } else {
      SetAuthorizedForIntents({AuthIntent::kVerifyOnly});
    }
    const AuthFactorDriver& factor_driver =
        auth_factor_driver_manager_->GetDriver(auth_factor_type);
    // There is at least 1 AuthFactor that needs full auth to reset, and the
    // current auth factor used for authentication supports repeating full auth.
    if (factor_driver.IsFullAuthRepeatable() &&
        NeedsFullAuthForReset(factor_driver.GetResetCapability())) {
      original_request.flags.force_full_auth = ForceFullAuthFlag::kForce;
      PostAuthAction action{
          .action_type = PostAuthActionType::kRepeat,
          .repeat_request = std::move(original_request),
      };
      std::move(on_done).Run(action, std::move(error));
      return;
    }
  }
  // Forward whatever the result was to on_done.
  std::move(on_done).Run(kNoPostAction, std::move(error));
}

CryptohomeStatus AuthSession::AddAuthFactorToUssTransaction(
    AuthFactor& auth_factor,
    const KeyBlobs& key_blobs,
    DecryptedUss::Transaction& transaction) {
  // Derive the credential secret for the USS from the key blobs.
  std::optional<brillo::SecureBlob> uss_credential_secret =
      key_blobs.DeriveUssCredentialSecret();
  if (!uss_credential_secret.has_value()) {
    LOG(ERROR) << "AuthSession: Failed to derive credential secret for "
                  "updated auth factor.";
    // TODO(b/229834676): Migrate USS and wrap the error.
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionDeriveUSSSecretFailedInAddSecretToUSS),
        ErrorActionSet({PossibleAction::kReboot, PossibleAction::kRetry,
                        PossibleAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED);
  }

  // This wraps the USS Main Key with the credential secret. The wrapping_id
  // field is defined equal to the factor's label.
  if (auto status = transaction.AssignWrappedMainKey(auth_factor.label(),
                                                     *uss_credential_secret);
      !status.ok()) {
    LOG(ERROR) << "AuthSession: Failed to add created auth factor into user "
                  "secret stash.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionAddMainKeyFailedInAddSecretToUSS),
               user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
        .Wrap(std::move(status));
  }

  // Types which need rate-limiters are exclusive with those which need
  // per-label reset secrets.
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor.type());

  if (factor_driver.NeedsResetSecret() && key_blobs.reset_secret.has_value()) {
    if (auto status = transaction.AssignResetSecret(auth_factor.label(),
                                                    *key_blobs.reset_secret);
        !status.ok()) {
      LOG(ERROR)
          << "AuthSession: Failed to insert reset secret for auth factor.";
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthSessionAddResetSecretFailedInAddSecretToUSS),
                 ErrorActionSet(
                     {PossibleAction::kReboot, PossibleAction::kRetry}),
                 user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
          .Wrap(std::move(status));
    }
  }

  return OkStatus<CryptohomeError>();
}

void AuthSession::AuthForDecrypt::AddAuthFactor(
    const user_data_auth::AddAuthFactorRequest& request,
    StatusCallback on_done) {
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  if (!AuthFactorPropertiesFromProto(request.auth_factor(),
                                     *session_->features_, auth_factor_type,
                                     auth_factor_label, auth_factor_metadata)) {
    LOG(ERROR) << "Failed to parse new auth factor parameters";
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionUnknownFactorInAddAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  on_done = WrapCallbackWithMetricsReporting(
      std::move(on_done), auth_factor_type,
      kCryptohomeErrorAddAuthFactorErrorBucket);

  // You cannot add an auth factor with a label if one already exists.
  if (session_->auth_factor_map_.Find(auth_factor_label)) {
    LOG(ERROR) << "Cannot add a new auth factor when one already exists: "
               << auth_factor_label;
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorAlreadyExistsInAddAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  CryptohomeStatusOr<AuthInput> auth_input_status =
      session_->CreateAuthInputForAdding(request.auth_input(), auth_factor_type,
                                         auth_factor_metadata);
  if (!auth_input_status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionNoInputInAddAuthFactor))
            .Wrap(std::move(auth_input_status).err_status()));
    return;
  }

  if (session_->is_ephemeral_user_) {
    // If AuthSession is configured as an ephemeral user, then we do not save
    // the key to the disk.
    session_->AddAuthFactorForEphemeral(auth_factor_type, auth_factor_label,
                                        auth_input_status.value(),
                                        std::move(on_done));
    return;
  }

  // Report timer for how long AddAuthFactor operation takes.
  auto auth_session_performance_timer =
      session_->decrypted_uss_ ? std::make_unique<AuthSessionPerformanceTimer>(
                                     kAuthSessionAddAuthFactorUSSTimer)
                               : std::make_unique<AuthSessionPerformanceTimer>(
                                     kAuthSessionAddAuthFactorVKTimer);

  // Determine the auth block type to use.
  const AuthFactorDriver& factor_driver =
      session_->auth_factor_driver_manager_->GetDriver(auth_factor_type);
  CryptoStatusOr<AuthBlockType> auth_block_type =
      session_->auth_block_utility_->SelectAuthBlockTypeForCreation(
          factor_driver.block_types());
  if (!auth_block_type.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInAddAuthFactor),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
            .Wrap(std::move(auth_block_type).status()));
    return;
  }

  // Parameterize timer by AuthBlockType.
  auth_session_performance_timer->auth_block_type = auth_block_type.value();

  KeyData key_data;
  user_data_auth::CryptohomeErrorCode error =
      session_->converter_.AuthFactorToKeyData(
          auth_factor_label, auth_factor_type, auth_factor_metadata, key_data);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET &&
      auth_factor_type != AuthFactorType::kCryptohomeRecovery &&
      auth_factor_type != AuthFactorType::kFingerprint) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionVKConverterFailsInAddAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}), error));
    return;
  }

  AuthFactorStorageType auth_factor_storage_type =
      session_->decrypted_uss_ ? AuthFactorStorageType::kUserSecretStash
                               : AuthFactorStorageType::kVaultKeyset;

  auto create_callback = session_->GetAddAuthFactorCallback(
      auth_factor_type, auth_factor_label, auth_factor_metadata, key_data,
      auth_input_status.value(), auth_factor_storage_type,
      std::move(auth_session_performance_timer), std::move(on_done));

  session_->auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      auth_block_type.value(), auth_input_status.value(),
      std::move(create_callback));
}

AuthBlock::CreateCallback AuthSession::GetAddAuthFactorCallback(
    const AuthFactorType& auth_factor_type,
    const std::string& auth_factor_label,
    const AuthFactorMetadata& auth_factor_metadata,
    const KeyData& key_data,
    const AuthInput& auth_input,
    const AuthFactorStorageType auth_factor_storage_type,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    StatusCallback on_done) {
  switch (auth_factor_storage_type) {
    case AuthFactorStorageType::kUserSecretStash:
      return base::BindOnce(&AuthSession::PersistAuthFactorToUserSecretStash,
                            weak_factory_.GetWeakPtr(), auth_factor_type,
                            auth_factor_label, auth_factor_metadata, auth_input,
                            std::move(auth_session_performance_timer),
                            std::move(on_done));

    case AuthFactorStorageType::kVaultKeyset:
      return base::BindOnce(&AuthSession::CreateAndPersistVaultKeyset,
                            weak_factory_.GetWeakPtr(), key_data, auth_input,
                            std::move(auth_session_performance_timer),
                            std::move(on_done));
  }
}

void AuthSession::AddAuthFactorForEphemeral(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    StatusCallback on_done) {
  CHECK(is_ephemeral_user_);

  if (!auth_input.user_input.has_value()) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocNoUserInputInAddFactorForEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  if (verifier_forwarder_.HasVerifier(auth_factor_label)) {
    // Overriding the verifier for a given label is not supported.
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVerifierAlreadySetInAddFactorForEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  CredentialVerifier* verifier =
      AddCredentialVerifier(auth_factor_type, auth_factor_label, auth_input);
  // Check whether the verifier creation failed.
  if (!verifier) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVerifierSettingErrorInAddFactorForEphemeral),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthenticateViaUserSecretStash(
    const std::string& auth_factor_label,
    const AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    const AuthFactor& auth_factor,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    StatusCallback on_done) {
  // Determine the auth block type to use.
  // TODO(b/223207622): This step is the same for both USS and VaultKeyset other
  // than how the AuthBlock state is obtained, they can be merged.
  std::optional<AuthBlockType> auth_block_type =
      auth_block_utility_->GetAuthBlockTypeFromState(
          auth_factor.auth_block_state());
  if (!auth_block_type) {
    LOG(ERROR) << "Failed to determine auth block type for the loaded factor "
                  "with label "
               << auth_factor.label();
    std::move(on_done).Run(MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionInvalidBlockTypeInAuthViaUSS),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO));
    return;
  }

  // Parameterize timer by AuthBlockType.
  auth_session_performance_timer->auth_block_type = *auth_block_type;

  // Derive the keyset and then use USS to complete the authentication.
  auto derive_callback = base::BindOnce(
      &AuthSession::LoadUSSMainKeyAndFsKeyset, weak_factory_.GetWeakPtr(),
      auth_factor.type(), auth_factor_label, auth_input,
      std::move(auth_session_performance_timer), auth_factor_type_user_policy,
      std::move(on_done));
  auth_block_utility_->DeriveKeyBlobsWithAuthBlock(
      *auth_block_type, auth_input, auth_factor.auth_block_state(),
      std::move(derive_callback));
}

void AuthSession::AuthenticateViaSingleFactor(
    const AuthFactorType& request_auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    const AuthFactorMetadata& metadata,
    const AuthFactorMap::ValueView& stored_auth_factor,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    StatusCallback on_done) {
  // If this auth factor comes from USS, run the USS flow.
  if (stored_auth_factor.storage_type() ==
      AuthFactorStorageType::kUserSecretStash) {
    // Record current time for timing for how long AuthenticateAuthFactor will
    // take.
    auto auth_session_performance_timer =
        std::make_unique<AuthSessionPerformanceTimer>(
            kAuthSessionAuthenticateAuthFactorUSSTimer);

    AuthenticateViaUserSecretStash(auth_factor_label, auth_input,
                                   std::move(auth_session_performance_timer),
                                   stored_auth_factor.auth_factor(),
                                   auth_factor_type_user_policy,
                                   std::move(on_done));
    return;
  }

  // If user does not have USS AuthFactors, then we switch to authentication
  // with Vaultkeyset. Status is flipped on the successful authentication.
  user_data_auth::CryptohomeErrorCode error = converter_.PopulateKeyDataForVK(
      obfuscated_username_, auth_factor_label, key_data_);
  if (error != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to authenticate auth session via vk-factor "
               << auth_factor_label;
    // TODO(b/229834676): Migrate The USS VKK converter then wrap the error.
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionVKConverterFailedInAuthAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}), error));
    return;
  }
  // Record current time for timing for how long AuthenticateAuthFactor will
  // take.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAuthenticateAuthFactorVKTimer);

  // Note that we pass in the auth factor type derived from the client request,
  // instead of ones from the AuthFactor, because legacy VKs could not contain
  // the auth factor type.
  AuthenticateViaVaultKeysetAndMigrateToUss(
      request_auth_factor_type, auth_factor_label, auth_input, metadata,
      std::move(auth_session_performance_timer), auth_factor_type_user_policy,
      std::move(on_done));
}

void AuthSession::AuthenticateViaSelectedAuthFactor(
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    StatusCallback on_done,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    CryptohomeStatus callback_error,
    std::optional<AuthInput> auth_input,
    std::optional<AuthFactor> auth_factor) {
  if (!callback_error.ok() || !auth_input.has_value() ||
      !auth_factor.has_value()) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInAuthViaSelected),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "AuthFactor selection failed before deriving KeyBlobs.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionSelectionFailed))
            .Wrap(std::move(callback_error)));
    return;
  }

  AuthenticateViaUserSecretStash(
      auth_factor->label(), auth_input.value(),
      std::move(auth_session_performance_timer), auth_factor.value(),
      auth_factor_type_user_policy, std::move(on_done));
}

void AuthSession::LoadUSSMainKeyAndFsKeyset(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::optional<AuthBlock::SuggestedAction> suggested_action) {
  // Check the status of the callback error, to see if the key blob derivation
  // was actually successful.
  if (!callback_error.ok() || !key_blobs) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNullParamInLoadUSS),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    // The user is locked out. So prepare an AuthFactorStatusUpdateSignal to be
    // sent periodically until the user is not locked out anymore or until the
    // auth session is timed out.
    if (callback_error->local_legacy_error() ==
        user_data_auth::CRYPTOHOME_ERROR_CREDENTIAL_LOCKED) {
      SendAuthFactorStatusUpdateSignal();
    }
    LOG(ERROR) << "KeyBlob derivation failed before loading USS";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveFailedInLoadUSS))
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
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    return;
  }

  // Decrypt the USS payload.
  // This unwraps the USS Main Key with the credential secret, and decrypts the
  // USS payload using the USS Main Key. The wrapping_id field is defined equal
  // to the factor's label.
  CryptohomeStatusOr<DecryptedUss> existing_uss =
      DecryptedUss::FromStorageUsingWrappedKey(uss_storage_, auth_factor_label,
                                               *uss_credential_secret);
  if (!existing_uss.ok()) {
    LOG(ERROR) << "Failed to decrypt the user secret stash";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDecryptUSSFailedInLoadUSS),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED)
            .Wrap(std::move(existing_uss).err_status()));
    return;
  }

  // By this point we know that the GSC works correctly and we were able to
  // successfully decrypt the USS. So, for GSC with updatable firmware, we
  // assume that it is stable (and the GSC can invalidate the old version).
  if (hwsec::Status status = crypto_->GetHwsec()->DeclareTpmFirmwareStable();
      !status.ok()) {
    LOG(WARNING) << "Failed to declare TPM firmware stable: " << status;
  }

  decrypted_uss_ = std::move(*existing_uss);

  // Populate data fields from the USS.
  file_system_keyset_ = decrypted_uss_->file_system_keyset();

  CryptohomeStatus prepare_status = OkStatus<error::CryptohomeError>();
  if (auth_intent_ == AuthIntent::kWebAuthn) {
    // Even if we failed to prepare WebAuthn secret, file system keyset
    // is already populated and we should proceed to set AuthSession as
    // authenticated. Just return the error status at last.
    prepare_status = PrepareWebAuthnSecret();
    if (!prepare_status.ok()) {
      LOG(ERROR) << "Failed to prepare WebAuthn secret: " << prepare_status;
    }
  }

  if (CryptohomeStatus status = PrepareChapsKey(); !status.ok()) {
    LOG(ERROR) << "Failed to prepare chaps key: " << status;
  }

  // Flip the status on the successful authentication.
  SetAuthorizedForFullAuthIntents(auth_factor_type,
                                  auth_factor_type_user_policy);

  // Set the credential verifier for this credential.
  AddCredentialVerifier(auth_factor_type, auth_factor_label, auth_input);

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  // Reset all of the rate limiters and and credential lockouts.
  ResetLECredentials();
  ResetRateLimiterCredentials(factor_driver.GetResetCapability());

  // Backup VaultKeyset of the authenticated factor can still be in disk if
  // the migration is not completed. Break the dependency of the migrated and
  // not-migrated keysets and remove the backup keyset
  if (auth_factor_map_.HasFactorWithStorage(
          AuthFactorStorageType::kVaultKeyset) &&
      keyset_management_->GetVaultKeyset(obfuscated_username_,
                                         auth_factor_label) != nullptr) {
    // This code path runs to cleanup a backup VaultKeyset for a migrated-to-USS
    // factor if it is not cleaned up due to the existence of not-migrated
    // VaultKeyset factors. Report the cleanup result to UMA whether it is (i)
    // success (ii) failure in adding reset_secret, or (iii) failure in removing
    // the keyset file, recording whether is a password or PIN.
    bool should_cleanup_backup_keyset = false;
    if (auth_factor_type != AuthFactorType::kPassword) {
      should_cleanup_backup_keyset = true;
    } else {
      // If there is an unmigrated PIN VaultKeyset we need to calculate the
      // reset_secret from password backup VaultKeyset and not-migrated PIN
      // keyset. In this case reset secret needs to be added to UserSecretStash
      // before removing the backup keysets.
      MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
          keyset_management_->GetValidKeyset(obfuscated_username_,
                                             std::move(*key_blobs.get()),
                                             auth_factor_label);
      if (vk_status.ok()) {
        vault_keyset_ = std::move(vk_status).value();
        if (MigrateResetSecretToUss()) {
          should_cleanup_backup_keyset = true;
        } else {
          ReportBackupKeysetCleanupResult(
              BackupKeysetCleanupResult::kAddResetSecretFailed);
        }
      } else {
        ReportBackupKeysetCleanupResult(
            BackupKeysetCleanupResult::kGetValidKeysetFailed);
      }
    }

    // Cleanup backup VaultKeyset of the authenticated factor.
    if (should_cleanup_backup_keyset) {
      if (CleanUpBackupKeyset(*keyset_management_, obfuscated_username_,
                              auth_factor_label)
              .ok()) {
        ReportBackupKeysetCleanupSucessWithType(auth_factor_type);

      } else {
        ReportBackupKeysetCleanupFileFailureWithType(auth_factor_type);
      }
    }
  }

  // If the derive suggests recreating the factor, attempt to do that. If this
  // fails we ignore the failure and report whatever status we were going to
  // report anyway.
  if (suggested_action == AuthBlock::SuggestedAction::kRecreate) {
    RecreateUssAuthFactor(auth_factor_type, auth_factor_label, auth_input,
                          std::move(auth_session_performance_timer),
                          std::move(prepare_status), std::move(on_done));
  } else {
    ReportTimerDuration(auth_session_performance_timer.get());
    std::move(on_done).Run(std::move(prepare_status));
  }
}

void AuthSession::RecreateUssAuthFactor(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    AuthInput auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    CryptohomeStatus original_status,
    StatusCallback on_done) {
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  CryptoStatusOr<AuthBlockType> auth_block_type =
      auth_block_utility_->SelectAuthBlockTypeForCreation(
          factor_driver.block_types());
  if (!auth_block_type.ok()) {
    LOG(WARNING) << "Unable to update obsolete auth factor, cannot determine "
                    "new block type: "
                 << auth_block_type.err_status();
    ReportRecreateAuthFactorError(std::move(auth_block_type).status(),
                                  auth_factor_type);
    std::move(on_done).Run(std::move(original_status));
    return;
  }

  std::optional<AuthFactorMap::ValueView> stored_auth_factor =
      auth_factor_map_.Find(auth_factor_label);
  if (!stored_auth_factor) {
    auto status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionGetStoredFactorFailedInRecreate),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    LOG(WARNING) << "Unable to update obsolete auth factor, it does not "
                    "seem to exist: "
                 << status;
    ReportRecreateAuthFactorError(std::move(status), auth_factor_type);
    std::move(on_done).Run(std::move(original_status));
    return;
  }
  const AuthFactor& auth_factor = stored_auth_factor->auth_factor();

  CryptohomeStatusOr<AuthInput> auth_input_for_add = CreateAuthInputForAdding(
      std::move(auth_input), auth_factor.type(), auth_factor.metadata());
  if (!auth_input_for_add.ok()) {
    LOG(WARNING) << "Unable to construct an auth input to recreate the factor: "
                 << auth_input_for_add.err_status();
    ReportRecreateAuthFactorError(std::move(auth_input_for_add).status(),
                                  auth_factor_type);
    std::move(on_done).Run(std::move(original_status));
    return;
  }

  // Make an on_done callback for passing in to GetUpdateAuthFactorCallback
  // that ignores the result of the update and instead just sends in the
  // existing prepare_status result that we would've sent if we hadn't tried
  // the Update at all.
  StatusCallback status_callback = base::BindOnce(
      [](CryptohomeStatus original_status, StatusCallback on_done,
         AuthFactorType auth_factor_type, CryptohomeStatus update_status) {
        if (!update_status.ok()) {
          LOG(WARNING) << "Recreating factor with update failed: "
                       << update_status;
          ReportRecreateAuthFactorError(std::move(update_status),
                                        auth_factor_type);
        } else {
          // If we reach here, the recreate operation is successful. If more
          // error locations are added after this point, this needs to be moved.
          ReportRecreateAuthFactorOk(auth_factor_type);
        }
        std::move(on_done).Run(std::move(original_status));
      },
      std::move(original_status), std::move(on_done), auth_factor_type);

  // Attempt to re-create the factor via a Create+Update.
  auto create_callback = base::BindOnce(
      &AuthSession::UpdateAuthFactorViaUserSecretStash,
      weak_factory_.GetWeakPtr(), auth_factor.type(), auth_factor.label(),
      auth_factor.metadata(), *auth_input_for_add,
      std::move(auth_session_performance_timer), std::move(status_callback));
  auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      *auth_block_type, *auth_input_for_add, std::move(create_callback));
}

void AuthSession::ResetLECredentials() {
  brillo::SecureBlob local_reset_seed;
  if (vault_keyset_ && vault_keyset_->HasWrappedResetSeed()) {
    local_reset_seed = vault_keyset_->GetResetSeed();
  }

  if (!decrypted_uss_ && local_reset_seed.empty()) {
    LOG(ERROR)
        << "No user secret stash or VK available to reset LE credentials.";
    return;
  }

  for (AuthFactorMap::ValueView stored_auth_factor : auth_factor_map_) {
    const AuthFactor& auth_factor = stored_auth_factor.auth_factor();

    // Look for only pinweaver backed AuthFactors.
    auto* state = std::get_if<::cryptohome::PinWeaverAuthBlockState>(
        &(auth_factor.auth_block_state().state));
    if (!state) {
      continue;
    }
    // Ensure that the AuthFactor has le_label.
    if (!state->le_label.has_value()) {
      LOG(WARNING) << "PinWeaver AuthBlock State does not have le_label";
      continue;
    }
    // If the LECredential is already at 0 attempts, there is no need to reset
    // it.
    if (crypto_->GetWrongAuthAttempts(state->le_label.value()) == 0) {
      continue;
    }

    brillo::SecureBlob reset_secret;
    std::optional<brillo::SecureBlob> reset_secret_uss;
    // Get the reset secret from the USS for this auth factor label.
    if (decrypted_uss_) {
      reset_secret_uss = decrypted_uss_->GetResetSecret(auth_factor.label());
    }

    if (reset_secret_uss.has_value()) {
      reset_secret = reset_secret_uss.value();
    } else if (!local_reset_seed.empty()) {
      // If USS does not have the reset secret for the auth factor, the reset
      // secret might still be available through VK.
      LOG(INFO) << "Reset secret could not be retrieved through USS for the LE "
                   "Credential with label "
                << auth_factor.label()
                << ". Will try to obtain it with the Vault Keyset reset seed.";
      std::optional<brillo::SecureBlob> reset_secret_vk =
          GetResetSecretFromVaultKeyset(local_reset_seed, obfuscated_username_,
                                        auth_factor.label(),
                                        *keyset_management_);
      if (!reset_secret_vk.has_value()) {
        LOG(WARNING)
            << "Reset secret could not be retrieved through VaultKeyset for "
               "the LE Credential with label "
            << auth_factor.label();
        continue;
      }
      reset_secret = reset_secret_vk.value();
    } else {
      LOG(WARNING)
          << "Reset secret could not be retrieved through USS or "
             "VaultKeyset since UserSecretStash doesn't include a reset "
             "secret and VaultKeyset doesn't include a reset_salt for "
             "the AuthFactor with label "
          << auth_factor.label();
      continue;
    }

    CryptoError error;
    if (!crypto_->ResetLeCredential(state->le_label.value(), reset_secret,
                                    /*strong_reset=*/false, error)) {
      LOG(WARNING) << "Failed to reset an LE credential for "
                   << state->le_label.value() << " with error: " << error;
    }
  }
}

void AuthSession::ResetRateLimiterCredentials(
    AuthFactorDriver::ResetCapability capability) {
  if (!decrypted_uss_) {
    return;
  }
  std::optional<uint64_t> rate_limiter_label =
      decrypted_uss_->encrypted().fingerprint_rate_limiter_id();
  if (!rate_limiter_label.has_value()) {
    return;
  }

  // Currently only fingerprint auth factor has a rate-limiter.
  std::optional<brillo::SecureBlob> reset_secret =
      decrypted_uss_->GetRateLimiterResetSecret(AuthFactorType::kFingerprint);
  if (!reset_secret.has_value()) {
    LOG(WARNING) << "Fingerprint rate-limiter has no reset secret in USS.";
    return;
  }
  CryptoError error;
  bool strong_reset =
      capability ==
      AuthFactorDriver::ResetCapability::kResetWrongAttemptsAndExpiration;
  // The only situation we don't need to reset the rate-limiter leaf is that
  // wrong attempts is zero and expiration shouldn't be reset.
  if (strong_reset ||
      crypto_->GetWrongAuthAttempts(rate_limiter_label.value()) != 0) {
    if (!crypto_->ResetLeCredential(rate_limiter_label.value(),
                                    reset_secret.value(), strong_reset,
                                    error)) {
      LOG(WARNING) << "Failed to reset fingerprint rate-limiter with error: "
                   << error;
    }
  }

  for (AuthFactorMap::ValueView stored_auth_factor : auth_factor_map_) {
    const AuthFactor& auth_factor = stored_auth_factor.auth_factor();

    // Look for only pinweaver backed AuthFactors.
    auto* state = std::get_if<FingerprintAuthBlockState>(
        &(auth_factor.auth_block_state().state));
    if (!state) {
      continue;
    }
    // Ensure that the AuthFactor has le_label.
    if (!state->gsc_secret_label.has_value()) {
      LOG(WARNING)
          << "Fingerprint AuthBlock State does not have gsc_secret_label.";
      continue;
    }
    // If the credential is already at 0 attempts, there is no need to reset
    // it.
    if (crypto_->GetWrongAuthAttempts(state->gsc_secret_label.value()) == 0) {
      continue;
    }
    if (!crypto_->ResetLeCredential(state->gsc_secret_label.value(),
                                    reset_secret.value(),
                                    /*strong_reset=*/false, error)) {
      LOG(WARNING) << "Failed to reset fingerprint credential for "
                   << state->gsc_secret_label.value()
                   << " with error: " << error;
    }
  }
}

bool AuthSession::NeedsFullAuthForReset(
    AuthFactorDriver::ResetCapability capability) {
  // Check if LE credentials need reset.
  for (AuthFactorMap::ValueView stored_auth_factor : auth_factor_map_) {
    const AuthFactor& auth_factor = stored_auth_factor.auth_factor();

    // Look for only pinweaver backed AuthFactors.
    auto* state = std::get_if<::cryptohome::PinWeaverAuthBlockState>(
        &(auth_factor.auth_block_state().state));
    if (!state) {
      continue;
    }
    // Ensure that the AuthFactor has le_label.
    if (!state->le_label.has_value()) {
      LOG(WARNING) << "PinWeaver AuthBlock State does not have le_label";
      continue;
    }
    // If the LECredential isn't at 0 attempts, it needs to be reset.
    if (crypto_->GetWrongAuthAttempts(state->le_label.value()) != 0) {
      return true;
    }
  }

  // Check if the rate-limiter needs reset.
  CryptohomeStatusOr<EncryptedUss> encrypted_uss =
      EncryptedUss::FromStorage(uss_storage_);
  if (!encrypted_uss.ok()) {
    return false;
  }
  if (!encrypted_uss->fingerprint_rate_limiter_id().has_value()) {
    return false;
  }

  // If reset expiration is supported, we should always reset.
  if (capability ==
      AuthFactorDriver::ResetCapability::kResetWrongAttemptsAndExpiration) {
    return true;
  }
  return crypto_->GetWrongAuthAttempts(
             *encrypted_uss->fingerprint_rate_limiter_id()) != 0;
}

std::unique_ptr<brillo::SecureBlob> AuthSession::GetHibernateSecret() {
  const FileSystemKeyset& fs_keyset = file_system_keyset();
  const std::string message(kHibernateSecretHmacMessage);

  return std::make_unique<brillo::SecureBlob>(HmacSha256(
      brillo::SecureBlob::Combine(fs_keyset.Key().fnek, fs_keyset.Key().fek),
      brillo::Blob(message.cbegin(), message.cend())));
}

void AuthSession::AddOnAuthCallback(base::OnceClosure on_auth) {
  // If the session is not authorized, add it to the list of callbacks.
  // Otherwise, just call the callback immediately.
  if (authorized_intents().empty()) {
    on_auth_.push_back(std::move(on_auth));
  } else {
    std::move(on_auth).Run();
  }
}

void AuthSession::SetAuthFactorStatusUpdateCallback(
    const AuthFactorStatusUpdateCallback& callback) {
  auth_factor_status_update_callback_ = callback;
}

CryptohomeStatus AuthSession::PrepareWebAuthnSecret() {
  if (!file_system_keyset_.has_value()) {
    LOG(ERROR) << "No file system keyset when preparing WebAuthn secret.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthSessionPrepareWebAuthnSecretNoFileSystemKeyset),
        ErrorActionSet({error::PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO,
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }
  UserSession* const session = user_session_map_->Find(username_);
  if (!session) {
    LOG(ERROR) << "No user session found when preparing WebAuthn secret.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionPrepareWebAuthnSecretNoUserSession),
        ErrorActionSet({error::PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO,
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }
  session->PrepareWebAuthnSecret(file_system_keyset_->Key().fek,
                                 file_system_keyset_->Key().fnek);
  SetAuthorizedForIntents({AuthIntent::kWebAuthn});
  return OkStatus<CryptohomeCryptoError>();
}

CryptohomeStatus AuthSession::PrepareChapsKey() {
  if (!file_system_keyset_.has_value()) {
    LOG(ERROR) << "No file system keyset when preparing chaps secret.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionPrepareChapsKeyNoFileSystemKeyset),
        ErrorActionSet({error::PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO,
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }

  // Only prepare the chaps key if the user session exist.
  UserSession* const session = user_session_map_->Find(username_);
  if (session) {
    session->PrepareChapsKey(file_system_keyset_->chaps_key());
  }

  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
