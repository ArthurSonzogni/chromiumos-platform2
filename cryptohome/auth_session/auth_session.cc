// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session/auth_session.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <absl/cleanup/cleanup.h>
#include <absl/container/flat_hash_set.h>
#include <base/barrier_closure.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_blocks/recoverable_key_store.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/flatbuffer.h"
#include "cryptohome/auth_factor/label.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/manager.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/prepare_purpose.h"
#include "cryptohome/auth_factor/protobuf.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/with_driver.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_io/auth_input.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/auth_session/protobuf.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/reap.h"
#include "cryptohome/error/reporting.h"
#include "cryptohome/error/utilities.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/fp_migration/legacy_record.h"
#include "cryptohome/fp_migration/utility.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/proto_bindings/auth_factor.pb.h"
#include "cryptohome/recoverable_key_store/type.h"
#include "cryptohome/signature_sealing/structures_proto.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_policy_file.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/migrator.h"
#include "cryptohome/user_secret_stash/storage.h"
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
using hwsec_foundation::CreateRandomBlob;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::HmacSha256Kdf;
using hwsec_foundation::kAesBlockSize;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

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

std::string IntentSetToDebugString(
    const absl::flat_hash_set<AuthIntent>& intents) {
  std::vector<std::string_view> strings;
  strings.reserve(intents.size());
  for (auto intent : intents) {
    strings.push_back(IntentToDebugString(intent));
  }
  return base::JoinString(strings, ",");
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
  out_auth_input.reset_salt = CreateRandomBlob(kAesBlockSize);
  out_auth_input.reset_secret = HmacSha256Kdf(
      out_auth_input.reset_salt.value(), out_auth_input.reset_seed.value());
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
      obfuscated_username, remove_vk->GetIndex());
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
  brillo::Blob reset_salt = vk->GetResetSalt();
  if (reset_salt.empty()) {
    LOG(WARNING) << "Reset salt is empty in VK  with label: " << label;
    return std::nullopt;
  }
  std::optional<brillo::SecureBlob> reset_secret =
      HmacSha256Kdf(reset_salt, reset_seed);
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

// Removes the backup VaultKeysets.
AuthFactorMetadata CreateAuthFactorMetadataForMigration(
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthFactorType auth_factor_type,
    const AsyncInitFeatures* features) {
  AuthFactorMetadata migration_auth_factor_metadata = auth_factor_metadata;

  if (auth_factor_type == AuthFactorType::kPin) {
    // All new PINs should have time-limited lockouts.
    migration_auth_factor_metadata.common.lockout_policy =
        SerializedLockoutPolicy::TIME_LIMITED;
  }
  return migration_auth_factor_metadata;
}

}  // namespace

SerializedUserAuthFactorTypePolicy GetEmptyAuthFactorTypePolicy(
    AuthFactorType type) {
  return SerializedUserAuthFactorTypePolicy(
      {.type = *SerializeAuthFactorType(type),
       .enabled_intents = {},
       .disabled_intents = {}});
}

SerializedUserAuthFactorTypePolicy GetAuthFactorPolicyFromUserPolicy(
    const std::optional<SerializedUserPolicy>& user_policy,
    AuthFactorType auth_factor_type) {
  if (!user_policy.has_value()) {
    return GetEmptyAuthFactorTypePolicy(auth_factor_type);
  }
  for (auto policy : user_policy->auth_factor_type_policy) {
    if (policy.type != std::nullopt &&
        policy.type == SerializeAuthFactorType(auth_factor_type)) {
      return policy;
    }
  }
  return GetEmptyAuthFactorTypePolicy(auth_factor_type);
}

std::unique_ptr<AuthSession> AuthSession::Create(Username account_id,
                                                 CreateOptions options,
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

  // Force a reload of the AuthFactorMap for this session's user. This preserves
  // the original "caching" behavior of in-memory AuthFactor objects from when
  // each session loaded its own copy.
  backing_apis.auth_factor_manager->DiscardAuthFactorMap(obfuscated_username);

  // Assumption here is that keyset_management_ will outlive this AuthSession.
  AuthSession::Params params = {.username = std::move(account_id),
                                .is_ephemeral_user = options.is_ephemeral_user,
                                .intent = options.intent,
                                .auth_factor_status_update_timer =
                                    std::make_unique<base::WallClockTimer>(),
                                .user_exists = user_exists};
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
      uss_manager_(backing_apis.user_secret_stash_manager),
      crypto_(backing_apis.crypto),
      platform_(backing_apis.platform),
      user_session_map_(backing_apis.user_session_map),
      verifier_forwarder_(username_, user_session_map_),
      keyset_management_(backing_apis.keyset_management),
      auth_block_utility_(backing_apis.auth_block_utility),
      auth_factor_driver_manager_(backing_apis.auth_factor_driver_manager),
      auth_factor_manager_(backing_apis.auth_factor_manager),
      fp_migration_utility_(backing_apis.fp_migration_utility),
      features_(backing_apis.features),
      signalling_(std::move(backing_apis.signalling)),
      key_store_cert_provider_(std::move(backing_apis.key_store_cert_provider)),
      converter_(keyset_management_),
      token_(platform_->CreateUnguessableToken()),
      serialized_token_(GetSerializedStringFromToken(token_)),
      public_token_(platform_->CreateUnguessableToken()),
      serialized_public_token_(GetSerializedStringFromToken(public_token_)),
      user_exists_(*params.user_exists) {
  CHECK(!serialized_token_.empty());
  CHECK(auth_factor_status_update_timer_);
  CHECK(uss_manager_);
  CHECK(crypto_);
  CHECK(platform_);
  CHECK(user_session_map_);
  CHECK(keyset_management_);
  CHECK(auth_block_utility_);
  CHECK(auth_factor_manager_);
  CHECK(features_);

  // Record the session start and report standard metrics.
  AuthFactorMap& auth_factor_map = GetAuthFactorMap();
  auth_factor_map.ReportAuthFactorBackingStoreMetrics();
  RecordAuthSessionStart(auth_factor_map);

  // If only USS factors exist, then we should remove all the backups.
  if (!is_ephemeral_user_ && user_exists_ &&
      !auth_factor_map.HasFactorWithStorage(
          AuthFactorStorageType::kVaultKeyset)) {
    CryptohomeStatus cleanup_status = CleanUpAllBackupKeysets(
        *keyset_management_, obfuscated_username_, auth_factor_map);
    if (!cleanup_status.ok()) {
      LOG(WARNING) << "Cleaning up backup keysets failed.";
    }
  }
}

AuthSession::~AuthSession() {
  std::string append_string = is_ephemeral_user_ ? ".Ephemeral" : ".Persistent";
  ReportTimerDuration(kAuthSessionTotalLifetimeTimer,
                      auth_session_creation_time_, append_string);
  ReportTimerDuration(kAuthSessionAuthenticatedLifetimeTimer,
                      authenticated_time_, append_string);
}

absl::flat_hash_set<AuthIntent> AuthSession::authorized_intents() const {
  absl::flat_hash_set<AuthIntent> intents;
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

AuthFactorMap& AuthSession::GetAuthFactorMap() {
  return auth_factor_manager_->GetAuthFactorMap(obfuscated_username_);
}

void AuthSession::RecordAuthSessionStart(
    const AuthFactorMap& auth_factor_map) const {
  std::vector<std::string> factor_labels;
  factor_labels.reserve(auth_factor_map.size());
  for (AuthFactorMap::ValueView item : auth_factor_map) {
    factor_labels.push_back(base::StringPrintf(
        "%s(type %d %s)", item.auth_factor().label().c_str(),
        static_cast<int>(item.auth_factor().type()),
        AuthFactorStorageTypeToDebugString(item.storage_type())));
  }
  std::vector<const CredentialVerifier*> verifiers =
      verifier_forwarder_.GetCredentialVerifiers();
  std::vector<std::string> verifier_labels;
  verifier_labels.reserve(verifiers.size());
  for (const CredentialVerifier* verifier : verifiers) {
    verifier_labels.push_back(
        base::StringPrintf("%s(type %d)", verifier->auth_factor_label().c_str(),
                           static_cast<int>(verifier->auth_factor_type())));
  }
  LOG(INFO) << "AuthSession: started with is_ephemeral_user="
            << is_ephemeral_user_
            << " intent=" << IntentToDebugString(auth_intent_)
            << " user_exists=" << user_exists_
            << " factors=" << base::JoinString(factor_labels, ",")
            << " verifiers=" << base::JoinString(verifier_labels, ",") << ".";
}

void AuthSession::SetAuthorizedForIntents(
    absl::flat_hash_set<AuthIntent> new_authorized_intents) {
  if (new_authorized_intents.empty()) {
    LOG(ERROR) << "Empty intent set cannot be authorized";
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
  absl::flat_hash_set<AuthIntent> authorized_for;
  for (AuthIntent intent : {AuthIntent::kDecrypt, AuthIntent::kVerifyOnly}) {
    if (factor_driver.IsFullAuthSupported(intent) &&
        IsIntentEnabledBasedOnPolicy(factor_driver, intent,
                                     auth_factor_type_user_policy)) {
      authorized_for.insert(intent);
    }
  }

  // Authorize the session for the subset of intents we found.
  SetAuthorizedForIntents(authorized_for);
}

void AuthSession::SendAuthFactorStatusUpdateSignal() {
  // If the auth factor status update callback is not set (testing purposes),
  // then we won't need to send a signal.
  if (!signalling_) {
    LOG(WARNING) << "Signalling interface is not available to the session";
    return;
  }
  UserPolicyFile user_policy_file(platform_,
                                  GetUserPolicyPath(obfuscated_username_));
  if (!user_policy_file.LoadFromFile().ok()) {
    user_policy_file.UpdateUserPolicy(
        SerializedUserPolicy({.auth_factor_type_policy = {}}));
  }
  auto user_policy = user_policy_file.GetUserPolicy();

  for (AuthFactorMap::ValueView item : GetAuthFactorMap()) {
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

    user_data_auth::AuthFactorStatusUpdate status_update;
    user_data_auth::AuthFactorWithStatus& factor_with_status =
        *status_update.mutable_auth_factor_with_status();
    status_update.set_broadcast_id(serialized_public_token_);
    *factor_with_status.mutable_auth_factor() = std::move(*auth_factor_proto);

    absl::flat_hash_set<AuthIntent> supported_intents = GetSupportedIntents(
        obfuscated_username_, auth_factor.type(), *auth_factor_driver_manager_,
        GetAuthFactorPolicyFromUserPolicy(user_policy, auth_factor.type()),
        /*only_light_auth=*/false);
    for (const auto& auth_intent : supported_intents) {
      factor_with_status.add_available_for_intents(
          AuthIntentToProto(auth_intent));
    }

    // Set |time_available_in| field.
    auto delay = driver.GetFactorDelay(obfuscated_username_, auth_factor);
    if (!delay.ok()) {
      // Something is wrong, prefer not to send the signal over filling some
      // default values.
      continue;
    }
    factor_with_status.mutable_status_info()->set_time_available_in(
        delay->is_max() ? std::numeric_limits<uint64_t>::max()
                        : delay->InMilliseconds());

    // Set |time_expiring_in| field.
    base::TimeDelta time_expiring_in = base::TimeDelta::Max();
    if (driver.IsExpirationSupported()) {
      auto expiration_delay =
          driver.GetTimeUntilExpiration(obfuscated_username_, auth_factor);
      if (!expiration_delay.ok()) {
        // Something is wrong, prefer not to send the signal over filling some
        // default values.
        continue;
      }
      time_expiring_in = *expiration_delay;
      factor_with_status.mutable_status_info()->set_time_expiring_in(
          expiration_delay->InMilliseconds());
    } else {
      // `time_expiring_in` in the output proto is set to maximum when the
      // expiration is not supported. Yet we keep the local variable
      // `time_expiring_in` as `0` to make the below delay calculations easier.
      factor_with_status.mutable_status_info()->set_time_expiring_in(
          std::numeric_limits<uint64_t>::max());
    }

    // TOOD(b:365070033): temporary logging: to monitor irregular lockout delay.
    LOG(INFO) << "AuthFactorStatusUpdate:" << " factor: " << auth_factor.label()
              << " available_in (millseconds): " << delay->InMilliseconds();
    signalling_->SendAuthFactorStatusUpdate(status_update);

    // If both delays are zero, then don't schedule another update.
    // If expiration is not supported by the factor delay is the determining
    // parameter in sending another signal.
    if (delay->is_zero() &&
        (time_expiring_in.is_zero() || time_expiring_in.is_max())) {
      continue;
    }
    // Schedule another update after the smallest of |delay|,
    // |time_expiring_in|, and kAuthFactorStatusUpdateDelay, but excluding zero
    // values.
    std::array<base::TimeDelta, 3> delays = {*delay, time_expiring_in,
                                             kAuthFactorStatusUpdateDelay};
    std::sort(delays.begin(), delays.end());
    for (auto d : delays) {
      if (d.is_zero()) {
        continue;
      }
      base::Time next_signal_time = base::Time::Now() + d;
      // Signal is going to fire before the next signal time we want to
      // schedule. Skip the scheduling.
      if (auth_factor_status_update_timer_->IsRunning() &&
          auth_factor_status_update_timer_->desired_run_time() <
              next_signal_time) {
        break;
      }
      auth_factor_status_update_timer_->Start(
          FROM_HERE, next_signal_time,
          base::BindOnce(&AuthSession::SendAuthFactorStatusUpdateSignal,
                         weak_factory_for_timed_tasks_.GetWeakPtr()));
      break;
    }
  }
}

const PrepareOutput* AuthSession::GetFactorTypePrepareOutput(
    AuthFactorType auth_factor_type) const {
  auto iter = active_auth_factor_tokens_.find(auth_factor_type);
  if (iter != active_auth_factor_tokens_.end()) {
    return &iter->second->prepare_output();
  }
  return nullptr;
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
    // Check invariants.
    CHECK(!decrypt_token_);
    CHECK(file_system_keyset_.has_value());
    // Create the USS for the newly created non-ephemeral user. Keep the USS in
    // memory: it will be persisted after the first auth factor gets added.
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
    // Attempt to add the new USS to the manager.
    CryptohomeStatusOr<UssManager::DecryptToken> token =
        uss_manager_->AddDecrypted(obfuscated_username_, std::move(*new_uss));
    if (!token.ok()) {
      return std::move(token).err_status();
    }
    decrypt_token_ = std::move(*token);
  }

  return OkStatus<CryptohomeError>();
}

void AuthSession::RegisterVaultKeysetAuthFactor(AuthFactor auth_factor) {
  GetAuthFactorMap().Add(std::move(auth_factor),
                         AuthFactorStorageType::kVaultKeyset);
}

void AuthSession::CancelAllOutstandingAsyncCallbacks() {
  weak_factory_.InvalidateWeakPtrs();
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
  AddCredentialVerifier(auth_factor_type, auth_factor_label, auth_input,
                        auth_factor_metadata);

  UssMigrator migrator(obfuscated_username_);
  // FilesystemKeyset is the same for all VaultKeysets hence the session's
  // |file_system_keyset_| is what we need for the migrator.
  migrator.MigrateVaultKeysetToUss(
      *uss_manager_, uss_storage_, auth_factor_label,
      file_system_keyset_.value(),
      base::BindOnce(&AuthSession::OnMigrationUssCreatedForUpdate,
                     weak_factory_.GetWeakPtr(), auth_factor_type,
                     auth_factor_label, auth_factor_metadata, auth_input,
                     std::move(on_done), std::move(callback_error),
                     std::move(key_blobs), std::move(auth_block_state)));
  // Since migration removes the keyset file, we don't update the keyset file.
  return;
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

  auth_block_utility_->DeriveKeyBlobsWithAuthBlock(*auth_block_type, auth_input,
                                                   metadata, auth_state,
                                                   std::move(derive_callback));
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
        PrimaryActionIs(status, error::PrimaryAction::kFactorLockedOut)) {
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
  file_system_keyset_ = vault_keyset_->ToFileSystemKeyset();

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
                        auth_input, metadata);

  ReportTimerDuration(auth_session_performance_timer.get());

  if (auth_for_decrypt_) {
    UssMigrator migrator(obfuscated_username_);

    migrator.MigrateVaultKeysetToUss(
        *uss_manager_, uss_storage_, vault_keyset_->GetLabel(),
        file_system_keyset_.value(),
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
    std::optional<UssManager::DecryptToken> loaded_token) {
  if (!loaded_token) {
    LOG(ERROR) << "Uss migration during UpdateVaultKeyset failed for "
                  "VaultKeyset with label: "
               << auth_factor_label;
    // We don't report VK to USS migration status here because it is expected
    // that the actual migration will have already reported a more precise error
    // directly.
    std::move(on_done).Run(OkStatus<CryptohomeError>());
    return;
  }

  decrypt_token_ = std::move(*loaded_token);

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
    std::optional<UssManager::DecryptToken> loaded_token) {
  if (!loaded_token) {
    LOG(ERROR) << "Uss migration failed for VaultKeyset with label: "
               << key_data_.label();
    // We don't report VK to USS migration status here because it is expected
    // that the actual migration will have already reported a more precise error
    // directly.
    std::move(on_done).Run(std::move(pre_migration_status));
    return;
  }

  decrypt_token_ = std::move(*loaded_token);

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

  AuthFactorMetadata migrated_auth_factor_metadata =
      CreateAuthFactorMetadataForMigration(auth_factor_metadata,
                                           auth_factor_type, features_);

  // If |vault_keyset_| has an empty label legacy label from GetLabel() is
  // passed for the USS wrapped block.
  auto create_callback = base::BindOnce(
      &AuthSession::PersistAuthFactorToUserSecretStashOnMigration,
      weak_factory_.GetWeakPtr(), auth_factor_type, vault_keyset_->GetLabel(),
      migrated_auth_factor_metadata, migration_auth_input_status.value(),
      std::move(migration_performance_timer), std::move(on_done),
      std::move(pre_migration_status));

  CreateAuthBlockStateAndKeyBlobs(
      auth_factor_type, auth_block_type, migration_auth_input_status.value(),
      migrated_auth_factor_metadata, std::move(create_callback));
}

const FileSystemKeyset& AuthSession::file_system_keyset() const {
  CHECK(file_system_keyset_.has_value());
  return file_system_keyset_.value();
}

bool AuthSession::MigrateResetSecretToUss() {
  CHECK(decrypt_token_);
  if (!vault_keyset_->HasWrappedResetSeed()) {
    // Authenticated VaultKeyset doesn't include a reset seed if it is not a
    // password VaultKeyset";
    return false;
  }

  bool updated = false;
  DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
  auto transaction = decrypted_uss.StartTransaction();
  for (AuthFactorMap::ValueView stored_auth_factor : GetAuthFactorMap()) {
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
    if (decrypted_uss.GetResetSecret(auth_factor.label())) {
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

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(*request_auth_factor_type);

  auto callback_with_metrics =
      WrapCallbackWithMetricsReporting<const PostAuthAction&>(
          std::move(callback), *request_auth_factor_type,
          kCryptohomeErrorAuthenticateAuthFactorErrorBucket);

  // Currently only lightweight auth might specify a non-null post-auth action,
  // so use the callback pre-bound with null post-auth action in all other
  // places to keep code simple.
  auto [on_done_temp, on_done_with_action] =
      base::SplitOnceCallback(std::move(callback_with_metrics));

  bool needs_reprepare =
      factor_driver.GetPrepareRequirement(
          AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor) ==
      AuthFactorDriver::PrepareRequirement::kEach;

  StatusCallback on_done;
  if (needs_reprepare) {
    on_done = base::BindOnce(
        [](AuthenticateAuthFactorCallback callback,
           AuthFactorType auth_factor_type, CryptohomeStatus status) {
          if (status.ok()) {
            std::move(callback).Run(kNoPostAction, std::move(status));
            return;
          }
          AuthSession::PostAuthAction reprepare_action{
              .action_type = PostAuthActionType::kReprepare};
          user_data_auth::AuthFactorType auth_factor_type_proto =
              AuthFactorTypeToProto(auth_factor_type);
          user_data_auth::PrepareAuthFactorRequest request;
          request.set_auth_factor_type(auth_factor_type_proto);
          request.set_purpose(user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);
          reprepare_action.reprepare_request = request;
          std::move(callback).Run(std::move(reprepare_action),
                                  std::move(status));
        },
        std::move(on_done_temp), *request_auth_factor_type);
  } else {
    on_done = base::BindOnce(std::move(on_done_temp), kNoPostAction);
  }

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
          CreateAuthInputForAuthentication(auth_input_proto);
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
            CreateAuthInputForAuthentication(auth_input_proto);
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
          GetAuthFactorMap().Find(auth_factor_labels[0]);
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
              ErrorActionSet({PossibleAction::kAuth}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_INVALID_ARGUMENT));
          return;
        }
      }

      CryptohomeStatusOr<AuthInput> auth_input =
          CreateAuthInputForAuthentication(auth_input_proto);
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
            GetAuthFactorMap().Find(label);
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
  AuthFactorMap& auth_factor_map = session_->GetAuthFactorMap();

  std::optional<AuthFactorMap::ValueView> stored_auth_factor =
      auth_factor_map.Find(auth_factor_label);
  if (!stored_auth_factor) {
    LOG(ERROR) << "AuthSession: Key to remove not found: " << auth_factor_label;
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorNotFoundInRemoveAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }
  LOG(INFO) << "AuthSession: Starting remove with auth_factor: "
            << auth_factor_label;

  on_done = WrapCallbackWithMetricsReporting(
      std::move(on_done), stored_auth_factor->auth_factor().type(),
      kCryptohomeErrorRemoveAuthFactorErrorBucket);

  if (auth_factor_map.size() == 1) {
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
      !session_->decrypt_token_ ||
      stored_auth_factor->storage_type() == AuthFactorStorageType::kVaultKeyset;

  if (!remove_using_vk) {
    session_->RemoveAuthFactorViaUserSecretStash(
        auth_factor_label, stored_auth_factor->auth_factor(),
        base::BindOnce(&AuthSession::ClearAuthFactorInMemoryObjects,
                       session_->weak_factory_.GetWeakPtr(), auth_factor_label,
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
  session_->GetAuthFactorMap().Remove(auth_factor_label);
  session_->verifier_forwarder_.ReleaseVerifier(auth_factor_label);

  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::PrepareUserForRemoval(base::OnceClosure on_finish) {
  // Remove rate-limiters separately, as it won't be removed by any auth
  // factor's removal.
  RemoveRateLimiters();

  // All auth factors of the user are being removed when we remove the user, so
  // we should PrepareForRemoval() all auth factors.
  AuthFactorMap& auth_factor_map = GetAuthFactorMap();
  base::RepeatingClosure barrier =
      base::BarrierClosure(auth_factor_map.size(), std::move(on_finish));
  for (AuthFactorMap::ValueView stored_auth_factor : auth_factor_map) {
    const AuthFactor& auth_factor = stored_auth_factor.auth_factor();
    auto log_status = [](const AuthFactor& auth_factor,
                         base::OnceClosure on_finish,
                         CryptohomeStatus remove_status) {
      if (!remove_status.ok()) {
        LOG(WARNING) << "Failed to prepare auth factor " << auth_factor.label()
                     << " for removal: " << remove_status;
      }
      std::move(on_finish).Run();
    };
    auth_block_utility_->PrepareAuthBlockForRemoval(
        obfuscated_username_, auth_factor.auth_block_state(),
        base::BindOnce(log_status, auth_factor, barrier));
  }
}

void AuthSession::RemoveRateLimiters() {
  // Currently fingerprint is the only auth factor type using rate
  // limiter, so the field name isn't generic. We'll make it generic to any
  // auth factor types in the future.
  ASSIGN_OR_RETURN(
      const EncryptedUss* encrypted_uss,
      uss_manager_->LoadEncrypted(obfuscated_username_),
      (_.LogWarning() << "Failed to load the user metadata.").ReturnVoid());
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
  GetAuthFactorMap().Remove(auth_factor_label);
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
  CHECK(decrypt_token_);

  auth_factor_manager_->RemoveAuthFactor(
      obfuscated_username_, auth_factor, auth_block_utility_,
      base::BindOnce(&AuthSession::ResaveUssWithFactorRemoved,
                     weak_factory_.GetWeakPtr(), auth_factor_label, auth_factor,
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
  LOG(INFO) << "AuthSession: Removed AuthFactor: " << auth_factor_label;

  // At any step after this point if we fail in updating the USS we still report
  // OkStatus as the final result. The AuthFactor itself is already gone and so
  // no matter how the rest of the cleanup goes the removal has happened.
  DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
  {
    auto transaction = decrypted_uss.StartTransaction();
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

  LOG(INFO) << "AuthSession: Starting update with auth_factor: "
            << request.auth_factor_label();
  std::optional<AuthFactorMap::ValueView> stored_auth_factor =
      session_->GetAuthFactorMap().Find(request.auth_factor_label());
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
      session_->CreateAuthInputForAdding(request.auth_input(),
                                         auth_factor_type);
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

  session_->CreateAuthBlockStateAndKeyBlobs(
      auth_factor_type, auth_block_type.value(), auth_input_status.value(),
      auth_factor_metadata, std::move(create_callback));
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

  LOG(INFO) << "AuthSession: Updated AuthFactor: " << auth_factor_label;

  // Create the auth factor by combining the metadata with the auth block
  // state.
  AuthFactor auth_factor(auth_factor_type, auth_factor_label,
                         auth_factor_metadata, *auth_block_state);

  // Update/persist the factor.
  auth_factor_manager_->UpdateAuthFactor(
      obfuscated_username_, auth_factor_label, auth_factor, auth_block_utility_,
      base::BindOnce(
          &AuthSession::ResaveUssWithFactorUpdated, weak_factory_.GetWeakPtr(),
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

  DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
  {
    auto transaction = decrypted_uss.StartTransaction();

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
  AddCredentialVerifier(auth_factor_type, auth_factor.label(), auth_input,
                        auth_factor.metadata());

  LOG(INFO) << "AuthSession: updated auth factor " << auth_factor.label()
            << " in USS.";
  GetAuthFactorMap().Add(std::move(auth_factor),
                         AuthFactorStorageType::kUserSecretStash);
  ReportTimerDuration(auth_session_performance_timer.get());
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthForDecrypt::UpdateAuthFactorMetadata(
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
      session_->GetAuthFactorMap().Find(request.auth_factor_label());
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
  if (!AuthFactorPropertiesFromProto(request.auth_factor(),
                                     *session_->features_, auth_factor_type,
                                     auth_factor_label, auth_factor_metadata)) {
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
  auto status = session_->auth_factor_manager_->SaveAuthFactorFile(
      session_->obfuscated_username_, *auth_factor);
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
  AuthFactorMap& auth_factor_map = session_->GetAuthFactorMap();
  std::optional<AuthFactor> old_auth_factor;
  {
    std::optional<AuthFactorMap::ValueView> stored_auth_factor =
        auth_factor_map.Find(request.auth_factor_label());
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
  if (auth_factor_map.Find(request.new_auth_factor_label())) {
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
    DecryptedUss& decrypted_uss =
        session_->uss_manager_->GetDecrypted(*session_->decrypt_token_);
    auto transaction = decrypted_uss.StartTransaction();
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
  auth_factor_map.Remove(old_auth_factor->label());
  auth_factor_map.Add(std::move(new_auth_factor),
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
  AuthFactorMap& auth_factor_map = session_->GetAuthFactorMap();
  std::optional<AuthFactor> original_auth_factor;
  {
    std::optional<AuthFactorMap::ValueView> stored_auth_factor =
        auth_factor_map.Find(request.auth_factor_label());
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
  if (auth_factor_map.Find(request.auth_factor().label())) {
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
      request.auth_input(), auth_factor_type);
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
      auth_factor_type, std::move(auth_factor_label), auth_factor_metadata,
      std::move(perf_timer), std::move(on_done));
  session_->CreateAuthBlockStateAndKeyBlobs(auth_factor_type, *auth_block_type,
                                            *auth_input, auth_factor_metadata,
                                            std::move(create_callback));
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
      request.auth_input(), auth_factor_type);
  if (!auth_input.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionNoInputInReplaceAuthFactorEphemeral))
            .Wrap(std::move(auth_input).err_status()));
    return;
  }

  // Create the replacement verifier.
  auto replacement_verifier = factor_driver.CreateCredentialVerifier(
      auth_factor_label, *auth_input, auth_factor_metadata);
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
    DecryptedUss& decrypted_uss =
        session_->uss_manager_->GetDecrypted(*session_->decrypt_token_);
    auto transaction = decrypted_uss.StartTransaction();

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
  AuthFactorMap& auth_factor_map = session_->GetAuthFactorMap();
  factor_to_remove = &original_auth_factor;
  session_->verifier_forwarder_.ReleaseVerifier(original_auth_factor.label());
  session_->AddCredentialVerifier(auth_factor_type, auth_factor_label,
                                  auth_input, auth_factor_metadata);
  auth_factor_map.Remove(original_auth_factor.label());
  auth_factor_map.Add(std::move(replacement_auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  LOG(INFO) << "AuthSession: replaced auth factor "
            << original_auth_factor.label() << " with new auth factor "
            << auth_factor_label;
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void AuthSession::AuthForDecrypt::MigrateLegacyFingerprints(
    StatusCallback on_done) {
  // USS is required for fp migration.
  auto encrypted_uss =
      session_->uss_manager_->LoadEncrypted(session_->obfuscated_username());
  if (!encrypted_uss.ok()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoUSSInMigrateLegacyFps),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }

  if (session_->fp_migration_utility_->NeedsMigration(
          (*encrypted_uss)->legacy_fingerprint_migration_rollout())) {
    session_->auth_factor_manager_->RemoveMigratedFingerprintAuthFactors(
        session_->obfuscated_username(), session_->auth_block_utility_,
        base::BindOnce(
            &AuthSession::AuthForDecrypt::UpdateUssAndStartFpMigration,
            weak_factory_.GetWeakPtr(), std::move(on_done)));
    return;
  } else {
    std::move(on_done).Run(OkStatus<CryptohomeError>());
    return;
  }
}

void AuthSession::AuthForDecrypt::UpdateUssAndStartFpMigration(
    StatusCallback on_done, CryptohomeStatus status) {
  if (!status.ok()) {
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Walk through the wrapped key ids in the USS, each corresponding to an auth
  // factor label. Remove the ids mapping to deleted auth factors.
  DecryptedUss& decrypted_uss =
      session_->uss_manager_->GetDecrypted(*session_->decrypt_token_);
  absl::flat_hash_set<std::string_view> uss_labels =
      decrypted_uss.encrypted().WrappedMainKeyIds();
  const auto& auth_factor_map = session_->GetAuthFactorMap();
  auto transaction = decrypted_uss.StartTransaction();
  for (const std::string_view label : uss_labels) {
    const std::string auth_factor_label(label);
    if (auth_factor_map.Find(auth_factor_label).has_value()) {
      continue;
    }

    // If an auth factor has been removed, remove its associated entry in the
    // USS. Log and ignore the return status as this removal should never fail.
    if (auto status = transaction.RemoveWrappingId(auth_factor_label);
        !status.ok()) {
      LOG(ERROR) << "Failed to remove the wrapping id <" << auth_factor_label
                 << "> from the USS after removing migrated fp factors: "
                 << status;
    }
  }

  if (auto status = std::move(transaction).Commit(); !status.ok()) {
    LOG(ERROR) << "Failed to persist user secret stash after remove migrated "
                  "fp factors:"
               << status;
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionPersistUSSFailedInDeletingMigratedFpFactors),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }
  session_->fp_migration_utility_->ListLegacyRecords(
      base::BindOnce(&AuthSession::AuthForDecrypt::MigrateLegacyRecords,
                     weak_factory_.GetWeakPtr(), std::move(on_done)));
}

void AuthSession::AuthForDecrypt::MigrateLegacyRecords(
    StatusCallback on_done,
    CryptohomeStatusOr<std::vector<LegacyRecord>> legacy_records) {
  if (!legacy_records.ok()) {
    std::move(on_done).Run(std::move(legacy_records).status());
    return;
  }
  if (legacy_records->empty()) {
    MarkFpMigrationCompletion(std::move(on_done));
    return;
  }

  AuthFactorDriver& fp_factor_driver =
      session_->auth_factor_driver_manager_->GetDriver(
          AuthFactorType::kFingerprint);

  // Fp auth factor requires a dedicated rate limiter in the USS.
  if (!session_->decrypt_token_) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoDecryptedUSSInMigrateLegacyFps),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }
  DecryptedUss& decrypted_uss =
      session_->uss_manager_->GetDecrypted(*session_->decrypt_token_);
  CryptohomeStatus status = fp_factor_driver.TryCreateRateLimiter(
      session_->obfuscated_username_, decrypted_uss);
  if (!status.ok()) {
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Binds |legacy_records| to a do-nothing callback called after |on_done|
  // to ensure the lifetime of |legacy_records| lasts until |on_done| completes.
  MigrateFromTheBack(*legacy_records,
                     std::move(on_done).Then(base::BindOnce(
                         [](auto) {}, std::move(legacy_records))));
}

void AuthSession::AuthForDecrypt::MigrateFromTheBack(
    base::span<LegacyRecord> legacy_records, StatusCallback on_done) {
  if (legacy_records.empty()) {
    MarkFpMigrationCompletion(std::move(on_done));
    return;
  }

  // Migration starts from the back of the list, so that the index of the
  // record is the same as the size of the span. The index later derives
  // the auth factor label, which must be unique for each factor.
  const auto& legacy_record = legacy_records.back();

  auto prepare_input = session_->CreatePrepareInputForAdding(
      user_data_auth::PrepareInput(), AuthFactorType::kFingerprint);
  if (!prepare_input.ok()) {
    std::move(on_done).Run(std::move(prepare_input).err_status());
    return;
  }
  AuthInput auth_input;
  auth_input.obfuscated_username = prepare_input->username;
  auth_input.reset_secret = prepare_input->reset_secret;
  auth_input.rate_limiter_label = prepare_input->rate_limiter_label;
  FingerprintAuthInput fp_auth_input;
  fp_auth_input.legacy_record_id = legacy_record.legacy_record_id;
  auth_input.fingerprint_auth_input = fp_auth_input;

  session_->fp_migration_utility_->PrepareLegacyTemplate(
      std::move(auth_input),
      base::BindOnce(
          &AuthSession::AuthForDecrypt::ContinueAddMigratedFpAuthFactor,
          weak_factory_.GetWeakPtr(), legacy_records, std::move(on_done)));
}

void AuthSession::AuthForDecrypt::ContinueAddMigratedFpAuthFactor(
    base::span<LegacyRecord> legacy_records,
    StatusCallback on_done,
    CryptohomeStatus status) {
  if (!status.ok() || legacy_records.empty()) {
    std::move(on_done).Run(std::move(status));
    return;
  }

  const auto& legacy_record = legacy_records.back();
  auto migrate_more = base::BindOnce(
      &AuthSession::AuthForDecrypt::MigrateRemainingLegacyFingerprints,
      weak_factory_.GetWeakPtr(),
      legacy_records.first(legacy_records.size() - 1), std::move(on_done));
  user_data_auth::AddAuthFactorRequest req;
  auto* auth_factor = req.mutable_auth_factor();
  auth_factor->set_type(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  auth_factor->mutable_fingerprint_metadata()->set_was_migrated(true);
  auth_factor->mutable_common_metadata()->set_user_specified_name(
      legacy_record.user_specified_name);

  auth_factor->set_label(
      FpMigrationUtility::MigratedLegacyFpLabel(legacy_records.size()));
  req.mutable_auth_input()->mutable_fingerprint_input();
  AddAuthFactor(std::move(req), std::move(migrate_more));
}

void AuthSession::AuthForDecrypt::MigrateRemainingLegacyFingerprints(
    base::span<LegacyRecord> remaining_records,
    StatusCallback on_done,
    CryptohomeStatus status) {
  if (!status.ok()) {
    std::move(on_done).Run(std::move(status));
    return;
  }

  MigrateFromTheBack(remaining_records, std::move(on_done));
}

void AuthSession::AuthForDecrypt::MarkFpMigrationCompletion(
    StatusCallback on_done) {
  DecryptedUss& decrypted_uss =
      session_->uss_manager_->GetDecrypted(*session_->decrypt_token_);
  auto transaction = decrypted_uss.StartTransaction();

  if (auto status = transaction.IncreaseLegacyFingerprintMigrationRolloutTo(
          session_->fp_migration_utility_
              ->GetLegacyFingerprintMigrationRollout());
      !status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionAddToUSSFailedInPersistFpMigrationRollout),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  // Persist the USS.
  if (auto status = std::move(transaction).Commit(); !status.ok()) {
    LOG(ERROR) << "Failed to persist user secret stash after updating fp "
                  "migration rollout.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionPersistUSSFailedInPersistFpMigrationRollout),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

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

  if (factor_driver.GetPrepareRequirement(*purpose) !=
      AuthFactorDriver::PrepareRequirement::kNone) {
    switch (*purpose) {
      case AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor: {
        auto prepare_input = CreatePrepareInputForAuthentication(
            request.prepare_input(), *auth_factor_type);
        if (!prepare_input.ok()) {
          std::move(on_done).Run(std::move(prepare_input).err_status());
          return;
        }
        factor_driver.PrepareForAuthenticate(
            *prepare_input,
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
        session_decrypt->PrepareAuthFactorForAdd(
            request.prepare_input(), *auth_factor_type, std::move(on_done));
        break;
      }
    }

    // If this type of factor supports label-less verifiers, then create one.
    if (auto verifier = factor_driver.CreateCredentialVerifier({}, {}, {})) {
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
    const user_data_auth::PrepareInput& prepare_input_proto,
    AuthFactorType auth_factor_type,
    StatusCallback on_done) {
  AuthFactorDriver& factor_driver =
      session_->auth_factor_driver_manager_->GetDriver(auth_factor_type);

  if (!session_->decrypt_token_) {
    // Currently PrepareAuthFactor is only supported for USS.
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionNoUSSInPrepareAuthFactorForAdd),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(std::move(status));
    return;
  }
  DecryptedUss& decrypted_uss =
      session_->uss_manager_->GetDecrypted(*session_->decrypt_token_);
  if (factor_driver.NeedsRateLimiter()) {
    CryptohomeStatus status = factor_driver.TryCreateRateLimiter(
        session_->obfuscated_username_, decrypted_uss);
    if (!status.ok()) {
      std::move(on_done).Run(std::move(status));
      return;
    }
  }
  auto prepare_input = session_->CreatePrepareInputForAdding(
      prepare_input_proto, auth_factor_type);
  if (!prepare_input.ok()) {
    std::move(on_done).Run(std::move(prepare_input).err_status());
    return;
  }
  factor_driver.PrepareForAdd(
      *prepare_input,
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

  // Create the USS for the newly created non-ephemeral user. Keep the USS in
  // memory: it will be persisted after the first auth factor gets added.
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

  AuthBlock::CreateCallback create_callback = base::BindOnce(
      &AuthSession::ResaveKeysetOnKeyBlobsGenerated, weak_factory_.GetWeakPtr(),
      std::move(updated_vault_keyset));
  CreateAuthBlockStateAndKeyBlobs(
      AuthFactorType::kPassword, out_auth_block_type.value(), auth_input,
      /*auth_factor_metadata=*/{}, std::move(create_callback));

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
    const user_data_auth::AuthInput& auth_input_proto) {
  // Look up the ephemeral public key. If a recovery operation has been prepared
  // then it should be available.
  const brillo::Blob* cryptohome_recovery_ephemeral_pub_key = nullptr;
  if (const PrepareOutput* prepare_output =
          GetFactorTypePrepareOutput(AuthFactorType::kCryptohomeRecovery)) {
    if (prepare_output->cryptohome_recovery_prepare_output) {
      cryptohome_recovery_ephemeral_pub_key =
          &prepare_output->cryptohome_recovery_prepare_output
               ->ephemeral_pub_key;
    }
  }

  std::optional<AuthInput> auth_input = CreateAuthInput(
      platform_, auth_input_proto, username_, obfuscated_username_,
      auth_block_utility_->GetLockedToSingleUser(),
      cryptohome_recovery_ephemeral_pub_key);
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
    AuthFactorType auth_factor_type) {
  // Convert the proto to a basic AuthInput.
  std::optional<AuthInput> auth_input = CreateAuthInput(
      platform_, auth_input_proto, username_, obfuscated_username_,
      auth_block_utility_->GetLockedToSingleUser(), nullptr);
  if (!auth_input.has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocCreateFailedInAuthInputForAdd),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Delegate the rest of the construction to the other overload.
  return CreateAuthInputForAdding(*std::move(auth_input), auth_factor_type);
}

CryptohomeStatusOr<AuthInput> AuthSession::CreateAuthInputForAdding(
    AuthInput auth_input, AuthFactorType auth_factor_type) {
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);

  std::optional<KnowledgeFactorType> knowledge_factor_type =
      factor_driver.GetKnowledgeFactorType();
  if (knowledge_factor_type.has_value() && decrypt_token_) {
    DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
    const SecurityDomainKeys* security_domain_keys =
        decrypted_uss.GetSecurityDomainKeys();
    if (!security_domain_keys) {
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(
              kLocRateLimiterNoSecurityDomainKeysInAuthInputForAdd),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    auth_input.security_domain_keys = *security_domain_keys;
  }

  // Types which need rate-limiters are exclusive with those which need
  // per-label reset secrets.
  if (factor_driver.NeedsRateLimiter() && decrypt_token_) {
    DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
    std::optional<brillo::SecureBlob> reset_secret =
        decrypted_uss.GetRateLimiterResetSecret(auth_factor_type);
    if (!reset_secret.has_value()) {
      LOG(ERROR) << "No existing rate-limiter.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocRateLimiterNoRateLimiterInAuthInputForAdd),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    auth_input.reset_secret = reset_secret;
    return std::move(auth_input);
  }

  if (factor_driver.NeedsResetSecret() && decrypt_token_) {
    // When using USS, every resettable factor gets a unique reset secret,
    // each of which is generated independently.
    LOG(INFO) << "Adding random reset secret for UserSecretStash.";
    auth_input.reset_secret =
        CreateSecureRandomBlob(kCryptohomeResetSecretLength);
    return std::move(auth_input);
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
    ASSIGN_OR_RETURN(
        const EncryptedUss* encrypted_uss,
        uss_manager_->LoadEncrypted(obfuscated_username_),
        _.WithStatus<CryptohomeError>(
             CRYPTOHOME_ERR_LOC(
                 kLocAuthSessionGetMetadataFailedInAuthInputForSelect),
             user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
                .LogError()
            << "Failed to load the user metadata.");

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

CryptohomeStatusOr<PrepareInput> AuthSession::CreatePrepareInputForAdding(
    const user_data_auth::PrepareInput& prepare_input_proto,
    AuthFactorType auth_factor_type) {
  PrepareInput prepare_input;
  prepare_input.username = obfuscated_username_;

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);

  if (factor_driver.NeedsRateLimiter()) {
    if (!decrypt_token_) {
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocRateLimiterNoUSSInAuthInputForPrepare),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    // Currently fingerprint is the only auth factor type using rate limiter, so
    // the interface isn't designed to be generic. We'll make it generic to any
    // auth factor types in the future.
    DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
    std::optional<uint64_t> rate_limiter_label =
        decrypted_uss.encrypted().fingerprint_rate_limiter_id();
    std::optional<brillo::SecureBlob> reset_secret =
        decrypted_uss.GetRateLimiterResetSecret(auth_factor_type);
    if (!rate_limiter_label.has_value() || !reset_secret.has_value()) {
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNoRateLimiterInAuthInputPrepareAdd),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    prepare_input.rate_limiter_label = rate_limiter_label;
    prepare_input.reset_secret = reset_secret;
    return std::move(prepare_input);
  }

  switch (prepare_input_proto.input_case()) {
    case user_data_auth::PrepareInput::kSmartCardInput: {
      for (const auto& content :
           prepare_input_proto.smart_card_input().signature_algorithms()) {
        std::optional<SerializedChallengeSignatureAlgorithm>
            signature_algorithm =
                proto::FromProto(ChallengeSignatureAlgorithm(content));
        if (signature_algorithm.has_value()) {
          prepare_input.challenge_signature_algorithms.push_back(
              signature_algorithm.value());
        } else {
          LOG(WARNING) << "Signature algorithm does not exist";
        }
      }
      break;
    }
    case user_data_auth::PrepareInput::kCryptohomeRecoveryInput:
    default:
      break;
  }

  return std::move(prepare_input);
}

CryptohomeStatusOr<PrepareInput>
AuthSession::CreatePrepareInputForAuthentication(
    const user_data_auth::PrepareInput& prepare_input_proto,
    AuthFactorType auth_factor_type) {
  PrepareInput prepare_input;
  prepare_input.username = obfuscated_username_;

  switch (prepare_input_proto.input_case()) {
    case user_data_auth::PrepareInput::kCryptohomeRecoveryInput: {
      // Set up references to the recovery-specific proto input as well as the
      // recovery-specific non-proto input to be filled in.
      const auto& recovery_input_proto =
          prepare_input_proto.cryptohome_recovery_input();
      prepare_input.cryptohome_recovery_prepare_input.emplace();
      CryptohomeRecoveryPrepareInput& recovery_input =
          *prepare_input.cryptohome_recovery_prepare_input;

      // Populate the request metadata from the prepare input.
      {
        auto& metadata = recovery_input.request_metadata;
        metadata.requestor_user_id = recovery_input_proto.requestor_user_id();
        switch (recovery_input_proto.requestor_user_id_type()) {
          case user_data_auth::CryptohomeRecoveryPrepareInput::GAIA_ID:
            metadata.requestor_user_id_type = cryptorecovery::UserType::kGaiaId;
            break;
          case user_data_auth::CryptohomeRecoveryPrepareInput::UNKNOWN:
          default:
            metadata.requestor_user_id_type =
                cryptorecovery::UserType::kUnknown;
            break;
        }
        metadata.auth_claim = cryptorecovery::AuthClaim{
            .gaia_access_token = recovery_input_proto.gaia_access_token(),
            .gaia_reauth_proof_token =
                recovery_input_proto.gaia_reauth_proof_token(),
        };
      }

      // Extract the epoch response directly from the input.
      recovery_input.epoch_response =
          brillo::BlobFromString(recovery_input_proto.epoch_response());

      // Load the auth factor specified by the input and use it to load the
      // recovery-specific auth block state.
      std::optional<AuthFactorMap::ValueView> stored_auth_factor =
          GetAuthFactorMap().Find(recovery_input_proto.auth_factor_label());
      if (!stored_auth_factor) {
        LOG(ERROR) << "Authentication key not found: "
                   << recovery_input_proto.auth_factor_label();
        return MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionFactorNotFoundInCreatePrepareInput),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND);
      }
      if (stored_auth_factor->auth_factor().type() !=
          AuthFactorType::kCryptohomeRecovery) {
        LOG(ERROR)
            << "Auth factor \"" << recovery_input_proto.auth_factor_label()
            << "\" is not a recovery factor and so cannot be prepared for "
               "recovery";
        return MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocWrongAuthFactorInCreatePrepareInput),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND);
      }
      auto* state = std::get_if<::cryptohome::CryptohomeRecoveryAuthBlockState>(
          &(stored_auth_factor->auth_factor().auth_block_state().state));
      if (!state) {
        return MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocNoRecoveryAuthBlockStateInCreatePrepareInput),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND);
      }
      recovery_input.auth_block_state = *state;

      break;
    }
    case user_data_auth::PrepareInput::kSmartCardInput: {
      break;
    }
    default:
      // No known input data type to convert.
      break;
  }

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  if (factor_driver.NeedsRateLimiter()) {
    // Load the USS to get the raw user metadata directly.
    ASSIGN_OR_RETURN(
        const EncryptedUss* encrypted_uss,
        uss_manager_->LoadEncrypted(obfuscated_username_),
        _.WithStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionGetMetadataFailedInCreatePrepareInput),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));

    // Currently fingerprint is the only auth factor type using rate
    // limiter, so the field name isn't generic. We'll make it generic to any
    // auth factor types in the future.
    if (!encrypted_uss->fingerprint_rate_limiter_id().has_value()) {
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionNoRateLimiterInCreatePrepareInput),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kAuth}),
          user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    }

    prepare_input.rate_limiter_label =
        *encrypted_uss->fingerprint_rate_limiter_id();
  }

  return prepare_input;
}

CredentialVerifier* AuthSession::AddCredentialVerifier(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    const AuthFactorMetadata& auth_factor_metadata) {
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);
  if (auto new_verifier = factor_driver.CreateCredentialVerifier(
          auth_factor_label, auth_input, auth_factor_metadata)) {
    auto* return_ptr = new_verifier.get();
    verifier_forwarder_.AddVerifier(std::move(new_verifier));
    return return_ptr;
  }
  verifier_forwarder_.ReleaseVerifier(auth_factor_label);
  return nullptr;
}

std::string AuthSession::GetSerializedStringFromToken(
    const base::UnguessableToken& token) {
  if (token.is_empty()) {
    return "";
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
    DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
    auto transaction = decrypted_uss.StartTransaction();

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
  AuthFactorMap& auth_factor_map = GetAuthFactorMap();
  if (!IsFactorTypeSupportedByVk(auth_factor_type)) {
    CryptohomeStatus cleanup_status = CleanUpAllBackupKeysets(
        *keyset_management_, obfuscated_username_, auth_factor_map);
    if (!cleanup_status.ok()) {
      LOG(ERROR) << "Cleaning up backup keysets failed: " << cleanup_status;
    }
  }

  AddCredentialVerifier(auth_factor_type, auth_factor.label(), auth_input,
                        auth_factor.metadata());

  LOG(INFO) << "AuthSession: added auth factor " << auth_factor.label()
            << " into USS.";
  auth_factor_map.Add(std::move(auth_factor),
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
    if (factor_driver.IsFullAuthRepeatable() && NeedsFullAuthForReset()) {
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
  CryptohomeStatusOr<brillo::SecureBlob> uss_credential_secret =
      key_blobs.DeriveUssCredentialSecret();
  if (!uss_credential_secret.ok()) {
    LOG(ERROR) << "AuthSession: Failed to derive credential secret for "
                  "updated auth factor.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthSessionDeriveUSSSecretFailedInAddSecretToUSS),
               ErrorActionSet({PossibleAction::kReboot, PossibleAction::kRetry,
                               PossibleAction::kDeleteVault}),
               user_data_auth::CRYPTOHOME_UPDATE_CREDENTIALS_FAILED)
        .Wrap(std::move(uss_credential_secret).err_status());
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
  if (session_->GetAuthFactorMap().Find(auth_factor_label)) {
    LOG(ERROR) << "Cannot add a new auth factor when one already exists: "
               << auth_factor_label;
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionFactorAlreadyExistsInAddAuthFactor),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  CryptohomeStatusOr<AuthInput> auth_input_status =
      session_->CreateAuthInputForAdding(request.auth_input(),
                                         auth_factor_type);
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
    session_->AddAuthFactorForEphemeral(
        auth_factor_type, auth_factor_label, auth_input_status.value(),
        auth_factor_metadata, std::move(on_done));
    return;
  }

  // Report timer for how long AddAuthFactor operation takes.
  auto auth_session_performance_timer =
      std::make_unique<AuthSessionPerformanceTimer>(
          kAuthSessionAddAuthFactorUSSTimer);

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

  session_->CreateAuthBlockStateAndKeyBlobs(
      auth_factor_type, auth_block_type.value(), auth_input_status.value(),
      auth_factor_metadata,
      base::BindOnce(
          &AuthSession::PersistAuthFactorToUserSecretStash,
          session_->weak_factory_.GetWeakPtr(), auth_factor_type,
          auth_factor_label, auth_factor_metadata, auth_input_status.value(),
          std::move(auth_session_performance_timer), std::move(on_done)));
}

void AuthSession::AddAuthFactorForEphemeral(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    const AuthFactorMetadata& auth_factor_metadata,
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

  CredentialVerifier* verifier = AddCredentialVerifier(
      auth_factor_type, auth_factor_label, auth_input, auth_factor_metadata);
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
      auth_factor, auth_input, std::move(auth_session_performance_timer),
      auth_factor_type_user_policy, std::move(on_done));
  auth_block_utility_->DeriveKeyBlobsWithAuthBlock(
      *auth_block_type, auth_input, auth_factor.metadata(),
      auth_factor.auth_block_state(), std::move(derive_callback));
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
  CryptohomeStatus populate_status = converter_.PopulateKeyDataForVK(
      obfuscated_username_, auth_factor_label, key_data_);
  if (!populate_status.ok()) {
    LOG(ERROR) << "Failed to authenticate auth session via vk-factor "
               << auth_factor_label;
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthSessionVKConverterFailedInAuthAuthFactor))
            .Wrap(std::move(populate_status)));
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
    const AuthFactor& auth_factor,
    const AuthInput& auth_input,
    std::unique_ptr<AuthSessionPerformanceTimer> auth_session_performance_timer,
    const SerializedUserAuthFactorTypePolicy& auth_factor_type_user_policy,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::optional<AuthBlock::SuggestedAction> suggested_action) {
  AuthFactorType auth_factor_type = auth_factor.type();
  const std::string& auth_factor_label = auth_factor.label();
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
  CryptohomeStatusOr<brillo::SecureBlob> uss_credential_secret =
      key_blobs->DeriveUssCredentialSecret();
  if (!uss_credential_secret.ok()) {
    LOG(ERROR)
        << "Failed to derive credential secret for authenticating auth factor";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDeriveUSSSecretFailedInLoadUSS),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED)
            .Wrap(std::move(uss_credential_secret).err_status()));
    return;
  }

  // Decrypt the USS payload.
  // This unwraps the USS Main Key with the credential secret, and decrypts the
  // USS payload using the USS Main Key. The wrapping_id field is defined equal
  // to the factor's label.
  auto existing_token = uss_manager_->LoadDecrypted(
      obfuscated_username_, auth_factor_label, *uss_credential_secret);
  if (!existing_token.ok()) {
    LOG(ERROR) << "Failed to decrypt the user secret stash";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthSessionDecryptUSSFailedInLoadUSS),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED)
            .Wrap(std::move(existing_token).err_status()));
    return;
  }

  // By this point we know that the GSC works correctly and we were able to
  // successfully decrypt the USS. So, for GSC with updatable firmware, we
  // assume that it is stable (and the GSC can invalidate the old version).
  if (hwsec::Status status = crypto_->GetHwsec()->DeclareTpmFirmwareStable();
      !status.ok()) {
    LOG(WARNING) << "Failed to declare TPM firmware stable: " << status;
  }

  decrypt_token_ = std::move(*existing_token);

  // Populate data fields from the USS.
  file_system_keyset_ =
      uss_manager_->GetDecrypted(*decrypt_token_).file_system_keyset();

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

  // Flip the status on the successful authentication and set the
  // authorization.
  SetAuthorizedForFullAuthIntents(auth_factor_type,
                                  auth_factor_type_user_policy);

  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(auth_factor_type);

  // Update the recoverable key store on the successful authentication.
  if (features_->IsFeatureEnabled(Features::kGenerateRecoverableKeyStore)) {
    std::optional<KnowledgeFactorType> knowledge_factor_type =
        factor_driver.GetKnowledgeFactorType();
    if (knowledge_factor_type.has_value()) {
      CryptohomeStatus update_status = MaybeUpdateRecoverableKeyStore(
          auth_factor, *knowledge_factor_type, auth_input);
      ReapAndReportError(
          std::move(update_status),
          {std::string(kCryptohomeErrorUpdateRecoverableKeyStoreErrorBucket)});
    }
  }

  // Set the credential verifier for this credential.
  AddCredentialVerifier(auth_factor_type, auth_factor_label, auth_input,
                        auth_factor.metadata());

  // Backup VaultKeyset of the authenticated factor can still be in disk if
  // the migration is not completed. Break the dependency of the migrated and
  // not-migrated keysets and remove the backup keyset
  if (GetAuthFactorMap().HasFactorWithStorage(
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
      GetAuthFactorMap().Find(auth_factor_label);
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

  CryptohomeStatusOr<AuthInput> auth_input_for_add =
      CreateAuthInputForAdding(std::move(auth_input), auth_factor.type());
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
  CreateAuthBlockStateAndKeyBlobs(auth_factor.type(), *auth_block_type,
                                  *auth_input_for_add, auth_factor.metadata(),
                                  std::move(create_callback));
}

void AuthSession::ResetLECredentials() {
  brillo::SecureBlob local_reset_seed;
  if (vault_keyset_ && vault_keyset_->HasWrappedResetSeed()) {
    local_reset_seed = vault_keyset_->GetResetSeed();
  }

  if (!decrypt_token_ && local_reset_seed.empty()) {
    LOG(WARNING)
        << "No user secret stash or VK available to reset LE credentials.";
    return;
  }

  for (AuthFactorMap::ValueView stored_auth_factor : GetAuthFactorMap()) {
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
    if (decrypt_token_) {
      DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
      reset_secret_uss = decrypted_uss.GetResetSecret(auth_factor.label());
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

  ResetRateLimiterCredentials();
}

void AuthSession::ResetRateLimiterCredentials() {
  if (!decrypt_token_) {
    return;
  }
  DecryptedUss& decrypted_uss = uss_manager_->GetDecrypted(*decrypt_token_);
  std::optional<uint64_t> rate_limiter_label =
      decrypted_uss.encrypted().fingerprint_rate_limiter_id();
  if (!rate_limiter_label.has_value()) {
    return;
  }

  // Currently only fingerprint auth factor has a rate-limiter.
  std::optional<brillo::SecureBlob> reset_secret =
      decrypted_uss.GetRateLimiterResetSecret(AuthFactorType::kFingerprint);
  if (!reset_secret.has_value()) {
    LOG(WARNING) << "Fingerprint rate-limiter has no reset secret in USS.";
    return;
  }
  CryptoError error;
  if (!crypto_->ResetLeCredential(rate_limiter_label.value(),
                                  reset_secret.value(), /*strong_reset=*/true,
                                  error)) {
    LOG(WARNING) << "Failed to reset fingerprint rate-limiter with error: "
                 << error;
  }

  for (AuthFactorMap::ValueView stored_auth_factor : GetAuthFactorMap()) {
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

bool AuthSession::NeedsFullAuthForReset() {
  // Check if LE credentials need reset.
  for (AuthFactorMap::ValueView stored_auth_factor : GetAuthFactorMap()) {
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

  // Check if there is a rate-limiter to reset.
  ASSIGN_OR_RETURN(const EncryptedUss* encrypted_uss,
                   uss_manager_->LoadEncrypted(obfuscated_username_),
                   _.As(false));
  return encrypted_uss->fingerprint_rate_limiter_id().has_value();
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

void AuthSession::CreateAuthBlockStateAndKeyBlobs(
    AuthFactorType auth_factor_type,
    AuthBlockType auth_block_type,
    const AuthInput& auth_input,
    const AuthFactorMetadata& auth_factor_metadata,
    AuthBlock::CreateCallback create_callback) {
  auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      auth_block_type, auth_input, auth_factor_metadata,
      base::BindOnce(&AuthSession::CreateCommonAuthBlockState,
                     weak_factory_.GetWeakPtr(), auth_factor_type, auth_input,
                     auth_factor_metadata, std::move(create_callback)));
}

void AuthSession::CreateCommonAuthBlockState(
    AuthFactorType auth_factor_type,
    const AuthInput& auth_input,
    const AuthFactorMetadata& auth_factor_metadata,
    AuthBlock::CreateCallback create_callback,
    CryptohomeStatus error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_block_state) {
  // If creation failed, pass on to the original callback to do error handling.
  if (!error.ok() || !key_blobs || !auth_block_state) {
    std::move(create_callback)
        .Run(std::move(error), std::move(key_blobs),
             std::move(auth_block_state));
    return;
  }
  // Now, create the common part of auth block state. Currently it's only the
  // recoverable key store state.
  if (features_->IsFeatureEnabled(Features::kGenerateRecoverableKeyStore)) {
    const AuthFactorDriver& factor_driver =
        auth_factor_driver_manager_->GetDriver(auth_factor_type);
    std::optional<KnowledgeFactorType> knowledge_factor_type =
        factor_driver.GetKnowledgeFactorType();
    if (knowledge_factor_type.has_value()) {
      CryptohomeStatus create_status = CreateRecoverableKeyStore(
          auth_factor_type, *knowledge_factor_type, auth_factor_metadata,
          auth_input, *auth_block_state);
      ReapAndReportError(
          std::move(create_status),
          {std::string(kCryptohomeErrorCreateRecoverableKeyStoreErrorBucket)});
    }
  }
  // Pass on the results to the original callback, with the auth_block_state
  // updated.
  std::move(create_callback)
      .Run(std::move(error), std::move(key_blobs), std::move(auth_block_state));
}

CryptohomeStatus AuthSession::CreateRecoverableKeyStore(
    AuthFactorType auth_factor_type,
    KnowledgeFactorType knowledge_factor_type,
    const AuthFactorMetadata& auth_factor_metadata,
    AuthInput auth_input,
    AuthBlockState& auth_block_state) {
  // This is always called when USS is decrypted.
  CHECK(decrypt_token_);

  // Cryptohome error codes in this function aren't carefully chosen, as these
  // will never be returned in a dbus response. They're only for UMA reporting
  // (which doesn't report the error code), and the error codes themselves are
  // deprecating soon. Similarly, only the kDevCheckUnexpectedState action
  // matters for UMA reporting.
  const SecurityDomainKeys* security_domain_keys =
      uss_manager_->GetDecrypted(*decrypt_token_).GetSecurityDomainKeys();
  if (!security_domain_keys) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocCreateKeyStoreNoDomainKeys),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  auth_input.security_domain_keys = *security_domain_keys;
  RecoverableKeyStoreBackendCertProvider* provider =
      key_store_cert_provider_.get();
  if (!provider) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocCreateKeyStoreNoProvider),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  CryptohomeStatusOr<RecoverableKeyStoreState> key_store_state =
      CreateRecoverableKeyStoreState(knowledge_factor_type, auth_input,
                                     auth_factor_metadata, *provider);
  if (!key_store_state.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocCreateKeyStoreCreateKeyStoreFailed),
               ErrorActionSet())
        .Wrap(std::move(key_store_state).err_status());
  }
  auth_block_state.recoverable_key_store_state = std::move(*key_store_state);
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus AuthSession::MaybeUpdateRecoverableKeyStore(
    const AuthFactor& auth_factor,
    KnowledgeFactorType knowledge_factor_type,
    AuthInput auth_input) {
  // This is always called after USS is decrypted.
  CHECK(decrypt_token_);

  // Cryptohome error codes in this function aren't carefully chosen, as these
  // will never be returned in a dbus response. They're only for UMA reporting
  // (which doesn't report the error code), and the error codes themselves are
  // deprecating soon. Similarly, only the kDevCheckUnexpectedState action
  // matters for UMA reporting.
  const SecurityDomainKeys* security_domain_keys =
      uss_manager_->GetDecrypted(*decrypt_token_).GetSecurityDomainKeys();
  if (!security_domain_keys) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUpdateKeyStoreNoDomainKeys),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  auth_input.security_domain_keys = *security_domain_keys;
  RecoverableKeyStoreBackendCertProvider* provider =
      key_store_cert_provider_.get();
  if (!provider) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUpdateKeyStoreNoProvider),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  RecoverableKeyStoreState new_state;
  const std::optional<RecoverableKeyStoreState>& old_state =
      auth_factor.auth_block_state().recoverable_key_store_state;
  if (!old_state.has_value()) {
    CryptohomeStatusOr<RecoverableKeyStoreState> new_state_status =
        CreateRecoverableKeyStoreState(knowledge_factor_type, auth_input,
                                       auth_factor.metadata(), *provider);
    if (!new_state_status.ok()) {
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(kLocUpdateKeyStoreCreateKeyStoreFailed),
                 ErrorActionSet())
          .Wrap(std::move(new_state_status).err_status());
    }
    new_state = std::move(*new_state_status);
  } else {
    CryptohomeStatusOr<std::optional<RecoverableKeyStoreState>>
        new_state_status = MaybeUpdateRecoverableKeyStoreState(
            *old_state, knowledge_factor_type, auth_input,
            auth_factor.metadata(), *provider);
    if (!new_state_status.ok()) {
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(kLocUpdateKeyStoreUpdateKeyStoreFailed),
                 ErrorActionSet())
          .Wrap(std::move(new_state_status).err_status());
    }
    if (!new_state_status->has_value()) {
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocUpdateKeyStoreUpdateNotNeeded),
          ErrorActionSet(),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    new_state = std::move(**new_state_status);
  }

  AuthBlockState updated_auth_block_state = auth_factor.auth_block_state();
  updated_auth_block_state.recoverable_key_store_state = std::move(new_state);
  AuthFactor updated_auth_factor =
      AuthFactor(auth_factor.type(), auth_factor.label(),
                 auth_factor.metadata(), updated_auth_block_state);
  CryptohomeStatus save_status = auth_factor_manager_->SaveAuthFactorFile(
      obfuscated_username_, updated_auth_factor);
  if (!save_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocUpdateKeyStoreSaveFactorFailed),
               ErrorActionSet())
        .Wrap(std::move(save_status).err_status());
  }
  GetAuthFactorMap().Add(std::move(updated_auth_factor),
                         AuthFactorStorageType::kUserSecretStash);
  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome
