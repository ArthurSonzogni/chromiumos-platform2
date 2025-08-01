// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <absl/cleanup/cleanup.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/numbers.h>
#include <attestation/proto_bindings/pca_agent.pb.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/json/json_writer.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/rand_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <base/unguessable_token.h>
#include <biod/biod_proxy/auth_stack_manager_proxy_base.h>
#include <bootlockbox/boot_lockbox_client.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <chromeos/constants/cryptohome.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <dbus_adaptors/org.chromium.UserDataAuth.h>
#include <device_management-client/device_management/dbus-proxies.h>
#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <featured/feature_library.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <libhwsec-foundation/utility/task_dispatching_framework.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/status.h>
#include <libstorage/platform/platform.h>
#include <metrics/timer.h>
#include <pca_agent-client/pca_agent/dbus-proxies.h>

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/biometrics_command_processor_impl.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/flatbuffer.h"
#include "cryptohome/auth_factor/manager.h"
#include "cryptohome/auth_factor/protobuf.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_factor/with_driver.h"
#include "cryptohome/auth_io/auth_input.h"
#include "cryptohome/auth_io/prepare_output.h"
#include "cryptohome/auth_session/auth_session.h"
#include "cryptohome/auth_session/flatbuffer.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/auth_session/manager.h"
#include "cryptohome/auth_session/protobuf.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/cleanup/disk_cleanup.h"
#include "cryptohome/cleanup/low_disk_space_handler.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/create_vault_keyset_rpc_impl.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/fp_migration/utility.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/real_pkcs11_token_factory.h"
#include "cryptohome/recoverable_key_store/backend_cert_provider.h"
#include "cryptohome/recoverable_key_store/backend_cert_provider_impl.h"
#include "cryptohome/signalling.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/mount.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/user_session/real_user_session_factory.h"
#include "cryptohome/user_session/user_session.h"
#include "cryptohome/username.h"
#include "cryptohome/util/proto_enum.h"

using base::FilePath;
using brillo::Blob;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::Sha1;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

constexpr char kMountThreadName[] = "MountThread";
constexpr char kNotFirstBootFilePath[] = "/run/cryptohome/not_first_boot";
// For enhanced security, PinWeaver pairing key establishment is blocked after
// the first user login event in each boot cycle. An ephemeral flag file is used
// to allow tracking this status.
constexpr char kPinweaverPkEstablishmentBlocked[] =
    "/run/cryptohome/pw_pk_establishment_blocked";
constexpr char kDeviceMapperDevicePrefix[] = "/dev/mapper/dmcrypt";
constexpr base::TimeDelta kDefaultExtensionTime = base::Seconds(60);
// Some utility functions used by UserDataAuth.

// Wrapper function for the ReplyWithError. |unused_auth_session| parameter is
// used for keeping the session alive until the operation has completed.
template <typename ReplyType>
void ReplyWithStatus(InUseAuthSession unused_auth_session,
                     UserDataAuth::OnDoneCallback<ReplyType> on_done,
                     CryptohomeStatus status) {
  ReplyType reply;
  ReplyWithError(std::move(on_done), std::move(reply), std::move(status));
}

// Function that can be used to inject the sending of an auth completion signal
// to an on_done callback. The function requires a copy of the start signal that
// it uses as a template for the completion signal.
template <typename ReplyType>
void SignalAuthCompletedThenDone(
    SignallingInterface* signalling,
    user_data_auth::AuthenticateStarted start_signal,
    UserDataAuth::OnDoneCallback<ReplyType> on_done,
    const ReplyType& reply) {
  user_data_auth::AuthenticateAuthFactorCompleted signal;
  signal.set_operation_id(start_signal.operation_id());
  if (reply.has_error_info()) {
    signal.set_error(reply.error());
    *signal.mutable_error_info() = reply.error_info();
  }

  signal.set_username(start_signal.username());
  signal.set_sanitized_username(start_signal.sanitized_username());

  switch (start_signal.auth_factor_case()) {
    case user_data_auth::AuthenticateStarted::kAuthFactorType:
      signal.set_auth_factor_type(start_signal.auth_factor_type());
      break;
    case user_data_auth::AuthenticateStarted::kUserCreation:
      signal.set_user_creation(start_signal.user_creation());
      break;
    case user_data_auth::AuthenticateStarted::AUTH_FACTOR_NOT_SET:
      break;
  }

  signalling->SendAuthenticateAuthFactorCompleted(signal);
  std::move(on_done).Run(reply);
}

// Function that can be used to inject the sending of a mount completion signal
// to an on_done callback. The function requires a copy of the start signal that
// it uses as a template for the completion signal.
template <typename ReplyType>
void SignalMountCompletedThenDone(
    SignallingInterface* signalling,
    user_data_auth::MountStarted start_signal,
    UserDataAuth::OnDoneCallback<ReplyType> on_done,
    const ReplyType& reply) {
  user_data_auth::MountCompleted signal;
  signal.set_operation_id(start_signal.operation_id());
  if (reply.has_error_info()) {
    signal.set_error(reply.error());
    *signal.mutable_error_info() = reply.error_info();
  }
  signalling->SendMountCompleted(signal);
  std::move(on_done).Run(reply);
}

// This function returns the AuthFactorPolicy from the UserPolicy. It will
// return an empty policy if the user policy doesn't exist, or if the
// auth_factor_type doesn't exist in the user policy.
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

// This function sets the auth intents for auth factor type. As long as an
// intent is supported it should be included in the maximal set. The minimal set
// only includes supported non-configurable intents. If a policy has been set
// for the auth factor type, the set policy should be used as the "current" set,
// otherwise supported intents that are enabled are considered the "current"
// set.
void SetAuthIntentsForAuthFactorType(
    const AuthFactorType& type,
    const AuthFactorDriver& factor_driver,
    std::optional<SerializedUserAuthFactorTypePolicy> type_policy,
    bool is_persistent_user,
    bool is_ephemeral_user,
    user_data_auth::AuthIntentsForAuthFactorType* intents_for_type) {
  intents_for_type->set_type(AuthFactorTypeToProto(type));

  for (AuthIntent intent : kAllAuthIntents) {
    // Determine if this intent can be used with this factor type for this
    // user. The check depends on the user type as full auth is only available
    // for persistent users.
    bool intent_is_supported;
    if (is_persistent_user) {
      intent_is_supported = factor_driver.IsFullAuthSupported(intent) ||
                            factor_driver.IsLightAuthSupported(intent);
    } else if (is_ephemeral_user) {
      intent_is_supported = factor_driver.IsLightAuthSupported(intent);
    } else {
      intent_is_supported = false;
    }
    // If the intent is supported, determine which of the "current, min, max"
    // sets it belongs in based on the configuration.
    if (intent_is_supported) {
      user_data_auth::AuthIntent proto_intent = AuthIntentToProto(intent);
      // The maximum contains all supported intents, always add to it.
      intents_for_type->add_maximum(proto_intent);
      // The minimum contains only the non-configurable supported intents.
      AuthFactorDriver::IntentConfigurability intent_configurability =
          factor_driver.GetIntentConfigurability(intent);
      if (intent_configurability ==
          AuthFactorDriver::IntentConfigurability::kNotConfigurable) {
        intents_for_type->add_minimum(proto_intent);
        // If an intent is not configurable and is supported it should be
        // included in the current set of intents regardless of a new type
        // policy being applied or not.
        intents_for_type->add_current(proto_intent);
      }
      // Unless there is a policy set for the user, the current set contains
      // supported intents which are enabled by default as well as
      // notconfigurable ones.
      if (!type_policy.has_value() &&
          intent_configurability ==
              AuthFactorDriver::IntentConfigurability::kEnabledByDefault) {
        intents_for_type->add_current(proto_intent);
      }
    }
  }
  // if there is a policy in place for this auth factor type, use the policy as
  // the "current" intent.
  if (type_policy.has_value()) {
    for (auto intent : type_policy->enabled_intents) {
      intents_for_type->add_current(
          AuthIntentToProto(DeserializeAuthIntent(intent)));
    }
  }
}

// Builder function for AuthFactorWithStatus. This function takes into account
// type and calls various library functions needed to convert AuthFactor to a
// proto for a persistent user.
std::optional<user_data_auth::AuthFactorWithStatus> GetAuthFactorWithStatus(
    const ObfuscatedUsername& username,
    UserPolicyFile* user_policy_file,
    AuthFactorDriverManager* auth_factor_driver_manager,
    const AuthFactor& auth_factor) {
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager->GetDriver(auth_factor.type());
  auto auth_factor_proto =
      factor_driver.ConvertToProto(auth_factor.label(), auth_factor.metadata());
  if (!auth_factor_proto) {
    return std::nullopt;
  }
  user_data_auth::AuthFactorWithStatus auth_factor_with_status;
  *auth_factor_with_status.mutable_auth_factor() =
      std::move(*auth_factor_proto);
  auto supported_intents = GetSupportedIntents(
      username, auth_factor.type(), *auth_factor_driver_manager,
      GetAuthFactorPolicyFromUserPolicy(user_policy_file->GetUserPolicy(),
                                        auth_factor.type()),
      /*only_light_auth=*/false);
  for (const auto& auth_intent : supported_intents) {
    auth_factor_with_status.add_available_for_intents(
        AuthIntentToProto(auth_intent));
  }
  user_data_auth::StatusInfo& status_info =
      *auth_factor_with_status.mutable_status_info();
  auto delay = factor_driver.GetFactorDelay(username, auth_factor);
  if (delay.ok()) {
    status_info.set_time_available_in(delay->is_max()
                                          ? std::numeric_limits<uint64_t>::max()
                                          : delay->InMilliseconds());
  } else {
    // Error in getting factor lockout delay, treat it as immediately available.
    status_info.set_time_available_in(0);
  }
  auto expiration_delay =
      factor_driver.GetTimeUntilExpiration(username, auth_factor);
  if (expiration_delay.ok()) {
    status_info.set_time_expiring_in(expiration_delay->InMilliseconds());
  } else {
    // Error in getting the expiration time. Treat it as won't expire.
    status_info.set_time_expiring_in(std::numeric_limits<uint64_t>::max());
  }
  return auth_factor_with_status;
}

// Builder function for AuthFactorWithStatus for ephemeral users. This function
// takes into account type and calls various library functions needed to convert
// AuthFactor to a proto.
std::optional<user_data_auth::AuthFactorWithStatus> GetAuthFactorWithStatus(
    const ObfuscatedUsername& username,
    UserPolicyFile* user_policy_file,
    AuthFactorDriverManager* auth_factor_driver_manager,
    const CredentialVerifier* verifier) {
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager->GetDriver(verifier->auth_factor_type());
  auto proto_factor = factor_driver.ConvertToProto(
      verifier->auth_factor_label(), verifier->auth_factor_metadata());
  if (!proto_factor) {
    LOG(INFO) << "Could not convert";
    return std::nullopt;
  }
  user_data_auth::AuthFactorWithStatus auth_factor_with_status;
  *auth_factor_with_status.mutable_auth_factor() = std::move(*proto_factor);
  auto supported_intents = GetSupportedIntents(
      username, verifier->auth_factor_type(), *auth_factor_driver_manager,
      GetAuthFactorPolicyFromUserPolicy(user_policy_file->GetUserPolicy(),
                                        verifier->auth_factor_type()),
      /*only_light_auth=*/true);
  for (const auto& auth_intent : supported_intents) {
    auth_factor_with_status.add_available_for_intents(
        AuthIntentToProto(auth_intent));
  }

  // Ephemeral user's credential won't lock out (always available) and won't
  // expire either.
  user_data_auth::StatusInfo& status_info =
      *auth_factor_with_status.mutable_status_info();
  status_info.set_time_available_in(0);
  status_info.set_time_expiring_in(std::numeric_limits<uint64_t>::max());
  return auth_factor_with_status;
}

// Overload that takes a reply type and returns the mutable AuthFactor message
// from it. Basically just calls mutable_X for whatever field "X" is the
// AuthFactorWithStatus from the reply. There must be an overload for a type to
// work with ReplyWithAuthFactorStatus.
user_data_auth::AuthFactorWithStatus* MutableAuthFactorForReplyType(
    user_data_auth::AddAuthFactorReply& reply) {
  return reply.mutable_added_auth_factor();
}
user_data_auth::AuthFactorWithStatus* MutableAuthFactorForReplyType(
    user_data_auth::UpdateAuthFactorReply& reply) {
  return reply.mutable_updated_auth_factor();
}
user_data_auth::AuthFactorWithStatus* MutableAuthFactorForReplyType(
    user_data_auth::UpdateAuthFactorMetadataReply& reply) {
  return reply.mutable_updated_auth_factor();
}
user_data_auth::AuthFactorWithStatus* MutableAuthFactorForReplyType(
    user_data_auth::RelabelAuthFactorReply& reply) {
  return reply.mutable_relabelled_auth_factor();
}
user_data_auth::AuthFactorWithStatus* MutableAuthFactorForReplyType(
    user_data_auth::ReplaceAuthFactorReply& reply) {
  return reply.mutable_replacement_auth_factor();
}

// Wrapper function for the ReplyWithError for AddAuthFactorReply,
// UpdateAuthFactorMetadata and UpdateAuthFactorWithReply.
template <typename ReplyType>
void ReplyWithAuthFactorStatus(
    InUseAuthSession auth_session,
    UserPolicyFile* user_policy_file,
    AuthFactorManager* auth_factor_manager,
    AuthFactorDriverManager* auth_factor_driver_manager,
    UserSession* user_session,
    std::string auth_factor_label,
    UserDataAuth::OnDoneCallback<ReplyType> on_done,
    CryptohomeStatus status) {
  ReplyType reply;
  if (!status.ok()) {
    ReplyWithError(std::move(on_done), std::move(reply), std::move(status));
    return;
  }
  if (CryptohomeStatus session_status = auth_session.AuthSessionStatus();
      !session_status.ok()) {
    ReplyWithError(std::move(on_done), std::move(reply),
                   std::move(session_status));
    return;
  }

  // This should always be set.
  CHECK(auth_factor_driver_manager);

  std::optional<user_data_auth::AuthFactorWithStatus> auth_factor_with_status;
  // Select which AuthFactorWithStatus to build based on user type.
  const ObfuscatedUsername& username = auth_session->obfuscated_username();
  if (auth_session->ephemeral_user()) {
    CHECK(user_session);
    auth_factor_with_status = GetAuthFactorWithStatus(
        username, user_policy_file, auth_factor_driver_manager,
        user_session->FindCredentialVerifier(auth_factor_label));
  } else {
    auth_factor_with_status = GetAuthFactorWithStatus(
        username, user_policy_file, auth_factor_driver_manager,
        auth_factor_manager->GetAuthFactorMap(username)
            .Find(auth_factor_label)
            ->auth_factor());
  }

  if (!auth_factor_with_status.has_value()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthProtoFailureInReplyWithAuthFactorStatus),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  *MutableAuthFactorForReplyType(reply) = std::move(*auth_factor_with_status);
  ReplyWithError(std::move(on_done), std::move(reply), std::move(status));
}

// Get the Account ID for an AccountIdentifier proto.
Username GetAccountId(const AccountIdentifier& id) {
  if (id.has_account_id()) {
    return Username(id.account_id());
  }
  return Username(id.email());
}

// Returns true if any of the path in |prefixes| starts with |path|
// Note that this function is case insensitive
bool PrefixPresent(const std::vector<FilePath>& prefixes,
                   const std::string path) {
  return std::any_of(
      prefixes.begin(), prefixes.end(), [&path](const FilePath& prefix) {
        return base::StartsWith(path, prefix.value(),
                                base::CompareCase::INSENSITIVE_ASCII);
      });
}

// Groups dm-crypt mounts for each user. Mounts for a user may have a source
// in either dmcrypt-<>-data or dmcrypt-<>-cache. Strip the application
// specific suffix for the device and use <> as the group key.
void GroupDmcryptDeviceMounts(
    std::multimap<const FilePath, const FilePath>* mounts,
    std::multimap<const FilePath, const FilePath>* grouped_mounts) {
  for (const auto& mount : *mounts) {
    // Group dmcrypt-<>-data and dmcrypt-<>-cache mounts. Strip out last
    // '-' from the path.
    size_t last_component_index = mount.first.value().find_last_of("-");

    if (last_component_index == std::string::npos) {
      continue;
    }

    base::FilePath device_group(
        mount.first.value().substr(0, last_component_index));
    if (device_group.ReferencesParent()) {
      // This should probably never occur in practice, but seems useful from
      // the security hygiene perspective to explicitly prevent transforming
      // stuff like "/foo/..-" into "/foo/..".
      LOG(WARNING) << "Skipping malformed dm-crypt mount point: "
                   << mount.first;
      continue;
    }
    grouped_mounts->insert({device_group, mount.second});
  }
}

// Fill AuthSessionProperties in auth_session_props.
void PopulateAuthSessionProperties(
    InUseAuthSession& auth_session,
    user_data_auth::AuthSessionProperties* auth_session_props) {
  for (const AuthIntent auth_intent : auth_session->authorized_intents()) {
    auth_session_props->add_authorized_for(AuthIntentToProto(auth_intent));
  }

  if (auth_session->authorized_intents().contains(AuthIntent::kDecrypt)) {
    auth_session_props->set_seconds_left(
        auth_session.GetRemainingTime().InSeconds());
  }
}

void HandleAuthenticationResult(
    InUseAuthSession auth_session,
    const SerializedUserAuthFactorTypePolicy& user_policy,
    UserDataAuth::OnDoneCallback<user_data_auth::AuthenticateAuthFactorReply>
        on_done,
    const AuthSession::PostAuthAction& post_auth_action,
    CryptohomeStatus status) {
  user_data_auth::AuthenticateAuthFactorReply reply;
  if (CryptohomeStatus session_status = auth_session.AuthSessionStatus();
      !session_status.ok()) {
    // Unfortunately if the session was timed out then regardless of the
    // post-auth actions we cannot actually execute them because we no longer
    // have a session to take them with. Just return the timeout error and stop.
    ReplyWithError(std::move(on_done), std::move(reply),
                   std::move(session_status));
    return;
  }

  // If we get here we have a valid session. Fill out the reply with it.
  PopulateAuthSessionProperties(auth_session, reply.mutable_auth_properties());
  bool auth_succeeded = status.ok();
  ReplyWithError(std::move(on_done), std::move(reply), std::move(status));

  // Reset LE credentials if authentication succeeded. Note that this requires a
  // decrypted USS so verify-only intent auth might not be able to reset LE
  // successfully here. Verify-only intent auth sets the PostAuthAction as
  // kRepeat to repeat the authentication but forcing full decrypt, such that
  // the repeated auth will be able to reset LE credentials.
  if (auth_succeeded) {
    auth_session->ResetLECredentials();
  }

  // The reply is sent, carry out any post-auth actions.
  switch (post_auth_action.action_type) {
    case AuthSession::PostAuthActionType::kNone:
      return;
    case AuthSession::PostAuthActionType::kRepeat: {
      if (!post_auth_action.repeat_request.has_value()) {
        LOG(DFATAL)
            << "PostAuthActionType::kRepeat with null repeat_request field.";
        return;
      }
      // HandleAuthenticationResult will be used as the callback to ensure the
      // repeated auth is handled identically to an ordinary auth request. The
      // implementation logic should ensure that repeated auth would not set
      // post-auth action to kRepeat again, otherwise there might be infinite
      // recursion.
      AuthSession* auth_session_ptr = auth_session.Get();
      auth_session_ptr->AuthenticateAuthFactor(
          post_auth_action.repeat_request.value(), user_policy,
          base::BindOnce(&HandleAuthenticationResult,
                         std::move(auth_session).BindForCallback(), user_policy,
                         base::DoNothing()));
      return;
    }
    case AuthSession::PostAuthActionType::kReprepare: {
      if (!post_auth_action.reprepare_request.has_value()) {
        LOG(DFATAL) << "PostAuthActionType::kReprepare with null "
                       "reprepare_request field.";
        return;
      }
      AuthSession* auth_session_ptr = auth_session.Get();
      auth_session_ptr->PrepareAuthFactor(
          post_auth_action.reprepare_request.value(),
          base::BindOnce(
              [](InUseAuthSession unused_auth_session,
                 CryptohomeStatus status) {
                if (!status.ok()) {
                  LOG(ERROR) << "Reprepare failed after an "
                                "authentication attempt: "
                             << std::move(status);
                }
              },
              std::move(auth_session).BindForCallback()));
      return;
    }
  }
}

// Wrapper around AuthSessionManager::RunWhenAvailable that will execute the
// given block of code with the in use session if the session has an OK status.
// The call expects to be given:
//  - the AuthSession manager
//  - a location to wrap the error with if the session is not OK
//  - the on_done callback for the request
//  - a run_with callback that consists of the actual handler
// The run_with callback can assume that the InUseAuthSession object that it is
// given is OK, i.e. CHECK(auth_session.AuthSessionStatus().ok()).
//
// By default the function will select the session token from an auth_session_id
// field in the request. However, there is also an overload that accepts an
// explicit token argument for use in cases where the request has no such field,
// or where the session ID is selected in some other way.
template <typename RequestType, typename ReplyType, typename TokenType>
void RunWithAuthSessionWhenAvailable(
    AuthSessionManager* auth_session_manager,
    CryptohomeError::ErrorLocationPair err_loc,
    const TokenType& token,
    RequestType request,
    UserDataAuth::OnDoneCallback<ReplyType> on_done,
    UserDataAuth::HandlerWithSessionCallback<RequestType, ReplyType> run_with) {
  // Wrap run_with in a callback which will check if InUseAuthSession is OK. If
  // the session is not okay then call ReplyWithError, wrapping the session
  // status. Otherwise, call run_with and pass on all of the parameters.
  auth_session_manager->RunWhenAvailable(
      token,
      base::BindOnce(
          [](CryptohomeError::ErrorLocationPair err_loc, RequestType request,
             UserDataAuth::OnDoneCallback<ReplyType> on_done,
             UserDataAuth::HandlerWithSessionCallback<RequestType, ReplyType>
                 run_with,
             InUseAuthSession auth_session) {
            CryptohomeStatus status = auth_session.AuthSessionStatus();
            if (!status.ok()) {
              ReplyWithError(
                  std::move(on_done), ReplyType{},
                  MakeStatus<CryptohomeError>(
                      err_loc,
                      ErrorActionSet(
                          {PossibleAction::kDevCheckUnexpectedState}),
                      user_data_auth::CryptohomeErrorCode::
                          CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN)
                      .Wrap(std::move(status).err_status()));
              return;
            }
            std::move(run_with).Run(std::move(request), std::move(on_done),
                                    std::move(auth_session));
          },
          std::move(err_loc), std::move(request), std::move(on_done),
          std::move(run_with)));
}
template <typename RequestType, typename ReplyType>
void RunWithAuthSessionWhenAvailable(
    AuthSessionManager* auth_session_manager,
    CryptohomeError::ErrorLocationPair err_loc,
    RequestType request,
    UserDataAuth::OnDoneCallback<ReplyType> on_done,
    UserDataAuth::HandlerWithSessionCallback<RequestType, ReplyType> run_with) {
  std::string auth_session_id = request.auth_session_id();
  RunWithAuthSessionWhenAvailable(auth_session_manager, std::move(err_loc),
                                  auth_session_id, std::move(request),
                                  std::move(on_done), std::move(run_with));
}

// Wrapper around AuthSessionManager::RunWhenAvailable that provides all of the
// same functions as RunWithAuthSessionWhenAvailable but in addition will also
// enforce that the session is authenticated for decryption. As such it also
// takes an extra second location to use when the session exists and is OK but
// is not authenticated.
template <typename RequestType, typename ReplyType>
void RunWithAuthorizedAuthSessionWhenAvailable(
    AuthIntent intent,
    AuthSessionManager* auth_session_manager,
    CryptohomeError::ErrorLocationPair not_ok_err_loc,
    CryptohomeError::ErrorLocationPair not_auth_err_loc,
    RequestType request,
    UserDataAuth::OnDoneCallback<ReplyType> on_done,
    UserDataAuth::HandlerWithSessionCallback<RequestType, ReplyType> run_with) {
  // Wrap run_with in a callback which will check if InUseAuthSession is OK and
  // if the session is authenticated. If the session is not okay or not
  // authenticated then call ReplyWithError, wrapping the session status.
  // Otherwise, call run_with and pass on all of the parameters.
  std::string auth_session_id = request.auth_session_id();
  auth_session_manager->RunWhenAvailable(
      auth_session_id,
      base::BindOnce(
          [](AuthIntent intent,
             CryptohomeError::ErrorLocationPair not_ok_err_loc,
             CryptohomeError::ErrorLocationPair not_auth_err_loc,
             RequestType request,
             UserDataAuth::OnDoneCallback<ReplyType> on_done,
             UserDataAuth::HandlerWithSessionCallback<RequestType, ReplyType>
                 run_with,
             InUseAuthSession auth_session) {
            CryptohomeStatus status = auth_session.AuthSessionStatus();
            if (!status.ok()) {
              ReplyWithError(
                  std::move(on_done), ReplyType{},
                  MakeStatus<CryptohomeError>(
                      not_ok_err_loc,
                      ErrorActionSet(
                          {PossibleAction::kDevCheckUnexpectedState}),
                      user_data_auth::CryptohomeErrorCode::
                          CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN)
                      .Wrap(std::move(status).err_status()));
              return;
            }
            if (!auth_session->authorized_intents().contains(intent)) {
              ReplyWithError(
                  std::move(on_done), ReplyType{},
                  MakeStatus<CryptohomeError>(
                      not_auth_err_loc,
                      ErrorActionSet(
                          {PossibleAction::kDevCheckUnexpectedState}),
                      user_data_auth::CryptohomeErrorCode::
                          CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
              return;
            }
            std::move(run_with).Run(std::move(request), std::move(on_done),
                                    std::move(auth_session));
          },
          intent, std::move(not_ok_err_loc), std::move(not_auth_err_loc),
          std::move(request), std::move(on_done), std::move(run_with)));
}

std::optional<user_data_auth::VaultEncryptionType>
MountTypeToVaultEncryptionType(cryptohome::MountType mount_type) {
  switch (mount_type) {
    case cryptohome::MountType::NONE:
    case cryptohome::MountType::EPHEMERAL:
      return std::nullopt;
    case cryptohome::MountType::DMCRYPT:
      return user_data_auth::VaultEncryptionType::
          CRYPTOHOME_VAULT_ENCRYPTION_DMCRYPT;
    case cryptohome::MountType::ECRYPTFS:
    case cryptohome::MountType::ECRYPTFS_TO_DIR_CRYPTO:
    case cryptohome::MountType::ECRYPTFS_TO_DMCRYPT:
      return user_data_auth::VaultEncryptionType::
          CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS;
    case cryptohome::MountType::DIR_CRYPTO:
    case cryptohome::MountType::DIR_CRYPTO_TO_DMCRYPT:
      return user_data_auth::VaultEncryptionType::
          CRYPTOHOME_VAULT_ENCRYPTION_FSCRYPT;
      break;
  }
}

}  // namespace

UserDataAuth::UserDataAuth(BackingApis apis)
    : origin_thread_id_(base::PlatformThread::CurrentId()),
      platform_(apis.platform),
      hwsec_(apis.hwsec),
      hwsec_pw_manager_(apis.hwsec_pw_manager),
      recovery_crypto_(apis.recovery_crypto),
      cryptohome_keys_manager_(apis.cryptohome_keys_manager),
      crypto_(apis.crypto),
      recovery_ab_service_(apis.recovery_ab_service),
      default_chaps_client_(new chaps::TokenManagerClient()),
      chaps_client_(default_chaps_client_.get()),
      default_pkcs11_init_(new Pkcs11Init()),
      pkcs11_init_(default_pkcs11_init_.get()),
      default_pkcs11_token_factory_(new RealPkcs11TokenFactory()),
      pkcs11_token_factory_(default_pkcs11_token_factory_.get()),
      user_activity_timestamp_manager_(apis.user_activity_timestamp_manager),
      keyset_management_(apis.keyset_management),
      uss_storage_(apis.uss_storage),
      uss_manager_(apis.uss_manager),
      auth_factor_manager_(apis.auth_factor_manager),
      disk_cleanup_threshold_(kFreeSpaceThresholdToTriggerCleanup),
      disk_cleanup_aggressive_threshold_(
          kFreeSpaceThresholdToTriggerAggressiveCleanup),
      disk_cleanup_critical_threshold_(
          kFreeSpaceThresholdToTriggerCriticalCleanup),
      disk_cleanup_target_free_space_(kTargetFreeSpaceAfterCleanup),
      guest_user_(brillo::cryptohome::home::GetGuestUsername()),
      async_init_features_(base::BindRepeating(&UserDataAuth::GetFeatures,
                                               base::Unretained(this))) {}

UserDataAuth::~UserDataAuth() {
  if (low_disk_space_handler_) {
    low_disk_space_handler_->Stop();
  }
  if (mount_thread_) {
    mount_thread_->Stop();
  }
}

bool UserDataAuth::Initialize(scoped_refptr<::dbus::Bus> mount_thread_bus) {
  AssertOnOriginThread();

  // Save the bus object. Note that this doesn't mean that mount_thread_bus_ is
  // not null because the passed in Bus can be (and usually is) null.
  mount_thread_bus_ = std::move(mount_thread_bus);

  // Note that we check to see if |origin_task_runner_| and |mount_task_runner_|
  // are available here because they may have been set to an overridden value
  // during unit testing before Initialize() is called.
  if (!origin_task_runner_) {
    origin_task_runner_ = base::SingleThreadTaskRunner::GetCurrentDefault();
  }
  if (!mount_task_runner_) {
    mount_thread_ = std::make_unique<MountThread>(kMountThreadName, this);
    base::Thread::Options options;
    options.message_pump_type = base::MessagePumpType::IO;
    mount_thread_->StartWithOptions(std::move(options));
    mount_task_runner_ = mount_thread_->task_runner();
  }

  // If it hasn't been created yet, start the scrypt thread.
  if (!scrypt_task_runner_) {
    base::Thread::Options options;
    options.message_pump_type = base::MessagePumpType::IO;
    scrypt_thread_ = std::make_unique<base::Thread>("scrypt_thread");
    scrypt_thread_->StartWithOptions(std::move(options));
    scrypt_task_runner_ = scrypt_thread_->task_runner();
  }

  crypto_->Init();

  if (!InitializeFilesystemLayout(platform_, &system_salt_)) {
    LOG(ERROR) << "Failed to initialize filesystem layout.";
    return false;
  }

  AsyncInitPtr<SignallingInterface> async_signalling(base::BindRepeating(
      [](UserDataAuth* uda) -> SignallingInterface* {
        if (uda->signalling_intf_ != &uda->default_signalling_) {
          return uda->signalling_intf_;
        }
        return nullptr;
      },
      base::Unretained(this)));
  fingerprint_service_ = std::make_unique<FingerprintAuthBlockService>(
      AsyncInitPtr<FingerprintManager>(base::BindRepeating(
          [](UserDataAuth* uda) {
            uda->AssertOnMountThread();
            return uda->fingerprint_manager_;
          },
          base::Unretained(this))),
      async_signalling);

  AsyncInitPtr<ChallengeCredentialsHelper> async_cc_helper(base::BindRepeating(
      [](UserDataAuth* uda) -> ChallengeCredentialsHelper* {
        uda->AssertOnMountThread();
        if (uda->challenge_credentials_helper_initialized_) {
          return uda->challenge_credentials_helper_;
        }
        return nullptr;
      },
      base::Unretained(this)));
  AsyncInitPtr<BiometricsAuthBlockService> async_biometrics_service(
      base::BindRepeating(
          [](UserDataAuth* uda) {
            uda->AssertOnMountThread();
            return uda->biometrics_service_;
          },
          base::Unretained(this)));
  AsyncInitPtr<RecoverableKeyStoreBackendCertProvider>
      async_key_store_cert_provider(base::BindRepeating(
          [](UserDataAuth* uda) {
            uda->AssertOnMountThread();
            return uda->key_store_cert_provider_;
          },
          base::Unretained(this)));
  if (!auth_block_utility_) {
    default_auth_block_utility_ = std::make_unique<AuthBlockUtilityImpl>(
        keyset_management_, crypto_, platform_, &async_init_features_,
        scrypt_task_runner_.get(), async_cc_helper,
        key_challenge_service_factory_, async_biometrics_service);
    auth_block_utility_ = default_auth_block_utility_.get();
  }

  if (!auth_factor_driver_manager_) {
    default_auth_factor_driver_manager_ =
        std::make_unique<AuthFactorDriverManager>(
            platform_, crypto_, uss_manager_, async_cc_helper,
            key_challenge_service_factory_, recovery_ab_service_,
            fingerprint_service_.get(), async_biometrics_service,
            &async_init_features_);
    auth_factor_driver_manager_ = default_auth_factor_driver_manager_.get();
  }

  if (!fp_migration_utility_) {
    default_fp_migration_utility_ = std::make_unique<FpMigrationUtility>(
        crypto_, async_biometrics_service, &async_init_features_);
    fp_migration_utility_ = default_fp_migration_utility_.get();
  }

  if (!auth_session_manager_) {
    default_auth_session_manager_ = std::make_unique<AuthSessionManager>(
        AuthSession::BackingApis{
            crypto_, platform_, sessions_, keyset_management_,
            auth_block_utility_, auth_factor_driver_manager_,
            auth_factor_manager_, fp_migration_utility_, uss_storage_,
            uss_manager_, &async_init_features_, async_signalling,
            async_key_store_cert_provider},
        mount_task_runner_.get());
    auth_session_manager_ = default_auth_session_manager_.get();
  }

  create_vault_keyset_impl_ = std::make_unique<CreateVaultKeysetRpcImpl>(
      keyset_management_, hwsec_, auth_block_utility_, auth_factor_manager_,
      auth_factor_driver_manager_);

  if (!vault_factory_) {
    auto container_factory =
        std::make_unique<libstorage::StorageContainerFactory>(platform_,
                                                              GetMetrics());
    container_factory->set_allow_fscrypt_v2(fscrypt_v2_);
    default_vault_factory_ = std::make_unique<CryptohomeVaultFactory>(
        platform_, std::move(container_factory));
    default_vault_factory_->set_enable_application_containers(
        enable_application_containers_);
    vault_factory_ = default_vault_factory_.get();

    if (platform_->IsStatefulLogicalVolumeSupported()) {
      base::FilePath stateful_device = platform_->GetStatefulDevice();
      brillo::LogicalVolumeManager* lvm = platform_->GetLogicalVolumeManager();
      brillo::PhysicalVolume pv(stateful_device,
                                std::make_shared<brillo::LvmCommandRunner>());

      std::optional<brillo::VolumeGroup> vg;
      std::optional<brillo::Thinpool> thinpool;

      vg = lvm->GetVolumeGroup(pv);
      if (vg && vg->IsValid()) {
        thinpool = lvm->GetThinpool(*vg, "thinpool");
      }

      if (thinpool && vg) {
        default_vault_factory_->CacheLogicalVolumeObjects(vg, thinpool);
      }
    }
  }

  if (!homedirs_) {
    // This callback runs in HomeDirs::Remove on |this.homedirs_|. Since
    // |this.keyset_management_| won't be destroyed upon call of Remove(),
    // base::Unretained(keyset_management_) will be valid when the callback
    // runs.
    HomeDirs::RemoveCallback remove_callback =
        base::BindRepeating(&KeysetManagement::RemoveLECredentials,
                            base::Unretained(keyset_management_));
    default_homedirs_ = std::make_unique<HomeDirs>(
        platform_, std::make_unique<policy::PolicyProvider>(), remove_callback,
        vault_factory_);
    homedirs_ = default_homedirs_.get();
  }

  auto homedirs = homedirs_->GetHomeDirs();
  for (const auto& dir : homedirs) {
    user_activity_timestamp_manager_->LoadTimestamp(dir.obfuscated);
  }

  if (!mount_factory_) {
    default_mount_factory_ = std::make_unique<MountFactory>();
    mount_factory_ = default_mount_factory_.get();
  }

  if (!user_session_factory_) {
    default_user_session_factory_ = std::make_unique<RealUserSessionFactory>(
        mount_factory_, platform_, homedirs_, user_activity_timestamp_manager_,
        pkcs11_token_factory_);
    user_session_factory_ = default_user_session_factory_.get();
  }

  if (!low_disk_space_handler_) {
    default_low_disk_space_handler_ = std::make_unique<LowDiskSpaceHandler>(
        homedirs_, platform_, async_signalling,
        user_activity_timestamp_manager_);
    low_disk_space_handler_ = default_low_disk_space_handler_.get();
  }
  low_disk_space_handler_->disk_cleanup()->set_cleanup_threshold(
      disk_cleanup_threshold_);
  low_disk_space_handler_->disk_cleanup()->set_aggressive_cleanup_threshold(
      disk_cleanup_aggressive_threshold_);
  low_disk_space_handler_->disk_cleanup()->set_critical_cleanup_threshold(
      disk_cleanup_critical_threshold_);
  low_disk_space_handler_->disk_cleanup()->set_target_free_space(
      disk_cleanup_target_free_space_);

  if (platform_->FileExists(base::FilePath(kNotFirstBootFilePath))) {
    // Clean up any unreferenced mountpoints at startup.
    PostTaskToMountThread(FROM_HERE,
                          base::BindOnce(
                              [](UserDataAuth* userdataauth) {
                                userdataauth->CleanUpStaleMounts(false);
                              },
                              base::Unretained(this)));
  } else {
    platform_->TouchFileDurable(base::FilePath(kNotFirstBootFilePath));
  }

  low_disk_space_handler_->SetUpdateUserActivityTimestampCallback(
      base::BindRepeating(
          base::IgnoreResult(&UserDataAuth::UpdateCurrentUserActivityTimestamp),
          base::Unretained(this), 0));

  hwsec_->RegisterOnReadyCallback(base::BindOnce(
      &UserDataAuth::HwsecReadyCallback, base::Unretained(this)));

  // Create a dbus connection on mount thread.
  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::CreateMountThreadDBus,
                                       base::Unretained(this)));

  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::SetDeviceManagementProxy,
                                       base::Unretained(this)));

  // SetDeviceManagementProxy() should be invoked before the following
  // initialization, as low_disk_space_handler_ uses homedirs to check the
  // enterprise_owned status.
  if (!low_disk_space_handler_->Init(base::BindRepeating(
          &UserDataAuth::PostTaskToMountThread, base::Unretained(this)))) {
    return false;
  }

  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::CreateFingerprintManager,
                                       base::Unretained(this)));

  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::CreateBiometricsService,
                                       base::Unretained(this)));

  PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuth::CreateRecoverableKeyStoreBackendCertProvider,
          base::Unretained(this)));

  PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuth::InitForChallengeResponseAuth,
                                base::Unretained(this)));

  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::InitializeFeatureLibrary,
                                       base::Unretained(this)));

  return true;
}

void UserDataAuth::CreateMountThreadDBus() {
  AssertOnMountThread();
  if (!mount_thread_bus_) {
    // Setup the D-Bus.
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mount_thread_bus_ = base::MakeRefCounted<dbus::Bus>(options);
    CHECK(mount_thread_bus_->Connect())
        << "Failed to connect to system D-Bus on mount thread";
  }
}

CryptohomeStatusOr<UserPolicyFile*> UserDataAuth::LoadUserPolicyFile(
    const ObfuscatedUsername& obfuscated_username) {
  auto [iter, is_new] = user_policy_files_.try_emplace(
      obfuscated_username, platform_, GetUserPolicyPath(obfuscated_username));
  if (is_new && !iter->second.LoadFromFile().ok()) {
    // The file could not be found, so either the policy file doesn't exist, or
    // the file is corrupted and thus could not be read. Regardless, we need to
    // revert to the default settings (which is an empty file).
    iter->second.UpdateUserPolicy(
        SerializedUserPolicy({.auth_factor_type_policy = {}}));
  }
  return &iter->second;
}

void UserDataAuth::ShutdownTask() {
  default_auth_session_manager_.reset();
  default_fingerprint_manager_.reset();
  default_challenge_credentials_helper_.reset();
  if (mount_thread_bus_) {
    mount_thread_bus_->ShutdownAndBlock();
    mount_thread_bus_.reset();
  }
}

void UserDataAuth::InitializeFeatureLibrary() {
  AssertOnMountThread();
  if (!features_) {
    CHECK(feature::PlatformFeatures::Initialize(mount_thread_bus_));
    default_features_ = std::make_unique<Features>(
        mount_thread_bus_, feature::PlatformFeatures::Get());
    features_ = default_features_.get();
    if (!features_) {
      LOG(WARNING) << "Failed to determine USS migration experiment flag";
      return;
    }
  }
}

void UserDataAuth::SetDeviceManagementProxy() {
  AssertOnMountThread();
  if (homedirs_) {
    homedirs_->CreateAndSetDeviceManagementClientProxy(mount_thread_bus_);
  }
  if (!device_management_client_) {
    default_device_management_client_ =
        std::make_unique<DeviceManagementClientProxy>(mount_thread_bus_);
    device_management_client_ = default_device_management_client_.get();
  }
}

Features* UserDataAuth::GetFeatures() {
  return features_;
}

void UserDataAuth::CreateFingerprintManager() {
  AssertOnMountThread();
  if (!fingerprint_manager_) {
    if (!default_fingerprint_manager_) {
      default_fingerprint_manager_ = FingerprintManager::Create(
          mount_thread_bus_,
          dbus::ObjectPath(std::string(biod::kBiodServicePath)
                               .append(kCrosFpBiometricsManagerRelativePath)));
    }
    fingerprint_manager_ = default_fingerprint_manager_.get();
  }
}

void UserDataAuth::CreateBiometricsService() {
  AssertOnMountThread();
  if (!biometrics_service_) {
    if (!default_biometrics_service_) {
      // This will return nullptr if connection to the biod service failed.
      auto bio_proxy = biod::AuthStackManagerProxyBase::Create(
          mount_thread_bus_,
          dbus::ObjectPath(std::string(biod::kBiodServicePath)
                               .append(CrosFpAuthStackManagerRelativePath)));
      if (bio_proxy) {
        auto bio_processor = std::make_unique<BiometricsCommandProcessorImpl>(
            std::move(bio_proxy));
        default_biometrics_service_ =
            std::make_unique<BiometricsAuthBlockService>(
                std::move(bio_processor),
                base::BindRepeating(&UserDataAuth::OnFingerprintEnrollProgress,
                                    base::Unretained(this)),
                base::BindRepeating(&UserDataAuth::OnFingerprintAuthProgress,
                                    base::Unretained(this)));
      }
    }
    biometrics_service_ = default_biometrics_service_.get();
  }
}

void UserDataAuth::OnFingerprintEnrollProgress(
    user_data_auth::AuthEnrollmentProgress result) {
  AssertOnMountThread();
  ReportFingerprintEnrollSignal(result.scan_result().fingerprint_result());
  user_data_auth::PrepareAuthFactorProgress progress;
  user_data_auth::PrepareAuthFactorForAddProgress add_progress;
  add_progress.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  *add_progress.mutable_biometrics_progress() = result;
  progress.set_purpose(user_data_auth::PURPOSE_ADD_AUTH_FACTOR);
  *progress.mutable_add_progress() = add_progress;
  signalling_intf_->SendPrepareAuthFactorProgress(progress);
}

void UserDataAuth::OnFingerprintAuthProgress(
    user_data_auth::AuthScanDone result) {
  AssertOnMountThread();
  ReportFingerprintAuthSignal(result.scan_result().fingerprint_result());
  user_data_auth::PrepareAuthFactorProgress progress;
  user_data_auth::PrepareAuthFactorForAuthProgress auth_progress;
  auth_progress.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  *auth_progress.mutable_biometrics_progress() = result;
  progress.set_purpose(user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);
  *progress.mutable_auth_progress() = auth_progress;
  signalling_intf_->SendPrepareAuthFactorProgress(progress);
}

void UserDataAuth::CreateRecoverableKeyStoreBackendCertProvider() {
  AssertOnMountThread();
  if (!key_store_cert_provider_) {
    if (!default_key_store_cert_provider_) {
      default_key_store_cert_provider_ =
          std::make_unique<RecoverableKeyStoreBackendCertProviderImpl>(
              platform_, std::make_unique<org::chromium::RksAgentProxy>(
                             mount_thread_bus_));
    }
    key_store_cert_provider_ = default_key_store_cert_provider_.get();
  }
}

bool UserDataAuth::PostTaskToOriginThread(const base::Location& from_here,
                                          base::OnceClosure task,
                                          const base::TimeDelta& delay) {
  if (delay.is_zero()) {
    return origin_task_runner_->PostTask(from_here, std::move(task));
  }
  return origin_task_runner_->PostDelayedTask(from_here, std::move(task),
                                              delay);
}

bool UserDataAuth::PostTaskToMountThread(const base::Location& from_here,
                                         base::OnceClosure task,
                                         const base::TimeDelta& delay) {
  CHECK(mount_task_runner_);
  if (delay.is_zero()) {
    // Increase and report the parallel task count.
    parallel_task_count_ += 1;

    // Reduce the parallel task count after finished the task.
    auto full_task = base::BindOnce(
        [](base::OnceClosure task, std::atomic<int>* task_count) {
          std::move(task).Run();
          *task_count -= 1;
        },
        std::move(task), base::Unretained(&parallel_task_count_));

    return mount_task_runner_->PostTask(from_here, std::move(full_task));
  }
  return mount_task_runner_->PostDelayedTask(from_here, std::move(task), delay);
}

bool UserDataAuth::IsMounted(const Username& username, bool* is_ephemeral_out) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  bool is_mounted = false;
  bool is_ephemeral = false;
  if (username->empty()) {
    // No username is specified, so we consider "the cryptohome" to be mounted
    // if any existing cryptohome is mounted.
    for (const auto& [unused, session] : *sessions_) {
      if (session.IsActive()) {
        is_mounted = true;
        is_ephemeral |= session.IsEphemeral();
      }
    }
  } else {
    // A username is specified, check the associated mount object.
    const UserSession* session = sessions_->Find(username);

    if (session) {
      is_mounted = session->IsActive();
      is_ephemeral = is_mounted && session->IsEphemeral();
    }
  }

  if (is_ephemeral_out) {
    *is_ephemeral_out = is_ephemeral;
  }

  return is_mounted;
}

user_data_auth::GetVaultPropertiesReply UserDataAuth::GetVaultProperties(
    user_data_auth::GetVaultPropertiesRequest request) {
  // Note: This can only run in mount_thread_.
  AssertOnMountThread();
  user_data_auth::GetVaultPropertiesReply reply;

  if (request.username().empty()) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUsernameEmpty),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  // A username is specified, find the session.
  const UserSession* session = sessions_->Find(Username(request.username()));
  if (!session) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFound),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  if (!session->IsActive()) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotActivity),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  auto mount_type = MountTypeToVaultEncryptionType(session->GetMountType());
  if (!mount_type.has_value()) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoMountFound),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  reply.set_encryption_type(mount_type.value());
  PopulateReplyWithError(OkStatus<CryptohomeError>(), &reply);
  return reply;
}

bool UserDataAuth::RemoveAllMounts() {
  AssertOnMountThread();

  bool success = true;
  while (!sessions_->empty()) {
    const auto& [username, session] = *sessions_->begin();
    if (session.IsActive() && !session.Unmount()) {
      success = false;
    }
    if (!sessions_->Remove(username)) {
      LOG(ERROR) << "Failed to remove user session on unmount";
    }
  }
  return success;
}

bool UserDataAuth::FilterActiveMounts(
    std::multimap<const FilePath, const FilePath>* mounts,
    std::multimap<const FilePath, const FilePath>* active_mounts,
    bool include_busy_mount) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  bool skipped = false;
  absl::flat_hash_set<FilePath> children_to_preserve;

  for (auto match = mounts->begin(); match != mounts->end();) {
    // curr->first is the source device of the group that we are processing in
    // this outer loop.
    auto curr = match;
    bool keep = false;

    // Note that we organize the set of mounts with the same source, then
    // process them together. That is, say there's /dev/mmcblk0p1 mounted on
    // /home/user/xxx and /home/chronos/u-xxx/MyFiles/Downloads. They are both
    // from the same source (/dev/mmcblk0p1, or match->first). In this case,
    // we'll decide the fate of all mounts with the same source together. For
    // each such group, the outer loop will run once. The inner loop will
    // iterate through every mount in the group with |match| variable, looking
    // to see if it's owned by any active mounts. If it is, the entire group is
    // kept. Otherwise, (and assuming no open files), the entire group is
    // discarded, as in, not moved into the active_mounts multimap.

    // Walk each set of sources as one group since multimaps are key ordered.
    for (; match != mounts->end() && match->first == curr->first; ++match) {
      // Ignore known mounts.
      for (const auto& [unused, session] : *sessions_) {
        if (session.OwnsMountPoint(match->second)) {
          keep = true;
          // If !include_busy_mount, other mount points not owned scanned after
          // should be preserved as well.
          if (include_busy_mount) {
            break;
          }
        }
      }

      // Ignore mounts pointing to children of used mounts.
      if (!include_busy_mount) {
        if (children_to_preserve.contains(match->second)) {
          keep = true;
          skipped = true;
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " is a just a child.";
        }
      }

      // Optionally, ignore mounts with open files.
      if (!keep && !include_busy_mount) {
        // Mark the mount points that are not in use as 'expired'. Add the mount
        // points to the |active_mounts| list if they are not expired.
        libstorage::ExpireMountResult expire_mount_result =
            platform_->ExpireMount(match->second);
        if (expire_mount_result == libstorage::ExpireMountResult::kBusy) {
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " has active holders.";
          keep = true;
          skipped = true;
        } else if (expire_mount_result ==
                   libstorage::ExpireMountResult::kError) {
          // To avoid unloading any pkcs11 token that is in use, add mount point
          // to the |active_mounts| if it is failed to be expired.
          LOG(ERROR) << "Stale mount " << match->second.value() << " from "
                     << match->first.value()
                     << " failed to be removed from active mounts list.";
          keep = true;
          skipped = true;
        }
      }
    }
    if (keep) {
      std::multimap<const FilePath, const FilePath> children;
      LOG(WARNING) << "Looking for children of " << curr->first;
      platform_->GetMountsBySourcePrefix(curr->first, &children);
      for (const auto& child : children) {
        children_to_preserve.insert(child.second);
      }

      active_mounts->insert(curr, match);
      mounts->erase(curr, match);
    }
  }
  return skipped;
}

void UserDataAuth::GetEphemeralLoopDevicesMounts(
    std::multimap<const FilePath, const FilePath>* mounts) {
  AssertOnMountThread();
  std::multimap<const FilePath, const FilePath> loop_mounts;
  platform_->GetLoopDeviceMounts(&loop_mounts);

  const FilePath sparse_path =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir);
  for (const auto& device : platform_->GetAttachedLoopDevices()) {
    // Ephemeral mounts are mounts from a loop device with ephemeral sparse
    // backing file.
    if (sparse_path.IsParent(device.backing_file)) {
      auto range = loop_mounts.equal_range(device.device);
      mounts->insert(range.first, range.second);
    }
  }
}

bool UserDataAuth::UnloadPkcs11Tokens(const std::vector<FilePath>& exclude) {
  AssertOnMountThread();

  SecureBlob isolate =
      chaps::IsolateCredentialManager::GetDefaultIsolateCredential();
  std::vector<std::string> tokens;
  if (!chaps_client_->GetTokenList(isolate, &tokens)) {
    return false;
  }
  for (const std::string& token : tokens) {
    if (token != chaps::kSystemTokenPath && !PrefixPresent(exclude, token)) {
      // It's not a system token and is not under one of the excluded path.
      LOG(INFO) << "Unloading up PKCS #11 token: " << token;
      chaps_client_->UnloadToken(isolate, FilePath(token));
    }
  }
  return true;
}

bool UserDataAuth::CleanUpStaleMounts(bool force) {
  AssertOnMountThread();

  // This function is meant to aid in a clean recovery from a crashed or
  // manually restarted cryptohomed.  Cryptohomed may restart:
  // 1. Before any mounts occur
  // 2. While mounts are active
  // 3. During an unmount
  // In case #1, there should be no special work to be done.
  // The best way to disambiguate #2 and #3 is to determine if there are
  // any active open files on any stale mounts.  If there are open files,
  // then we've likely(*) resumed an active session. If there are not,
  // the last cryptohome should have been unmounted.
  // It's worth noting that a restart during active use doesn't impair
  // other user session behavior, like CheckKey, because it doesn't rely
  // exclusively on mount state.
  //
  // In the future, it may make sense to attempt to keep the MountMap
  // persisted to disk which would make resumption much easier.
  //
  // (*) Relies on the expectation that all processes have been killed off.

  // Stale shadow and ephemeral mounts.
  std::multimap<const FilePath, const FilePath> shadow_mounts;
  std::multimap<const FilePath, const FilePath> ephemeral_mounts;
  std::multimap<const FilePath, const FilePath> dmcrypt_mounts,
      grouped_dmcrypt_mounts;

  // Active mounts that we don't intend to unmount.
  std::multimap<const FilePath, const FilePath> active_mounts;

  // Retrieve all the mounts that's currently mounted by the kernel and concerns
  // us
  platform_->GetMountsBySourcePrefix(ShadowRoot(), &shadow_mounts);
  platform_->GetMountsByDevicePrefix(kDeviceMapperDevicePrefix,
                                     &dmcrypt_mounts);
  GroupDmcryptDeviceMounts(&dmcrypt_mounts, &grouped_dmcrypt_mounts);
  GetEphemeralLoopDevicesMounts(&ephemeral_mounts);

  // Remove mounts that we've a record of or have open files on them
  bool skipped =
      FilterActiveMounts(&shadow_mounts, &active_mounts, force) ||
      FilterActiveMounts(&ephemeral_mounts, &active_mounts, force) ||
      FilterActiveMounts(&grouped_dmcrypt_mounts, &active_mounts, force);

  // Unload PKCS#11 tokens on any mount that we're going to unmount.
  std::vector<FilePath> excluded_mount_points;
  for (const auto& mount : active_mounts) {
    excluded_mount_points.push_back(mount.second);
  }
  UnloadPkcs11Tokens(excluded_mount_points);

  // Unmount anything left.
  for (const auto& match : grouped_dmcrypt_mounts) {
    LOG(WARNING) << "Lazily unmounting stale dmcrypt mount: "
                 << match.second.value() << " for " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
  }

  for (const auto& match : shadow_mounts) {
    LOG(WARNING) << "Lazily unmounting stale shadow mount: "
                 << match.second.value() << " from " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
  }

  // Attempt to clear the encryption key for the shadow directories once
  // the mount has been unmounted. The encryption key needs to be cleared
  // after all the unmounts are done to ensure that none of the existing
  // submounts becomes inaccessible.
  if (force && !shadow_mounts.empty()) {
    // Attempt to clear fscrypt encryption keys for the shadow mounts.
    for (const auto& match : shadow_mounts) {
      if (!platform_->InvalidateDirCryptoKey(dircrypto::KeyReference(),
                                             match.first)) {
        LOG(WARNING) << "Failed to clear fscrypt keys for stale mount: "
                     << match.first;
      }
    }

    // Clear all keys in the user keyring for ecryptfs mounts.
    if (!platform_->ClearUserKeyring()) {
      LOG(WARNING) << "Failed to clear stale user keys.";
    }
  }
  for (const auto& match : ephemeral_mounts) {
    LOG(WARNING) << "Lazily unmounting stale ephemeral mount: "
                 << match.second.value() << " from " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
    // Clean up destination directory for ephemeral mounts under ephemeral
    // cryptohome dir.
    if (base::StartsWith(match.first.value(), libstorage::kLoopPrefix,
                         base::CompareCase::SENSITIVE) &&
        FilePath(kEphemeralCryptohomeDir).IsParent(match.second)) {
      platform_->DeletePathRecursively(match.second);
    }
  }

  // Clean up all stale sparse files, this is comprised of two stages:
  // 1. Clean up stale loop devices.
  // 2. Clean up stale sparse files.
  // Note that some mounts are backed by loop devices, and loop devices are
  // backed by sparse files.

  std::vector<libstorage::Platform::LoopDevice> loop_devices =
      platform_->GetAttachedLoopDevices();
  const FilePath sparse_dir =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir);
  std::vector<FilePath> stale_sparse_files;
  platform_->EnumerateDirectoryEntries(sparse_dir, false /* is_recursive */,
                                       &stale_sparse_files);

  // We'll go through all loop devices, and for every of them, we'll see if we
  // can remove it. Also in the process, we'll get to keep track of which sparse
  // files are actually used by active loop devices.
  for (const auto& device : loop_devices) {
    // Check whether the loop device is created from an ephemeral sparse file.
    if (!sparse_dir.IsParent(device.backing_file)) {
      // Nah, it's this loop device is not backed by an ephemeral sparse file
      // created by cryptohome, so we'll leave it alone.
      continue;
    }

    // Check if any of our active mounts are backed by this loop device.
    if (active_mounts.count(device.device) == 0) {
      // Nope, this loop device have nothing to do with our active mounts.
      LOG(WARNING) << "Detaching stale loop device: " << device.device.value();
      if (!platform_->DetachLoop(device.device)) {
        PLOG(ERROR) << "Can't detach stale loop: " << device.device.value();
        ReportCryptohomeError(kEphemeralCleanUpFailed);
      }
    } else {
      // This loop device backs one of our active_mounts, so we can't count it
      // as stale. Thus removing from the stale_sparse_files list.
      stale_sparse_files.erase(
          std::remove(stale_sparse_files.begin(), stale_sparse_files.end(),
                      device.backing_file),
          stale_sparse_files.end());
    }
  }

  // Now we clean up the stale sparse files.
  for (const auto& file : stale_sparse_files) {
    LOG(WARNING) << "Deleting stale ephemeral backing sparse file: "
                 << file.value();
    if (!platform_->DeleteFile(file)) {
      PLOG(ERROR) << "Failed to clean up ephemeral sparse file: "
                  << file.value();
      ReportCryptohomeError(kEphemeralCleanUpFailed);
    }
  }

  return skipped;
}

user_data_auth::UnmountReply UserDataAuth::Unmount() {
  AssertOnMountThread();

  bool unmount_ok = RemoveAllMounts();

  // If there are any unexpected mounts lingering from a crash/restart,
  // clean them up now.
  // Note that we do not care about the return value of CleanUpStaleMounts()
  // because it doesn't matter if any mount is skipped due to open files, and
  // additionally, since we've specified force=true, it'll not skip over mounts
  // with open files.
  CleanUpStaleMounts(true);

  // Removes all ephemeral cryptohomes owned by anyone other than the owner
  // user (if set) and non ephemeral users, regardless of free disk space.
  homedirs_->RemoveCryptohomesBasedOnPolicy();

  // Since all the user mounts are now gone, there should not be any active
  // authsessions left. Remove them all and discard any loaded state related to
  // them such as loaded USS data.
  CryptohomeStatus result = TerminateAuthSessionsAndClearLoadedState();

  // If the unmount failed, reporting the error there takes priority over the
  // failed termination of auth sessions.
  if (!unmount_ok) {
    result = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthRemoveAllMountsFailedInUnmount),
        ErrorActionSet({PossibleAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  user_data_auth::UnmountReply reply;
  PopulateReplyWithError(result, &reply);
  return reply;
}

void UserDataAuth::InitializePkcs11(UserSession* session) {
  AssertOnMountThread();

  // We should not pass nullptr to this method.
  CHECK(session);

  bool still_mounted = false;

  // The mount has to be mounted, that is, still tracked by cryptohome.
  // Otherwise there's no point in initializing PKCS#11 for it. The reason for
  // this check is because it might be possible for Unmount() to be called after
  // mounting and before getting here.
  for (const auto& [unused, user_session] : *sessions_) {
    if (&user_session == session && session->IsActive()) {
      still_mounted = true;
      break;
    }
  }

  if (!still_mounted) {
    LOG(WARNING)
        << "PKCS#11 initialization requested but cryptohome is not mounted.";
    return;
  }

  // Note that the timer stops in the Mount class' method.
  ReportTimerStart(kPkcs11InitTimer);

  if (session->GetPkcs11Token()) {
    session->GetPkcs11Token()->Insert();
  }

  ReportTimerStop(kPkcs11InitTimer);

  LOG(INFO) << "PKCS#11 initialization succeeded.";
}

void UserDataAuth::Pkcs11RestoreTpmTokens() {
  AssertOnMountThread();

  for (const auto& [unused, session] : *sessions_) {
    if (session.IsActive()) {
      session.GetPkcs11Token()->TryRestoring();
    }
  }
}

void UserDataAuth::EnsureCryptohomeKeys() {
  if (!IsOnMountThread()) {
    // We are not on mount thread, but to be safe, we'll only access Mount
    // objects on mount thread, so let's post ourself there.
    PostTaskToMountThread(FROM_HERE,
                          base::BindOnce(&UserDataAuth::EnsureCryptohomeKeys,
                                         base::Unretained(this)));
    return;
  }

  AssertOnMountThread();

  if (!cryptohome_keys_manager_->HasAnyCryptohomeKey()) {
    cryptohome_keys_manager_->Init();
  }
}

void UserDataAuth::set_cleanup_threshold(uint64_t cleanup_threshold) {
  disk_cleanup_threshold_ = cleanup_threshold;
}

void UserDataAuth::set_aggressive_cleanup_threshold(
    uint64_t aggressive_cleanup_threshold) {
  disk_cleanup_aggressive_threshold_ = aggressive_cleanup_threshold;
}

void UserDataAuth::set_critical_cleanup_threshold(
    uint64_t critical_cleanup_threshold) {
  disk_cleanup_critical_threshold_ = critical_cleanup_threshold;
}

void UserDataAuth::set_target_free_space(uint64_t target_free_space) {
  disk_cleanup_target_free_space_ = target_free_space;
}

void UserDataAuth::SetSignallingInterface(SignallingInterface& signalling) {
  signalling_intf_ = &signalling;
}

void UserDataAuth::HwsecReadyCallback(hwsec::Status status) {
  if (!IsOnMountThread()) {
    // We are not on mount thread, so let's post ourself there.
    PostTaskToMountThread(
        FROM_HERE, base::BindOnce(&UserDataAuth::HwsecReadyCallback,
                                  base::Unretained(this), std::move(status)));
    return;
  }

  AssertOnMountThread();

  if (!status.ok()) {
    LOG(ERROR) << "HwsecReadyCallback failed: " << status;
    return;
  }

  // Make sure cryptohome keys are loaded and ready for every mount.
  EnsureCryptohomeKeys();
}

void UserDataAuth::EnsureBootLockboxFinalized() {
  AssertOnMountThread();

  // Lock NVRamBootLockbox
  auto nvram_boot_lockbox_client =
      bootlockbox::BootLockboxClient::CreateBootLockboxClient();
  if (!nvram_boot_lockbox_client) {
    LOG(WARNING) << "Failed to create nvram_boot_lockbox_client";
    return;
  }

  if (!nvram_boot_lockbox_client->Finalize()) {
    LOG(WARNING) << "Failed to finalize nvram lockbox.";
  }
}

void UserDataAuth::BlockPkEstablishment() {
  AssertOnMountThread();

  if (pk_establishment_blocked_) {
    return;
  }

  hwsec::StatusOr<bool> enabled = hwsec_pw_manager_->IsEnabled();
  if (!enabled.ok() || !*enabled) {
    return;
  }

  // Pk related mechanisms are only added in PW version 2.
  hwsec::StatusOr<uint8_t> version = hwsec_pw_manager_->GetVersion();
  if (!version.ok() || *version <= 1) {
    return;
  }

  hwsec::Status status = hwsec_pw_manager_->BlockGeneratePk();
  if (!status.ok()) {
    LOG(WARNING) << "Block biometrics Pk establishment failed: "
                 << status.status();
  } else {
    pk_establishment_blocked_ = true;
    if (!platform_->FileExists(
            base::FilePath(kPinweaverPkEstablishmentBlocked))) {
      platform_->TouchFileDurable(
          base::FilePath(kPinweaverPkEstablishmentBlocked));
    }
  }
}

UserSession* UserDataAuth::GetOrCreateUserSession(const Username& username) {
  // This method touches the |sessions_| object so it needs to run on
  // |mount_thread_|
  AssertOnMountThread();
  UserSession* session = sessions_->Find(username);
  if (!session) {
    // Lock bootlockbox as we considered the device becoming more vulnerable to
    // attackers.
    EnsureBootLockboxFinalized();
    // Block biometrics pairing key establishment afterwards as we considered
    // the device becoming more vulnerable to attackers.
    BlockPkEstablishment();
    // We don't have a mount associated with |username|, let's create one.
    std::unique_ptr<UserSession> owned_session = user_session_factory_->New(
        username, legacy_mount_, /*bind_mount_downloads*/ false);
    session = owned_session.get();
    if (!sessions_->Add(username, std::move(owned_session))) {
      LOG(ERROR) << "Failed to add created user session";
      return nullptr;
    }
  }
  return session;
}

void UserDataAuth::RemoveInactiveUserSession(const Username& username) {
  AssertOnMountThread();

  UserSession* session = sessions_->Find(username);
  if (!session || session->IsActive()) {
    return;
  }

  if (!sessions_->Remove(username)) {
    LOG(ERROR) << "Failed to remove inactive user session.";
  }
}

void UserDataAuth::InitForChallengeResponseAuth() {
  AssertOnMountThread();
  if (challenge_credentials_helper_initialized_) {
    // Already successfully initialized.
    return;
  }

  if (!challenge_credentials_helper_) {
    // Lazily create the helper object that manages generation/decryption of
    // credentials for challenge-protected vaults.
    default_challenge_credentials_helper_ =
        std::make_unique<ChallengeCredentialsHelperImpl>(hwsec_);
    challenge_credentials_helper_ = default_challenge_credentials_helper_.get();
  }

  if (!mount_thread_bus_) {
    LOG(ERROR) << "Cannot do challenge-response mount without system D-Bus bus";
    return;
  }
  key_challenge_service_factory_->SetMountThreadBus(mount_thread_bus_);

  challenge_credentials_helper_initialized_ = true;
}

void UserDataAuth::Remove(user_data_auth::RemoveRequest request,
                          OnDoneCallback<user_data_auth::RemoveReply> on_done) {
  AssertOnMountThread();

  if (!request.has_identifier() && request.auth_session_id().empty()) {
    // RemoveRequest must have identifier or an AuthSession Id
    ReplyWithError(
        std::move(on_done), {},
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoIDInRemove),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // If the caller supplies an account identifier then we need to start a new
  // session to do the cleanup.
  if (request.auth_session_id().empty()) {
    base::UnguessableToken token = auth_session_manager_->CreateAuthSession(
        GetAccountId(request.identifier()),
        {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
    // Rewrite the request to use the new session ID and not the account ID.
    request.clear_identifier();
    request.set_auth_session_id(
        AuthSession::GetSerializedStringFromToken(token));
  }

  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInRemove),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::RemoveWithSession, base::Unretained(this)));
}

void UserDataAuth::RemoveWithSession(
    user_data_auth::RemoveRequest request,
    OnDoneCallback<user_data_auth::RemoveReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::RemoveReply reply;
  LOG(INFO) << "UDA: Starting removal.";

  Username account_id = auth_session->username();
  if (account_id->empty()) {
    // RemoveRequest must have valid account_id.
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthNoAccountIdWithAuthSessionInRemove),
                       ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                                       PossibleAction::kReboot}),
                       user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  ObfuscatedUsername obfuscated = SanitizeUserName(account_id);

  const UserSession* const session = sessions_->Find(account_id);
  if (session && session->IsActive()) {
    LOG(ERROR) << "UDA: User removal failed, user is still active.";
    // Can't remove active user
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUserActiveInRemove),
            ErrorActionSet({PossibleAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
    return;
  }

  AuthSession* auth_session_ptr = auth_session.Get();
  auth_session_ptr->PrepareUserForRemoval(base::BindOnce(
      &UserDataAuth::OnPreparedUserForRemoval, base::Unretained(this),
      obfuscated, std::move(auth_session).BindForCallback(),
      std::move(on_done)));
}

void UserDataAuth::OnPreparedUserForRemoval(
    const ObfuscatedUsername& obfuscated,
    InUseAuthSession auth_session,
    base::OnceCallback<void(const user_data_auth::RemoveReply&)> on_done) {
  user_data_auth::RemoveReply reply;
  if (!homedirs_->Remove(obfuscated)) {
    LOG(ERROR) << "UDA: User removal failed, unable to remove homedir.";
    // User vault removal failed.
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(kLocUserDataAuthRemoveFailedInRemove),
                       ErrorActionSet({PossibleAction::kPowerwash,
                                       PossibleAction::kReboot}),
                       user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED));
    return;
  }

  // Since the user is now removed, any further operations require a fresh
  // AuthSession. So terminate ALL auth sessions for the user.
  auth_session_manager_->RemoveUserAuthSessions(obfuscated);
  std::move(auth_session).Release();

  // Send RemoveCompleted signal.
  user_data_auth::RemoveCompleted signal;
  signal.set_sanitized_username(*obfuscated);
  signalling_intf_->SendRemoveCompleted(signal);
  LOG(INFO) << "UDA: User removal completed.";

  // We should have removed the auth sessions of the user-to-be-removed. Try
  // to unload the encrypted USS from manager otherwise the same account can't
  // be added again. If the unload failed, the same account can't be added again
  // until the next boot.
  CryptohomeStatus status = uss_manager_->DiscardEncrypted(obfuscated);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to discard encrypted USS: " << status;
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

user_data_auth::ResetApplicationContainerReply
UserDataAuth::ResetApplicationContainer(
    const user_data_auth::ResetApplicationContainerRequest& request) {
  AssertOnMountThread();
  user_data_auth::ResetApplicationContainerReply reply;
  Username account_id = GetAccountId(request.account_id());

  if (account_id->empty() || request.application_name().empty()) {
    // RemoveRequest must have identifier or an AuthSession Id
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoIDInResetAppContainer),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  UserSession* session = sessions_->Find(account_id);
  if (!session || !session->IsActive()) {
    // Can't reset container of inactive user.
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUserInactiveInResetAppContainer),
            ErrorActionSet({PossibleAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY),
        &reply);
    return reply;
  }

  if (!session->ResetApplicationContainer(request.application_name())) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUserFailedResetAppContainer),
            ErrorActionSet({PossibleAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY),
        &reply);
    return reply;
  }

  PopulateReplyWithError(OkStatus<CryptohomeError>(), &reply);
  return reply;
}

user_data_auth::SetUserDataStorageWriteEnabledReply
UserDataAuth::SetUserDataStorageWriteEnabled(
    const user_data_auth::SetUserDataStorageWriteEnabledRequest& request) {
  AssertOnMountThread();
  user_data_auth::SetUserDataStorageWriteEnabledReply reply;
  Username account_id = GetAccountId(request.account_id());

  if (account_id->empty()) {
    // Request must have identifier
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoIDInSetUserDataStorageWriteEnabled),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  UserSession* session = sessions_->Find(account_id);
  if (!session || !session->IsActive()) {
    // Can't reset container of inactive user.
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthUserInactiveInSetUserDataStorageWriteEnabled),
            ErrorActionSet({PossibleAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY),
        &reply);
    return reply;
  }

  if (!session->EnableWriteUserDataStorage(request.enabled())) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthUserFailedToSetUserDataStorageWriteEnabled),
            ErrorActionSet({PossibleAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY),
        &reply);
    return reply;
  }

  PopulateReplyWithError(OkStatus<CryptohomeError>(), &reply);
  return reply;
}

void UserDataAuth::StartMigrateToDircrypto(
    user_data_auth::StartMigrateToDircryptoRequest request,
    OnDoneCallback<user_data_auth::StartMigrateToDircryptoReply> on_done,
    Mount::MigrationCallback progress_callback) {
  AssertOnMountThread();

  // If the request does not specify an auth session then just directly execute
  // using the specified username.
  if (request.auth_session_id().empty()) {
    Username username = GetAccountId(request.account_id());
    StartMigrateToDircryptoWithUsername(std::move(request), std::move(on_done),
                                        std::move(progress_callback),
                                        std::move(username));
    return;
  }

  // Schedule the request to run with the username associated with the specified
  // auth session once that session is available to run.
  std::string auth_session_id = request.auth_session_id();
  auth_session_manager_->RunWhenAvailable(
      auth_session_id,
      base::BindOnce(
          [](user_data_auth::StartMigrateToDircryptoRequest request,
             OnDoneCallback<user_data_auth::StartMigrateToDircryptoReply>
                 on_done,
             Mount::MigrationCallback progress_callback, UserDataAuth* this_uda,
             InUseAuthSession auth_session) {
            CryptohomeStatus status = auth_session.AuthSessionStatus();
            if (!status.ok()) {
              LOG(ERROR) << "StartMigrateToDircrypto: Invalid auth_session_id.";
              user_data_auth::DircryptoMigrationProgress progress;
              progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
              progress_callback.Run(progress);
              // Note that we still reply with "ok" because failures are
              // reported via the progress callback.
              ReplyWithError(std::move(on_done),
                             user_data_auth::StartMigrateToDircryptoReply{},
                             OkStatus<CryptohomeError>());
              return;
            }
            this_uda->StartMigrateToDircryptoWithUsername(
                std::move(request), std::move(on_done),
                std::move(progress_callback), auth_session->username());
          },
          std::move(request), std::move(on_done), std::move(progress_callback),
          this));
}

void UserDataAuth::StartMigrateToDircryptoWithUsername(
    user_data_auth::StartMigrateToDircryptoRequest request,
    OnDoneCallback<user_data_auth::StartMigrateToDircryptoReply> on_done,
    Mount::MigrationCallback progress_callback,
    Username username) {
  MigrationType migration_type = request.minimal_migration()
                                     ? MigrationType::MINIMAL
                                     : MigrationType::FULL;
  user_data_auth::StartMigrateToDircryptoReply reply;
  user_data_auth::DircryptoMigrationProgress progress;

  // Note that total_bytes and current_bytes field in |progress| is discarded by
  // client whenever |progress.status| is not DIRCRYPTO_MIGRATION_IN_PROGRESS,
  // this is why they are left with the default value of 0 here.
  UserSession* const session = sessions_->Find(username);
  if (!session) {
    LOG(ERROR) << "StartMigrateToDircrypto: Failed to get session.";
    progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
    progress_callback.Run(progress);
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    return;
  }
  LOG(INFO) << "StartMigrateToDircrypto: Migrating to dircrypto.";
  if (!session->MigrateVault(progress_callback, migration_type)) {
    LOG(ERROR) << "StartMigrateToDircrypto: Failed to migrate.";
    progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
    progress_callback.Run(progress);
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    return;
  }
  LOG(INFO) << "StartMigrateToDircrypto: Migration done.";
  progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
  progress_callback.Run(progress);
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

user_data_auth::CryptohomeErrorCode UserDataAuth::NeedsDircryptoMigration(
    const AccountIdentifier& account, bool* result) {
  AssertOnMountThread();
  ObfuscatedUsername obfuscated_username =
      SanitizeUserName(GetAccountId(account));
  if (!homedirs_->Exists(obfuscated_username)) {
    LOG(ERROR) << "Unknown user in NeedsDircryptoMigration.";
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  *result = !force_ecryptfs_ &&
            homedirs_->NeedsDircryptoMigration(obfuscated_username);
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

bool UserDataAuth::IsLowEntropyCredentialSupported() {
  AssertOnOriginThread();
  hwsec::StatusOr<bool> is_enabled = hwsec_->IsPinWeaverEnabled();
  if (!is_enabled.ok()) {
    LOG(ERROR) << "Failed to get pinweaver status";
    return false;
  }
  return is_enabled.value();
}

int64_t UserDataAuth::GetAccountDiskUsage(const AccountIdentifier& account) {
  AssertOnMountThread();
  // Note that if the given |account| is invalid or non-existent, then HomeDirs'
  // implementation of ComputeDiskUsage is specified to return 0.
  return homedirs_->ComputeDiskUsage(SanitizeUserName(GetAccountId(account)));
}

bool UserDataAuth::Pkcs11IsTpmTokenReady() {
  AssertOnMountThread();
  // We touched the sessions_ object, so we need to be on mount thread.

  for (const auto& [unused, session] : *sessions_) {
    if (!session.GetPkcs11Token() || !session.GetPkcs11Token()->IsReady()) {
      return false;
    }
  }

  return true;
}

user_data_auth::TpmTokenInfo UserDataAuth::Pkcs11GetTpmTokenInfo(
    const Username& username) {
  AssertOnOriginThread();
  user_data_auth::TpmTokenInfo result;
  std::string label, pin;
  CK_SLOT_ID slot;
  FilePath token_path;
  if (username->empty()) {
    // We want to get the system token.

    // Get the label and pin for system token.
    pkcs11_init_->GetTpmTokenInfo(&label, &pin);

    token_path = FilePath(chaps::kSystemTokenPath);
  } else {
    // We want to get the user token.

    // Get the label and pin for user token.
    pkcs11_init_->GetTpmTokenInfoForUser(username, &label, &pin);

    token_path = homedirs_->GetChapsTokenDir(SanitizeUserName(username));
  }

  result.set_label(label);
  result.set_user_pin(pin);

  if (!pkcs11_init_->GetTpmTokenSlotForPath(token_path, &slot)) {
    // Failed to get the slot, let's use -1 for default.
    slot = -1;
  }
  result.set_slot(slot);

  return result;
}

void UserDataAuth::Pkcs11Terminate() {
  AssertOnMountThread();
  // We are touching the |sessions_| object so we need to be on mount thread.

  for (const auto& [unused, session] : *sessions_) {
    if (session.GetPkcs11Token()) {
      session.GetPkcs11Token()->Remove();
    }
  }
}

user_data_auth::GetWebAuthnSecretReply UserDataAuth::GetWebAuthnSecret(
    const user_data_auth::GetWebAuthnSecretRequest& request) {
  AssertOnMountThread();
  user_data_auth::GetWebAuthnSecretReply reply;

  if (!request.has_account_id()) {
    LOG(ERROR) << "GetWebAuthnSecretRequest must have account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  Username account_id = GetAccountId(request.account_id());
  if (account_id->empty()) {
    LOG(ERROR) << "GetWebAuthnSecretRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  UserSession* const session = sessions_->Find(account_id);
  std::unique_ptr<brillo::SecureBlob> secret;
  if (session) {
    secret = session->GetWebAuthnSecret();
  }
  if (!secret) {
    LOG(ERROR) << "Failed to get WebAuthn secret.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    return reply;
  }

  reply.set_webauthn_secret(secret->to_string());
  return reply;
}

user_data_auth::GetWebAuthnSecretHashReply UserDataAuth::GetWebAuthnSecretHash(
    const user_data_auth::GetWebAuthnSecretHashRequest& request) {
  AssertOnMountThread();
  user_data_auth::GetWebAuthnSecretHashReply reply;

  if (!request.has_account_id()) {
    LOG(ERROR) << "GetWebAuthnSecretHashRequest must have account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  Username account_id = GetAccountId(request.account_id());
  if (account_id->empty()) {
    LOG(ERROR) << "GetWebAuthnSecretHashRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  const UserSession* const session = sessions_->Find(account_id);
  brillo::SecureBlob secret_hash;
  if (session) {
    secret_hash = session->GetWebAuthnSecretHash();
  }
  if (secret_hash.empty()) {
    LOG(ERROR) << "Failed to get WebAuthn secret hash.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    return reply;
  }

  reply.set_webauthn_secret_hash(secret_hash.to_string());
  return reply;
}

void UserDataAuth::GetRecoverableKeyStores(
    user_data_auth::GetRecoverableKeyStoresRequest request,
    OnDoneCallback<user_data_auth::GetRecoverableKeyStoresReply> on_done) {
  AssertOnMountThread();
  user_data_auth::GetRecoverableKeyStoresReply reply;

  // Check whether user exists.
  // Compute the raw and sanitized user name from the request.
  Username username = GetAccountId(request.account_id());
  ObfuscatedUsername obfuscated_username = SanitizeUserName(username);
  UserSession* user_session = sessions_->Find(username);  // May be null!
  bool is_persistent_user =
      (user_session && !user_session->IsEphemeral()) ||
      platform_->DirectoryExists(UserPath(obfuscated_username));
  bool is_ephemeral_user = user_session && user_session->IsEphemeral();
  if (!is_persistent_user && !is_ephemeral_user) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthUserNonexistentInGetRecoverableKeyStores),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  // Ephemeral users don't have AuthBlockStates, so they'll never have
  // recoverable key stores generated.
  if (!is_persistent_user) {
    ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    return;
  }

  // Load the AuthFactorMap.
  AuthFactorMap& auth_factor_map =
      auth_factor_manager_->GetAuthFactorMap(obfuscated_username);

  // Populate the response from the items in the AuthFactorMap.
  for (AuthFactorMap::ValueView item : auth_factor_map) {
    const AuthBlockState& state = item.auth_factor().auth_block_state();
    if (!state.recoverable_key_store_state.has_value()) {
      continue;
    }
    RecoverableKeyStore key_store;
    if (!key_store.ParseFromString(brillo::BlobToString(
            state.recoverable_key_store_state->key_store_proto))) {
      LOG(WARNING) << "Failed to parse recoverable key store proto from auth "
                      "block state.";
      continue;
    }
    *reply.add_key_stores() = std::move(key_store);
  }
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

const brillo::SecureBlob& UserDataAuth::GetSystemSalt() {
  AssertOnOriginThread();
  CHECK_NE(system_salt_.size(), 0)
      << "Cannot call GetSystemSalt before initialization";
  return system_salt_;
}

bool UserDataAuth::UpdateCurrentUserActivityTimestamp(int time_shift_sec) {
  AssertOnMountThread();
  // We are touching the sessions object, so we'll need to be on mount thread.

  bool success = true;
  for (const auto& [username, session] : *sessions_) {
    const ObfuscatedUsername obfuscated_username = SanitizeUserName(username);
    // Inactive session is not current and ephemerals should not have ts since
    // they do not affect disk space use and do not participate in disk
    // cleaning.
    if (!session.IsActive() || session.IsEphemeral()) {
      continue;
    }
    success &= user_activity_timestamp_manager_->UpdateTimestamp(
        obfuscated_username, base::Seconds(time_shift_sec));
  }

  return success;
}

bool UserDataAuth::GetRsuDeviceId(std::string* rsu_device_id) {
  AssertOnOriginThread();

  hwsec::StatusOr<brillo::Blob> rsu = hwsec_->GetRsuDeviceId();
  if (!rsu.ok()) {
    LOG(INFO) << "Failed to get RSU device ID: " << rsu.status();
    return false;
  }

  *rsu_device_id = brillo::BlobToString(rsu.value());
  return true;
}

bool UserDataAuth::RequiresPowerwash() {
  AssertOnOriginThread();
  const bool is_powerwash_required = !crypto_->CanUnsealWithUserAuth();
  return is_powerwash_required;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::LockToSingleUserMountUntilReboot(
    const AccountIdentifier& account_id) {
  AssertOnOriginThread();
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(GetAccountId(account_id));

  homedirs_->SetLockedToSingleUser();
  brillo::Blob pcr_value;

  hwsec::StatusOr<bool> is_current_user_set = hwsec_->IsCurrentUserSet();
  if (!is_current_user_set.ok()) {
    LOG(ERROR) << "Failed to get current user status for "
                  "LockToSingleUserMountUntilReboot(): "
               << is_current_user_set.status();
    return user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR;
  }

  if (is_current_user_set.value()) {
    return user_data_auth::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED;
  }

  if (hwsec::Status status = hwsec_->SetCurrentUser(*obfuscated_username);
      !status.ok()) {
    LOG(ERROR)
        << "Failed to set current user for LockToSingleUserMountUntilReboot(): "
        << status;
    return user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::GetPinWeaverInfoReply UserDataAuth::GetPinWeaverInfo() {
  user_data_auth::GetPinWeaverInfoReply reply;

  hwsec::StatusOr<bool> enabled = hwsec_pw_manager_->IsEnabled();
  if (!enabled.ok()) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthGetPinWeaverInfoIsEnabledFailed),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE),
        &reply);
    return reply;
  }
  if (!*enabled) {
    PopulateReplyWithError(OkStatus<CryptohomeError>(), &reply);
    reply.set_has_credential(false);
    return reply;
  }

  hwsec::StatusOr<bool> has_cred = hwsec_pw_manager_->HasAnyCredential();
  if (!has_cred.ok()) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthGetPinWeaverInfoCheckFailed),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE),
        &reply);
    return reply;
  }
  reply.set_has_credential(*has_cred);

  PopulateReplyWithError(OkStatus<CryptohomeError>(), &reply);
  return reply;
}

bool UserDataAuth::OwnerUserExists() {
  AssertOnOriginThread();
  ObfuscatedUsername owner;
  return homedirs_->GetOwner(&owner);
}

bool UserDataAuth::IsArcQuotaSupported() {
  AssertOnOriginThread();
  // Quota is not supported if there are one or more unmounted Android users.
  // (b/181159107)
  return homedirs_->GetUnmountedAndroidDataCount() == 0;
}

void UserDataAuth::StartAuthSession(
    user_data_auth::StartAuthSessionRequest request,
    OnDoneCallback<user_data_auth::StartAuthSessionReply> on_done) {
  AssertOnMountThread();
  user_data_auth::StartAuthSessionReply reply;

  // Determine if the request is for an ephemeral user.
  bool is_ephemeral_user = request.is_ephemeral_user();

  std::optional<AuthIntent> auth_intent = AuthIntentFromProto(request.intent());
  if (!auth_intent.has_value()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoIntentInStartAuthSession),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL));
    return;
  }

  base::UnguessableToken token = auth_session_manager_->CreateAuthSession(
      GetAccountId(request.account_id()),
      {.is_ephemeral_user = is_ephemeral_user, .intent = *auth_intent});

  // Now that the session exists, queue up the work to run on it.
  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInStartAuthSession),
      token, std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::StartAuthSessionWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::StartAuthSessionWithSession(
    user_data_auth::StartAuthSessionRequest request,
    OnDoneCallback<user_data_auth::StartAuthSessionReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::StartAuthSessionReply reply;
  reply.set_auth_session_id(auth_session->serialized_token());
  reply.set_broadcast_id(auth_session->serialized_public_token());
  reply.set_user_exists(auth_session->user_exists());

  const AuthFactorMap& auth_factor_map = auth_factor_manager_->GetAuthFactorMap(
      auth_session->obfuscated_username());
  if (auth_factor_map.empty() &&
      (auth_session->user_exists() && !auth_session->ephemeral_user())) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNotConfiguredInStartAuthSession),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kDeleteVault,
                            PossibleAction::kAuth}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_UNUSABLE_VAULT));
    return;
  }

  // Discover any available auth factors from the AuthSession.
  absl::flat_hash_set<std::string> listed_auth_factor_labels;
  for (AuthFactorMap::ValueView stored_auth_factor : auth_factor_map) {
    const AuthFactor& auth_factor = stored_auth_factor.auth_factor();
    AuthFactorDriver& factor_driver =
        auth_factor_driver_manager_->GetDriver(auth_factor.type());

    std::optional<user_data_auth::AuthFactor> proto_factor =
        factor_driver.ConvertToProto(auth_factor.label(),
                                     auth_factor.metadata());
    if (proto_factor.has_value()) {
      // Only output one factor per label.
      auto [unused, was_inserted] =
          listed_auth_factor_labels.insert(auth_factor.label());
      if (!was_inserted) {
        continue;
      }

      // Only populate reply with AuthFactors that support the intended form of
      // authentication.
      // AuthFactorWithStatus is populated irresptive of what is available or
      // not.
      auto user_policy_file_status =
          LoadUserPolicyFile(auth_session->obfuscated_username());
      if (!user_policy_file_status.ok()) {
        ReplyWithError(
            std::move(on_done), reply,
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocCouldntLoadUserPolicyFileInStartAuthSession),
                ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                                PossibleAction::kReboot})));
        return;
      }
      auto user_policy = (*user_policy_file_status)->GetUserPolicy();
      if (!user_policy.has_value()) {
        ReplyWithError(
            std::move(on_done), reply,
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(kLocCouldntGetUserPolicyInStartAuthSession),
                ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                                PossibleAction::kReboot})));
        return;
      }
      auto supported_intents = GetSupportedIntents(
          auth_session->obfuscated_username(), auth_factor.type(),
          *auth_factor_driver_manager_,
          GetAuthFactorPolicyFromUserPolicy(user_policy, auth_factor.type()),
          /*only_light_auth=*/false);
      std::optional<AuthIntent> requested_intent =
          AuthIntentFromProto(request.intent());
      user_data_auth::AuthFactorWithStatus auth_factor_with_status;
      auth_factor_with_status.mutable_auth_factor()->CopyFrom(
          proto_factor.value());

      for (const auto& auth_intent : supported_intents) {
        auth_factor_with_status.add_available_for_intents(
            AuthIntentToProto(auth_intent));
        if (requested_intent && auth_intent == requested_intent) {
          *reply.add_auth_factors() = std::move(*proto_factor);
        }
      }
      user_data_auth::StatusInfo& status_info =
          *auth_factor_with_status.mutable_status_info();
      auto delay = factor_driver.GetFactorDelay(
          auth_session->obfuscated_username(), auth_factor);
      if (delay.ok()) {
        status_info.set_time_available_in(
            delay->is_max() ? std::numeric_limits<uint64_t>::max()
                            : delay->InMilliseconds());
      }
      auto expiration_delay = factor_driver.GetTimeUntilExpiration(
          auth_session->obfuscated_username(), auth_factor);
      if (expiration_delay.ok()) {
        status_info.set_time_expiring_in(expiration_delay->InMilliseconds());
      } else {
        // Error getting the expiration time. Treat it as won't expire.
        status_info.set_time_expiring_in(std::numeric_limits<uint64_t>::max());
      }
      *reply.add_configured_auth_factors_with_status() =
          std::move(auth_factor_with_status);
    }
  }

  // The associated UserSession (if there is one) may also have some factors of
  // its own, via verifiers. However, these are only available if the request is
  // for a verify-only session.
  //
  // This is done after the persistent factors are looked up because if a
  // persistent factor also has a verifier then we only want output from the
  // persistent factor data.
  if (request.intent() == user_data_auth::AUTH_INTENT_VERIFY_ONLY) {
    if (UserSession* user_session =
            sessions_->Find(GetAccountId(request.account_id()))) {
      for (const CredentialVerifier* verifier :
           user_session->GetCredentialVerifiers()) {
        const AuthFactorDriver& factor_driver =
            auth_factor_driver_manager_->GetDriver(
                verifier->auth_factor_type());
        if (auto proto_factor = factor_driver.ConvertToProto(
                verifier->auth_factor_label(),
                verifier->auth_factor_metadata())) {
          auto [unused, was_inserted] =
              listed_auth_factor_labels.insert(verifier->auth_factor_label());
          if (was_inserted) {
            user_data_auth::AuthFactorWithStatus auth_factor_with_status;
            auth_factor_with_status.mutable_auth_factor()->CopyFrom(
                *proto_factor);
            auth_factor_with_status.add_available_for_intents(
                AuthIntentToProto(AuthIntent::kVerifyOnly));
            *reply.add_auth_factors() = std::move(*proto_factor);
            *reply.add_configured_auth_factors_with_status() =
                std::move(auth_factor_with_status);
          }
        }
      }
    }
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::InvalidateAuthSession(
    user_data_auth::InvalidateAuthSessionRequest request,
    OnDoneCallback<user_data_auth::InvalidateAuthSessionReply> on_done) {
  AssertOnMountThread();

  user_data_auth::InvalidateAuthSessionReply reply;
  if (auth_session_manager_->RemoveAuthSession(request.auth_session_id())) {
    LOG(INFO) << "AuthSession: invalidated.";
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::ExtendAuthSession(
    user_data_auth::ExtendAuthSessionRequest request,
    OnDoneCallback<user_data_auth::ExtendAuthSessionReply> on_done) {
  AssertOnMountThread();

  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInExtendAuthSession),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthInExtendAuthSession),
      std::move(request), std::move(on_done),
      base::BindOnce(
          [](user_data_auth::ExtendAuthSessionRequest request,
             OnDoneCallback<user_data_auth::ExtendAuthSessionReply> on_done,
             InUseAuthSession auth_session) {
            user_data_auth::ExtendAuthSessionReply reply;

            // Extend specified AuthSession.
            auto timer_extension =
                request.extension_duration() != 0
                    ? base::Seconds(request.extension_duration())
                    : kDefaultExtensionTime;
            CryptohomeStatus result =
                auth_session.ExtendTimeout(timer_extension);
            if (!result.ok()) {
              result = MakeStatus<CryptohomeError>(
                           CRYPTOHOME_ERR_LOC(
                               kLocUserDataAuthExtendFailedInExtendAuthSession))
                           .Wrap(std::move(result));
            }
            LOG(INFO) << "AuthSession: Extended by " << timer_extension;
            reply.set_seconds_left(auth_session.GetRemainingTime().InSeconds());
            ReplyWithError(std::move(on_done), reply, std::move(result));
          }));
}

CryptohomeStatusOr<UserSession*> UserDataAuth::GetMountableUserSession(
    AuthSession* auth_session) {
  AssertOnMountThread();

  const ObfuscatedUsername& obfuscated_username =
      auth_session->obfuscated_username();

  // Check no guest is mounted.
  UserSession* const guest_session = sessions_->Find(guest_user_);
  if (guest_session && guest_session->IsActive()) {
    LOG(ERROR) << "Can not mount non-anonymous while guest session is active.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthGuestAlreadyMountedInGetMountableUS),
        ErrorActionSet({PossibleAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  }

  // Check the user is not already mounted.
  UserSession* const session = GetOrCreateUserSession(auth_session->username());
  if (session->IsActive()) {
    LOG(ERROR) << "User is already mounted: " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthSessionAlreadyMountedInGetMountableUS),
        ErrorActionSet({PossibleAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  }

  return session;
}

void UserDataAuth::PreMountHook(const ObfuscatedUsername& obfuscated_username) {
  AssertOnMountThread();

  LOG(INFO) << "Started mounting for: " << obfuscated_username;

  // Any non-guest mount attempt triggers InstallAttributes finalization.
  // The return value is ignored as it is possible we're pre-ownership.
  // The next login will assure finalization if possible.
  if (device_management_client_->IsInstallAttributesFirstInstall()) {
    std::ignore = device_management_client_->InstallAttributesFinalize();
  }
  // Removes all ephemeral cryptohomes owned by anyone other than the owner
  // user (if set) and non ephemeral users, regardless of free disk space.
  // Note that a fresh policy value is read here, which in theory can conflict
  // with the one used for calculation of |mount_args.is_ephemeral|. However,
  // this inconsistency (whose probability is anyway pretty low in practice)
  // should only lead to insignificant transient glitches, like an attempt to
  // mount a non existing anymore cryptohome.
  homedirs_->RemoveCryptohomesBasedOnPolicy();
}

void UserDataAuth::PostMountHook(UserSession* user_session,
                                 const MountStatus& status) {
  AssertOnMountThread();

  if (!status.ok()) {
    LOG(ERROR) << "Finished mounting with status code: " << status;
    return;
  }
  LOG(INFO) << "Mount succeeded.";
  InitializePkcs11(user_session);
}

CryptohomeStatus UserDataAuth::TerminateAuthSessionsAndClearLoadedState() {
  auth_session_manager_->RemoveAllAuthSessions();
  auth_factor_manager_->DiscardAllAuthFactorMaps();
  RETURN_IF_ERROR(uss_manager_->DiscardAllEncrypted());
  return OkStatus<CryptohomeError>();
}

libstorage::StorageContainerType
UserDataAuth::DbusEncryptionTypeToContainerType(
    user_data_auth::VaultEncryptionType type) {
  switch (type) {
    case user_data_auth::VaultEncryptionType::CRYPTOHOME_VAULT_ENCRYPTION_ANY:
      return libstorage::StorageContainerType::kUnknown;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS:
      return libstorage::StorageContainerType::kEcryptfs;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_FSCRYPT:
      return libstorage::StorageContainerType::kFscrypt;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_DMCRYPT:
      return libstorage::StorageContainerType::kDmcrypt;
    default:
      // Default cuz proto3 enum sentinels, that's why -_-
      return libstorage::StorageContainerType::kUnknown;
  }
}

void UserDataAuth::PrepareGuestVault(
    user_data_auth::PrepareGuestVaultRequest request,
    OnDoneCallback<user_data_auth::PrepareGuestVaultReply> on_done) {
  AssertOnMountThread();
  LOG(INFO) << "Preparing guest vault";

  // Send a mount starting signal.
  user_data_auth::MountStarted start_signal;
  start_signal.set_operation_id(base::RandUint64());
  signalling_intf_->SendMountStarted(start_signal);
  auto on_done_with_signal = base::BindOnce(
      &SignalMountCompletedThenDone<user_data_auth::PrepareGuestVaultReply>,
      signalling_intf_, std::move(start_signal), std::move(on_done));

  CryptohomeStatus status = PrepareGuestVaultImpl();

  // Send the mount completed signal and then the RPC reply.
  user_data_auth::PrepareGuestVaultReply reply;
  reply.set_sanitized_username(*SanitizeUserName(guest_user_));
  ReplyWithError(std::move(on_done_with_signal), reply, status);
  return;
}

void UserDataAuth::PrepareEphemeralVault(
    user_data_auth::PrepareEphemeralVaultRequest request,
    OnDoneCallback<user_data_auth::PrepareEphemeralVaultReply> on_done) {
  AssertOnMountThread();
  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoAuthSessionInPrepareEphemeralVault),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::PrepareEphemeralVaultWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::PrepareEphemeralVaultWithSession(
    user_data_auth::PrepareEphemeralVaultRequest request,
    OnDoneCallback<user_data_auth::PrepareEphemeralVaultReply> on_done,
    InUseAuthSession auth_session) {
  AssertOnMountThread();
  LOG(INFO) << "Preparing ephemeral vault";

  // Send a mount starting signal and wrap the on_done callback to send the
  // completion signal.
  user_data_auth::MountStarted start_signal;
  start_signal.set_operation_id(base::RandUint64());
  signalling_intf_->SendMountStarted(start_signal);
  auto on_done_with_signal = base::BindOnce(
      &SignalMountCompletedThenDone<user_data_auth::PrepareEphemeralVaultReply>,
      signalling_intf_, std::move(start_signal), std::move(on_done));

  user_data_auth::PrepareEphemeralVaultReply reply;

  // If there are no active sessions, attempt to account for cryptohome restarts
  // after crashing.
  if (sessions_->size() != 0 || CleanUpStaleMounts(false)) {
    LOG(ERROR) << "Can not mount ephemeral while other sessions are active.";
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthOtherSessionActiveInPrepareEphemeralVault),
            ErrorActionSet({PossibleAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
    return;
  }

  if (!auth_session->ephemeral_user()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNonEphemeralAuthSessionInPrepareEphemeralVault),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot,
                            PossibleAction::kPowerwash}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  CryptohomeStatusOr<UserSession*> session_status =
      GetMountableUserSession(auth_session.Get());
  if (!session_status.ok()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthGetSessionFailedInPrepareEphemeralVault))
            .Wrap(std::move(session_status).err_status()));
    return;
  }

  PreMountHook(auth_session->obfuscated_username());
  ReportTimerStart(kMountExTimer);
  MountStatus mount_status =
      session_status.value()->MountEphemeral(auth_session->username());
  ReportTimerStop(kMountExTimer);
  PostMountHook(session_status.value(), mount_status);
  if (!mount_status.ok()) {
    RemoveInactiveUserSession(auth_session->username());
    ReplyWithError(std::move(on_done_with_signal), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthMountFailedInPrepareEphemeralVault))
                       .Wrap(std::move(mount_status).err_status()));
    return;
  }

  // Let the auth session perform any finalization operations for a newly
  // created user.
  CryptohomeStatus ret = auth_session->OnUserCreated();
  if (!ret.ok()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthFinalizeFailedInPrepareEphemeralVault))
            .Wrap(std::move(ret)));
    return;
  }

  PopulateAuthSessionProperties(auth_session, reply.mutable_auth_properties());
  reply.set_sanitized_username(*auth_session->obfuscated_username());
  ReplyWithError(std::move(on_done_with_signal), reply,
                 OkStatus<CryptohomeError>());
}

void UserDataAuth::PreparePersistentVault(
    user_data_auth::PreparePersistentVaultRequest request,
    OnDoneCallback<user_data_auth::PreparePersistentVaultReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotFoundInPreparePersistentVault),
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotAuthInPreparePersistentVault),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::PreparePersistentVaultWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::PreparePersistentVaultWithSession(
    user_data_auth::PreparePersistentVaultRequest request,
    OnDoneCallback<user_data_auth::PreparePersistentVaultReply> on_done,
    InUseAuthSession auth_session) {
  LOG(INFO) << "Preparing persistent vault";

  // Send a mount starting signal.
  user_data_auth::MountStarted start_signal;
  start_signal.set_operation_id(base::RandUint64());
  signalling_intf_->SendMountStarted(start_signal);
  auto on_done_with_signal = base::BindOnce(
      &SignalMountCompletedThenDone<
          user_data_auth::PreparePersistentVaultReply>,
      signalling_intf_, std::move(start_signal), std::move(on_done));

  CryptohomeVault::Options options = {
      .force_type =
          DbusEncryptionTypeToContainerType(request.encryption_type()),
      .block_ecryptfs = request.block_ecryptfs(),
  };
  CryptohomeStatus status = PreparePersistentVaultImpl(auth_session, options);

  if (status.ok() && !auth_session->obfuscated_username()->empty()) {
    // Send UMA with VK stats once per successful mount operation.
    keyset_management_->RecordAllVaultKeysetMetrics(
        auth_session->obfuscated_username());
  }

  // Send the mount completed signal and then the RPC reply.
  user_data_auth::PreparePersistentVaultReply reply;
  reply.set_sanitized_username(*auth_session->obfuscated_username());
  ReplyWithError(std::move(on_done_with_signal), reply, status);
}

void UserDataAuth::PrepareVaultForMigration(
    user_data_auth::PrepareVaultForMigrationRequest request,
    OnDoneCallback<user_data_auth::PrepareVaultForMigrationReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotFoundInPrepareVaultForMigration),
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotAuthInPrepareVaultForMigration),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::PrepareVaultForMigrationWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::PrepareVaultForMigrationWithSession(
    user_data_auth::PrepareVaultForMigrationRequest request,
    OnDoneCallback<user_data_auth::PrepareVaultForMigrationReply> on_done,
    InUseAuthSession auth_session) {
  AssertOnMountThread();
  LOG(INFO) << "Preparing vault for migration";

  // Send a mount starting signal.
  user_data_auth::MountStarted start_signal;
  start_signal.set_operation_id(base::RandUint64());
  signalling_intf_->SendMountStarted(start_signal);
  auto on_done_with_signal = base::BindOnce(
      &SignalMountCompletedThenDone<
          user_data_auth::PrepareVaultForMigrationReply>,
      signalling_intf_, std::move(start_signal), std::move(on_done));

  CryptohomeVault::Options options = {
      .migrate = true,
  };
  CryptohomeStatus status = PreparePersistentVaultImpl(auth_session, options);

  // Send the mount completed signal and then the RPC reply.
  user_data_auth::PrepareVaultForMigrationReply reply;
  reply.set_sanitized_username(*auth_session->obfuscated_username());
  ReplyWithError(std::move(on_done_with_signal), reply, status);
}

void UserDataAuth::CreatePersistentUser(
    user_data_auth::CreatePersistentUserRequest request,
    OnDoneCallback<user_data_auth::CreatePersistentUserReply> on_done) {
  AssertOnMountThread();
  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInCreatePersistentUser),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::CreatePersistentUserWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::CreatePersistentUserWithSession(
    user_data_auth::CreatePersistentUserRequest request,
    OnDoneCallback<user_data_auth::CreatePersistentUserReply> on_done,
    InUseAuthSession auth_session) {
  LOG(INFO) << "Creating persistent user";
  // Record the time in between now and when this function exits.
  absl::Cleanup report_time = [start_time = base::TimeTicks::Now()]() {
    ReportTimerDuration(kCreatePersistentUserTimer, start_time, "");
  };

  // Send the auth started signal and wrap the completion callback in a sender
  // for the completion signal.
  uint64_t operation_id = base::RandUint64();
  user_data_auth::AuthenticateStarted start_signal;
  start_signal.set_operation_id(operation_id);
  start_signal.set_user_creation(true);
  start_signal.set_username(*auth_session->username());
  start_signal.set_sanitized_username(*auth_session->obfuscated_username());
  signalling_intf_->SendAuthenticateStarted(start_signal);
  auto on_done_with_signal = base::BindOnce(
      &SignalAuthCompletedThenDone<user_data_auth::CreatePersistentUserReply>,
      signalling_intf_, std::move(start_signal), std::move(on_done));

  user_data_auth::CreatePersistentUserReply reply;
  if (auth_session->ephemeral_user()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthCreatePersistentUserInEphemeralSession),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot,
                            PossibleAction::kPowerwash}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  const ObfuscatedUsername& obfuscated_username =
      auth_session->obfuscated_username();

  // This checks presence of the actual encrypted vault. We fail if Create is
  // called while actual persistent vault is present.
  auto exists_or = homedirs_->CryptohomeExists(obfuscated_username);
  if (exists_or.ok() && exists_or.value()) {
    LOG(ERROR) << "User already exists: " << obfuscated_username;
    ReplyWithError(std::move(on_done_with_signal), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthUserExistsInCreatePersistentUser),
                       ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                                       PossibleAction::kDeleteVault}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
    return;
  }

  if (!exists_or.ok()) {
    MountError mount_error = exists_or.err_status()->error();
    LOG(ERROR) << "Failed to query vault existance for: " << obfuscated_username
               << ", code: " << mount_error;
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeMountError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthCheckExistsFailedInCreatePersistentUser),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot}),
            mount_error, MountErrorToCryptohomeError(mount_error)));
    return;
  }

  // This check seems superfluous after the `HomeDirs::CryptohomeExists()` check
  // above, but it can happen that the user directory exists without any vault
  // in it. We perform both checks for completeness and also to distinguish
  // between these two error cases in metrics and logs.
  if (auth_session->user_exists()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthUserDirExistsInCreatePersistentUser),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kDeleteVault,
                            PossibleAction::kPowerwash}),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
    return;
  }

  // This checks and creates if missing the user's directory in shadow root.
  // We need to disambiguate with vault presence, because it is possible that
  // we have an empty shadow root directory for the user left behind after
  // removing a profile (due to a bug or for some other reasons). To avoid weird
  // failures in the case, just let the creation succeed, since the user is
  // effectively not there. Eventually |Exists| will check for the presence of
  // the USS/auth factors to determine if the user is intended to be there.
  // This call will not create the actual volume (for efficiency, idempotency,
  // and because that would require going the full sequence of mount and unmount
  // because of ecryptfs possibility).
  if (!homedirs_->Exists(obfuscated_username) &&
      !homedirs_->Create(obfuscated_username)) {
    LOG(ERROR) << "Failed to create shadow directory for: "
               << obfuscated_username;
    ReplyWithError(std::move(on_done_with_signal), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthCreateFailedInCreatePersistentUser),
                       ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                                       PossibleAction::kReboot,
                                       PossibleAction::kPowerwash}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  // Let the auth session perform any finalization operations for a newly
  // created user.
  CryptohomeStatus ret = auth_session->OnUserCreated();
  if (!ret.ok()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthFinalizeFailedInCreatePersistentUser))
            .Wrap(std::move(ret)));
    return;
  }

  PopulateAuthSessionProperties(auth_session, reply.mutable_auth_properties());
  reply.set_sanitized_username(*auth_session->obfuscated_username());
  ReplyWithError(std::move(on_done_with_signal), reply,
                 OkStatus<CryptohomeError>());
}

CryptohomeStatus UserDataAuth::PrepareGuestVaultImpl() {
  AssertOnMountThread();

  // If there are no active sessions, attempt to account for cryptohome restarts
  // after crashing.
  if (sessions_->size() != 0 || CleanUpStaleMounts(false)) {
    LOG(ERROR) << "Can not mount guest while other sessions are active.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthOtherSessionActiveInPrepareGuestVault),
        ErrorActionSet({PossibleAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  UserSession* const session = GetOrCreateUserSession(guest_user_);

  LOG(INFO) << "Started mounting for guest";
  ReportTimerStart(kMountGuestExTimer);
  MountStatus status = session->MountGuest();
  ReportTimerStop(kMountGuestExTimer);
  if (!status.ok()) {
    CHECK(status->mount_error() != MOUNT_ERROR_NONE);
    LOG(ERROR) << "Finished mounting with status code: "
               << status->mount_error();
    RemoveInactiveUserSession(guest_user_);
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthMountFailedInPrepareGuestVault))
        .Wrap(std::move(status));
  }
  LOG(INFO) << "Mount succeeded.";
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus UserDataAuth::PreparePersistentVaultImpl(
    InUseAuthSession& auth_session,
    const CryptohomeVault::Options& vault_options) {
  AssertOnMountThread();

  // If there are no active sessions, attempt to account for cryptohome restarts
  // after crashing.
  if (sessions_->empty()) {
    CleanUpStaleMounts(false);
  }

  if (auth_session->ephemeral_user()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthEphemeralAuthSessionAttemptPreparePersistentVault),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kDeleteVault, PossibleAction::kReboot,
                        PossibleAction::kPowerwash}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  const ObfuscatedUsername& obfuscated_username =
      auth_session->obfuscated_username();
  if (!homedirs_->Exists(obfuscated_username)) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthNonExistentInPreparePersistentVault),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kDeleteVault, PossibleAction::kReboot,
                        PossibleAction::kPowerwash}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  }

  CryptohomeStatusOr<UserSession*> session_status =
      GetMountableUserSession(auth_session.Get());
  if (!session_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthGetSessionFailedInPreparePersistentVault))
        .Wrap(std::move(session_status).err_status());
  }

  // User session and kiosk session cannot co-exist.
  bool are_active_sessions = false;
  for (const auto& [username, session] : *sessions_) {
    if (session.IsActive()) {
      are_active_sessions = true;
      // Don't mount user cryptohome if there is a mounted kiosk session.
      if (IsKioskUser(SanitizeUserName(username))) {
        return MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthUnexpectedKioskMountInPreparePersistent),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      }
    }
  }
  // Don't mount if the current request is for a kiosk session, there
  // are other active mounts.
  if (are_active_sessions && IsKioskUser(auth_session->obfuscated_username())) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthExitingMountsInPreparePersistent),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  PreMountHook(obfuscated_username);
  if (low_disk_space_handler_) {
    low_disk_space_handler_->disk_cleanup()->FreeDiskSpaceDuringLogin(
        obfuscated_username);
  }
  ReportTimerStart(kMountExTimer);
  MountStatus mount_status = session_status.value()->MountVault(
      auth_session->username(), auth_session->file_system_keyset(),
      vault_options);
  ReportTimerStop(kMountExTimer);
  PostMountHook(session_status.value(), mount_status);
  if (!mount_status.ok()) {
    RemoveInactiveUserSession(auth_session->username());
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthMountFailedInPreparePersistentVault))
        .Wrap(std::move(mount_status).err_status());
  }
  return OkStatus<CryptohomeError>();
}

bool UserDataAuth::IsKioskUser(ObfuscatedUsername obfuscated) {
  // Load the AuthFactorMap for the obfuscated username.
  AuthFactorMap& auth_factor_map =
      auth_factor_manager_->GetAuthFactorMap(obfuscated);
  // Populate the response from the items in the AuthFactorMap.
  for (AuthFactorMap::ValueView item : auth_factor_map) {
    if (item.auth_factor().type() == AuthFactorType::kKiosk) {
      return true;
    }
  }
  return false;
}

void UserDataAuth::AddAuthFactor(
    user_data_auth::AddAuthFactorRequest request,
    OnDoneCallback<user_data_auth::AddAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthAuthSessionNotFoundInAddAuthFactor),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthAuthSessionNotAuthInAddAuthFactor),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::AddAuthFactorWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::AddAuthFactorWithSession(
    user_data_auth::AddAuthFactorRequest request,
    OnDoneCallback<user_data_auth::AddAuthFactorReply> on_done,
    InUseAuthSession auth_session) {
  // Wrap callback to signal AuthFactorAdded.
  OnDoneCallback<user_data_auth::AddAuthFactorReply>
      on_done_wrapped_with_signal_ = base::BindOnce(
          [](SignallingInterface* signalling_intf, std::string broadcast_id,
             OnDoneCallback<user_data_auth::AddAuthFactorReply> cb,
             const user_data_auth::AddAuthFactorReply& reply) {
            user_data_auth::AuthFactorAdded completed_proto;
            if (!reply.has_error_info()) {
              completed_proto.mutable_auth_factor()->CopyFrom(
                  reply.added_auth_factor().auth_factor());
              completed_proto.set_broadcast_id(broadcast_id);
              signalling_intf->SendAuthFactorAdded(completed_proto);
            }
            std::move(cb).Run(reply);
          },
          signalling_intf_, auth_session->serialized_public_token(),
          std::move(on_done));

  user_data_auth::AddAuthFactorReply reply;

  // Populate the request auth factor with accurate sysinfo.
  PopulateAuthFactorProtoWithSysinfo(*request.mutable_auth_factor());
  auto user_policy_file_status =
      LoadUserPolicyFile(auth_session->obfuscated_username());
  if (!user_policy_file_status.ok()) {
    ReplyWithError(
        std::move(on_done_wrapped_with_signal_), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCouldntLoadUserPolicyFileInAddAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot})));
    return;
  }
  auto* session_decrypt = auth_session->GetAuthForDecrypt();
  if (!session_decrypt) {
    ReplyWithError(
        std::move(on_done_wrapped_with_signal_), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUnauthedInAddAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }
  const Username& username = auth_session->username();
  session_decrypt->AddAuthFactor(
      request,
      base::BindOnce(
          &ReplyWithAuthFactorStatus<user_data_auth::AddAuthFactorReply>,
          std::move(auth_session).BindForCallback(),
          user_policy_file_status.value(), auth_factor_manager_,
          auth_factor_driver_manager_, sessions_->Find(username),
          request.auth_factor().label(),
          std::move(on_done_wrapped_with_signal_)));
}

void UserDataAuth::AuthenticateAuthFactor(
    user_data_auth::AuthenticateAuthFactorRequest request,
    OnDoneCallback<user_data_auth::AuthenticateAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInAuthAuthFactor),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::AuthenticateAuthFactorWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::AuthenticateAuthFactorWithSession(
    user_data_auth::AuthenticateAuthFactorRequest request,
    OnDoneCallback<user_data_auth::AuthenticateAuthFactorReply> on_done,
    InUseAuthSession auth_session) {
  // We will tie the life time of the authenticate event with the wrapped
  // on_done callback.
  hwsec::ScopedEvent event;
  if (hwsec_) {
    event = hwsec_->NotifyAuthenticateEvent().value_or(hwsec::ScopedEvent());
  }

  // Extract the auth factor type.
  std::optional<AuthFactorType> auth_factor_type =
      DetermineFactorTypeFromAuthInput(request.auth_input());
  user_data_auth::AuthFactorType auth_factor_type_proto = AuthFactorTypeToProto(
      auth_factor_type.value_or(AuthFactorType::kUnspecified));

  // Send the auth started signal and wrap the completion callback in a sender
  // for the completion signal.
  uint64_t operation_id = base::RandUint64();
  user_data_auth::AuthenticateStarted start_signal;
  start_signal.set_operation_id(operation_id);
  start_signal.set_auth_factor_type(auth_factor_type_proto);
  start_signal.set_username(*auth_session->username());
  start_signal.set_sanitized_username(*auth_session->obfuscated_username());
  signalling_intf_->SendAuthenticateStarted(start_signal);
  auto on_done_with_signal = base::BindOnce(
      &SignalAuthCompletedThenDone<user_data_auth::AuthenticateAuthFactorReply>,
      signalling_intf_, std::move(start_signal), std::move(on_done));

  user_data_auth::AuthenticateAuthFactorReply reply;
  std::vector<std::string> auth_factor_labels;
  auth_factor_labels.reserve(request.auth_factor_labels_size());
  for (const auto& label : request.auth_factor_labels()) {
    auth_factor_labels.push_back(label);
  }

  auto user_policy_file_status =
      LoadUserPolicyFile(auth_session->obfuscated_username());
  if (!user_policy_file_status.ok()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocCouldntLoadUserPolicyFileInAuthenticateAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot})));
    return;
  }

  SerializedUserAuthFactorTypePolicy auth_factor_type_policy;
  if (!auth_factor_type.has_value()) {
    ReplyWithError(
        std::move(on_done_with_signal), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthAuthFactorNotFoundInAuthenticateAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  } else {
    auth_factor_type_policy = GetAuthFactorPolicyFromUserPolicy(
        (*user_policy_file_status)->GetUserPolicy(), *auth_factor_type);
  }

  AuthSession::AuthenticateAuthFactorRequest authenticate_auth_factor_request{
      .auth_factor_labels = std::move(auth_factor_labels),
      .auth_input_proto = std::move(request.auth_input()),
      .flags =
          AuthSession::AuthenticateAuthFactorFlags{
              .force_full_auth = AuthSession::ForceFullAuthFlag::kNone},
  };

  AuthSession* auth_session_ptr = auth_session.Get();
  auth_session_ptr->AuthenticateAuthFactor(
      authenticate_auth_factor_request, auth_factor_type_policy,
      base::BindOnce(&HandleAuthenticationResult,
                     std::move(auth_session).BindForCallback(),
                     auth_factor_type_policy, std::move(on_done_with_signal)));
}

void UserDataAuth::UpdateAuthFactor(
    user_data_auth::UpdateAuthFactorRequest request,
    OnDoneCallback<user_data_auth::UpdateAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInUpdateAuthFactor),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthInUpdateAuthFactor),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::UpdateAuthFactorWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::UpdateAuthFactorWithSession(
    user_data_auth::UpdateAuthFactorRequest request,
    OnDoneCallback<user_data_auth::UpdateAuthFactorReply> on_done,
    InUseAuthSession auth_session) {
  // Wrap callback to signal AuthFactorUpdated.
  OnDoneCallback<user_data_auth::UpdateAuthFactorReply>
      on_done_wrapped_with_signal_ = base::BindOnce(
          [](SignallingInterface* signalling_intf, std::string broadcast_id,
             OnDoneCallback<user_data_auth::UpdateAuthFactorReply> cb,
             const user_data_auth::UpdateAuthFactorReply& reply) {
            user_data_auth::AuthFactorUpdated completed_proto;

            if (reply.has_error_info() && reply.error_info().primary_action() ==
                                              user_data_auth::PRIMARY_NONE) {
              completed_proto.mutable_auth_factor()->CopyFrom(
                  reply.updated_auth_factor().auth_factor());
              completed_proto.set_broadcast_id(broadcast_id);
              signalling_intf->SendAuthFactorUpdated(completed_proto);
            }
            std::move(cb).Run(reply);
          },
          signalling_intf_, auth_session->serialized_public_token(),
          std::move(on_done));
  user_data_auth::UpdateAuthFactorReply reply;

  // Populate the request auth factor with accurate sysinfo.
  PopulateAuthFactorProtoWithSysinfo(*request.mutable_auth_factor());

  auto user_policy_file_status =
      LoadUserPolicyFile(auth_session->obfuscated_username());
  if (!user_policy_file_status.ok()) {
    ReplyWithError(
        std::move(on_done_wrapped_with_signal_), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCouldntLoadUserPolicyFileInUpdateAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot})));
    return;
  }
  auto* session_decrypt = auth_session->GetAuthForDecrypt();
  if (!session_decrypt) {
    ReplyWithError(
        std::move(on_done_wrapped_with_signal_), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUnauthedInUpdateAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }
  const Username& username = auth_session->username();
  session_decrypt->UpdateAuthFactor(
      request,
      base::BindOnce(
          &ReplyWithAuthFactorStatus<user_data_auth::UpdateAuthFactorReply>,
          std::move(auth_session).BindForCallback(),
          user_policy_file_status.value(), auth_factor_manager_,
          auth_factor_driver_manager_, sessions_->Find(username),
          request.auth_factor().label(),
          std::move(on_done_wrapped_with_signal_)));
}

void UserDataAuth::UpdateAuthFactorMetadata(
    user_data_auth::UpdateAuthFactorMetadataRequest request,
    OnDoneCallback<user_data_auth::UpdateAuthFactorMetadataReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotFoundInUpdateAuthFactorMetadata),
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotAuthInUpdateAuthFactorMetadata),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::UpdateAuthFactorMetadataWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::UpdateAuthFactorMetadataWithSession(
    user_data_auth::UpdateAuthFactorMetadataRequest request,
    OnDoneCallback<user_data_auth::UpdateAuthFactorMetadataReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::UpdateAuthFactorMetadataReply reply;

  // Populate the request auth factor with accurate sysinfo.
  auto user_policy_file_status =
      LoadUserPolicyFile(auth_session->obfuscated_username());
  if (!user_policy_file_status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocCouldntLoadUserPolicyFileInUpdateAuthFactorMetadata),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot})));
    return;
  }
  AuthSession* auth_session_ptr = auth_session.Get();
  auto* session_decrypt = auth_session_ptr->GetAuthForDecrypt();
  if (!session_decrypt) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthUnauthedInUpdateAuthFactorMetadata),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }
  session_decrypt->UpdateAuthFactorMetadata(
      request,
      base::BindOnce(&ReplyWithAuthFactorStatus<
                         user_data_auth::UpdateAuthFactorMetadataReply>,
                     std::move(auth_session).BindForCallback(),
                     user_policy_file_status.value(), auth_factor_manager_,
                     auth_factor_driver_manager_,
                     sessions_->Find(auth_session_ptr->username()),
                     request.auth_factor().label(), std::move(on_done)));
}

void UserDataAuth::RelabelAuthFactor(
    user_data_auth::RelabelAuthFactorRequest request,
    OnDoneCallback<user_data_auth::RelabelAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInRelabelAuthFactor),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthInRelabelAuthFactor),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::RelabelAuthFactorWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::RelabelAuthFactorWithSession(
    user_data_auth::RelabelAuthFactorRequest request,
    OnDoneCallback<user_data_auth::RelabelAuthFactorReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::RelabelAuthFactorReply reply;
  auto* session_decrypt = auth_session->GetAuthForDecrypt();
  if (!session_decrypt) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUnauthedInRelabelAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  // Load the user policy, also needed for the final result.
  auto user_policy_file =
      LoadUserPolicyFile(auth_session->obfuscated_username());
  if (!user_policy_file.ok()) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocCouldntLoadUserPolicyFileInRelabelAuthFactor))
                       .Wrap(std::move(user_policy_file).err_status()));
    return;
  }

  // Execute the actual relabel.
  AuthSession* auth_session_ptr = auth_session.Get();
  session_decrypt->RelabelAuthFactor(
      request,
      base::BindOnce(
          &ReplyWithAuthFactorStatus<user_data_auth::RelabelAuthFactorReply>,
          std::move(auth_session).BindForCallback(), *user_policy_file,
          auth_factor_manager_, auth_factor_driver_manager_,
          sessions_->Find(auth_session_ptr->username()),
          request.new_auth_factor_label(), std::move(on_done)));
}

void UserDataAuth::ReplaceAuthFactor(
    user_data_auth::ReplaceAuthFactorRequest request,
    OnDoneCallback<user_data_auth::ReplaceAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInReplaceAuthFactor),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthInReplaceAuthFactor),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::ReplaceAuthFactorWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::ReplaceAuthFactorWithSession(
    user_data_auth::ReplaceAuthFactorRequest request,
    OnDoneCallback<user_data_auth::ReplaceAuthFactorReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::ReplaceAuthFactorReply reply;
  auto* session_decrypt = auth_session->GetAuthForDecrypt();
  if (!session_decrypt) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUnauthedInReplaceAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  // Load the user policy, also needed for the final result.
  auto user_policy_file =
      LoadUserPolicyFile(auth_session->obfuscated_username());
  if (!user_policy_file.ok()) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocCouldntLoadUserPolicyFileInReplaceAuthFactor))
                       .Wrap(std::move(user_policy_file).err_status()));
    return;
  }

  // Execute the actual relabel.
  AuthSession* auth_session_ptr = auth_session.Get();
  session_decrypt->ReplaceAuthFactor(
      request,
      base::BindOnce(
          &ReplyWithAuthFactorStatus<user_data_auth::ReplaceAuthFactorReply>,
          std::move(auth_session).BindForCallback(), *user_policy_file,
          auth_factor_manager_, auth_factor_driver_manager_,
          sessions_->Find(auth_session_ptr->username()),
          request.auth_factor().label(), std::move(on_done)));
}

void UserDataAuth::RemoveAuthFactor(
    user_data_auth::RemoveAuthFactorRequest request,
    OnDoneCallback<user_data_auth::RemoveAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInRemoveAuthFactor),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthInRemoveAuthFactor),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::RemoveAuthFactorWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::RemoveAuthFactorWithSession(
    user_data_auth::RemoveAuthFactorRequest request,
    OnDoneCallback<user_data_auth::RemoveAuthFactorReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::RemoveAuthFactorReply reply;
  auto* session_decrypt = auth_session->GetAuthForDecrypt();
  if (!session_decrypt) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUnauthedInRemoveAuthFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  user_data_auth::AuthFactorRemoved auth_factor_removed_msg;
  if (auto view = auth_factor_manager_
                      ->GetAuthFactorMap(auth_session->obfuscated_username())
                      .Find(request.auth_factor_label());
      view.has_value()) {
    const auto& af = view->auth_factor();
    const AuthFactorDriver& factor_driver =
        auth_factor_driver_manager_->GetDriver(af.type());

    auto af_proto = factor_driver.ConvertToProto(af.label(), af.metadata());
    if (af_proto.has_value()) {
      auth_factor_removed_msg.mutable_auth_factor()->CopyFrom(af_proto.value());
    }

    auth_factor_removed_msg.set_broadcast_id(
        auth_session->serialized_public_token());
  }

  // Wrap callback to signal AuthenticateAuthFactorCompleted.
  OnDoneCallback<user_data_auth::RemoveAuthFactorReply>
      on_done_wrapped_with_signal_cb = base::BindOnce(
          [](SignallingInterface* signalling_intf,
             user_data_auth::AuthFactorRemoved auth_factor_removed_msg,
             OnDoneCallback<user_data_auth::RemoveAuthFactorReply> cb,
             const user_data_auth::RemoveAuthFactorReply& reply) {
            if (!reply.has_error_info()) {
              signalling_intf->SendAuthFactorRemoved(auth_factor_removed_msg);
            }
            std::move(cb).Run(reply);
          },
          signalling_intf_, std::move(auth_factor_removed_msg),
          std::move(on_done));
  StatusCallback on_remove_auth_factor_finished =
      base::BindOnce(&ReplyWithStatus<user_data_auth::RemoveAuthFactorReply>,
                     std::move(auth_session).BindForCallback(),
                     std::move(on_done_wrapped_with_signal_cb));
  session_decrypt->RemoveAuthFactor(request,
                                    std::move(on_remove_auth_factor_finished));
}

void UserDataAuth::ListAuthFactors(
    user_data_auth::ListAuthFactorsRequest request,
    OnDoneCallback<user_data_auth::ListAuthFactorsReply> on_done) {
  AssertOnMountThread();
  user_data_auth::ListAuthFactorsReply reply;

  // Check whether user exists.
  // Compute the raw and sanitized user name from the request.
  Username username = GetAccountId(request.account_id());
  ObfuscatedUsername obfuscated_username = SanitizeUserName(username);
  UserSession* user_session = sessions_->Find(username);  // May be null!
  // If the user does not exist, we cannot return auth factors for it.
  bool is_persistent_user =
      (user_session && !user_session->IsEphemeral()) ||
      platform_->DirectoryExists(UserPath(obfuscated_username));
  bool is_ephemeral_user = user_session && user_session->IsEphemeral();
  if (!is_persistent_user && !is_ephemeral_user) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthUserNonexistentInListAuthFactors),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  auto user_policy_file_status = LoadUserPolicyFile(obfuscated_username);
  if (!user_policy_file_status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCouldntLoadUserPolicyFileInListAuthFactors),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kReboot})));
    return;
  }

  // Helper function to filter out types of auth factor that are supported
  // internally but which should not be reported as supported in the public API.
  auto IsPublicType = [](AuthFactorType type) {
    switch (type) {
      case AuthFactorType::kPassword:
      case AuthFactorType::kPin:
      case AuthFactorType::kCryptohomeRecovery:
      case AuthFactorType::kKiosk:
      case AuthFactorType::kSmartCard:
      case AuthFactorType::kFingerprint:
        return true;
      case AuthFactorType::kLegacyFingerprint:
      case AuthFactorType::kUnspecified:
      default:
        return false;
    }
  };

  std::vector<AuthFactorType> supported_auth_factors;
  if (is_persistent_user) {
    // Prepare the response for configured AuthFactors (with status) with all of
    // the auth factors from the disk.

    // Load the AuthFactorMap.
    AuthFactorMap& auth_factor_map =
        auth_factor_manager_->GetAuthFactorMap(obfuscated_username);

    // Populate the response from the items in the AuthFactorMap.
    for (AuthFactorMap::ValueView item : auth_factor_map) {
      if (IsPublicType(item.auth_factor().type())) {
        auto auth_factor_with_status = GetAuthFactorWithStatus(
            obfuscated_username, *user_policy_file_status,
            auth_factor_driver_manager_, item.auth_factor());
        if (auth_factor_with_status.has_value()) {
          *reply.add_configured_auth_factors_with_status() =
              std::move(*auth_factor_with_status);
        }
      }
    }

    // Prepare the response for supported AuthFactors for the given user.
    // Since user is a persistent user this is determined based on the
    // underlying storage backend and the existing configured factors.

    // Turn the list of configured types into a set that we can use for
    // computing the list of supported factors.
    absl::flat_hash_set<AuthFactorType> configured_types;
    for (const auto& configured_factor_status :
         reply.configured_auth_factors_with_status()) {
      if (auto type = AuthFactorTypeFromProto(
              configured_factor_status.auth_factor().type())) {
        configured_types.insert(*type);
      }
    }

    // Determine what auth factors are supported by going through the entire set
    // of auth factor types and checking each one.
    absl::flat_hash_set<AuthFactorStorageType> configured_storages;
    configured_storages.insert(AuthFactorStorageType::kUserSecretStash);

    if (auth_factor_map.HasFactorWithStorage(
            AuthFactorStorageType::kVaultKeyset)) {
      configured_storages.insert(AuthFactorStorageType::kVaultKeyset);
    }

    for (auto proto_type :
         PROTOBUF_ENUM_ALL_VALUES(user_data_auth::AuthFactorType)) {
      std::optional<AuthFactorType> type = AuthFactorTypeFromProto(proto_type);
      if (!type || !IsPublicType(*type)) {
        continue;
      }
      const AuthFactorDriver& factor_driver =
          auth_factor_driver_manager_->GetDriver(*type);
      if (factor_driver.IsSupportedByStorage(configured_storages,
                                             configured_types) &&
          factor_driver.IsSupportedByHardware()) {
        reply.add_supported_auth_factors(proto_type);
        supported_auth_factors.push_back(*type);
      }
    }
  } else if (is_ephemeral_user) {
    // Use the credential verifier for the session to determine what types of
    // factors are configured.
    if (user_session) {
      for (const CredentialVerifier* verifier :
           user_session->GetCredentialVerifiers()) {
        if (IsPublicType(verifier->auth_factor_type())) {
          auto auth_factor_with_status = GetAuthFactorWithStatus(
              obfuscated_username, *user_policy_file_status,
              auth_factor_driver_manager_, verifier);
          if (auth_factor_with_status.has_value()) {
            *reply.add_configured_auth_factors_with_status() =
                std::move(*auth_factor_with_status);
          }
        }
      }
    }
    // Determine what auth factors are supported by going through the entire set
    // of auth factor types and checking each one.
    for (auto proto_type :
         PROTOBUF_ENUM_ALL_VALUES(user_data_auth::AuthFactorType)) {
      std::optional<AuthFactorType> type = AuthFactorTypeFromProto(proto_type);
      if (!type || !IsPublicType(*type)) {
        continue;
      }
      const AuthFactorDriver& factor_driver =
          auth_factor_driver_manager_->GetDriver(*type);
      if (factor_driver.IsLightAuthSupported(AuthIntent::kVerifyOnly)) {
        reply.add_supported_auth_factors(proto_type);
        supported_auth_factors.push_back(*type);
      }
    }
  }

  // For every supported auth factor type the user has, report the available
  // auth intents.
  for (AuthFactorType type : supported_auth_factors) {
    const AuthFactorDriver& factor_driver =
        auth_factor_driver_manager_->GetDriver(type);
    auto type_policy = GetAuthFactorPolicyFromUserPolicy(
        (*user_policy_file_status)->GetUserPolicy(), type);
    // Proto AuthIntentsForAuthFactorType assumes nothing is enabled if the type
    // policy is empty, but here the emptiness is just an indication of no
    // change to the default policy.
    if (type_policy.enabled_intents.empty() &&
        type_policy.disabled_intents.empty()) {
      SetAuthIntentsForAuthFactorType(type, factor_driver, std::nullopt,
                                      /*is_persistent_user=*/is_persistent_user,
                                      /*is_ephemeral_user=*/is_ephemeral_user,
                                      reply.add_auth_intents_for_types());
    } else {
      SetAuthIntentsForAuthFactorType(type, factor_driver, type_policy,
                                      /*is_persistent_user=*/is_persistent_user,
                                      /*is_ephemeral_user=*/is_ephemeral_user,
                                      reply.add_auth_intents_for_types());
    }
  }

  // Sort the auth factors by label, to produce a more consistent response.
  std::sort(reply.mutable_configured_auth_factors_with_status()->begin(),
            reply.mutable_configured_auth_factors_with_status()->end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.auth_factor().label() < rhs.auth_factor().label();
            });

  // This field is technically unnecessary since it is just a subset of
  // configured_auth_factors_with_status but since both fields are in use by
  // clients it's kept for compatibility.
  for (auto configured_auth_factors_with_status :
       reply.configured_auth_factors_with_status()) {
    *reply.add_configured_auth_factors() =
        configured_auth_factors_with_status.auth_factor();
  }

  // Successfully completed, send the response with OK.
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::ModifyAuthFactorIntents(
    user_data_auth::ModifyAuthFactorIntentsRequest request,
    OnDoneCallback<user_data_auth::ModifyAuthFactorIntentsReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotFoundInModifyAuthFactorIntents),
      CRYPTOHOME_ERR_LOC(
          kLocUserDataAuthSessionNotAuthInModifyAuthFactorIntents),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::ModifyAuthFactorIntentsWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::ModifyAuthFactorIntentsWithSession(
    user_data_auth::ModifyAuthFactorIntentsRequest request,
    OnDoneCallback<user_data_auth::ModifyAuthFactorIntentsReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::ModifyAuthFactorIntentsReply reply;
  std::optional<AuthFactorType> type = AuthFactorTypeFromProto(request.type());
  if (!type.has_value()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthFactorTypeNotFoundInModifyAuthFactorIntents),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }
  auto user_policy_file_status =
      LoadUserPolicyFile(auth_session->obfuscated_username());
  if (!user_policy_file_status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocCouldntLoadUserPolicyFileInModifyAuthFactorIntents))
            .Wrap(std::move(user_policy_file_status).err_status()));
    return;
  }
  SerializedUserAuthFactorTypePolicy new_auth_factor_policy;
  absl::flat_hash_set<AuthIntent> intents_for_auth_factor;
  for (int i = 0; i < request.intents_size(); i++) {
    auto auth_intent_from_proto = AuthIntentFromProto(request.intents(i));
    if (!auth_intent_from_proto.has_value()) {
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocCouldntConvertToAuthIntentInModifyAuthFactorIntents),
              ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                              PossibleAction::kReboot}),
              user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
    }
    intents_for_auth_factor.insert(*auth_intent_from_proto);
  }
  new_auth_factor_policy.type = SerializeAuthFactorType(*type);
  const AuthFactorDriver& driver =
      auth_factor_driver_manager_->GetDriver(*type);
  bool is_ephemeral_user = auth_session->ephemeral_user();

  // Any intent that is enabled should be both supported by the hardware and be
  // configurable.
  if (driver.IsSupportedByHardware()) {
    for (auto intent : intents_for_auth_factor) {
      if (driver.GetIntentConfigurability(intent) ==
          AuthFactorDriver::IntentConfigurability::kNotConfigurable) {
        continue;
      }
      if (is_ephemeral_user) {
        if (!driver.IsLightAuthSupported(intent)) {
          continue;
        }
      } else {
        if (!driver.IsLightAuthSupported(intent) &&
            !driver.IsFullAuthSupported(intent)) {
          continue;
        }
      }
      new_auth_factor_policy.enabled_intents.push_back(
          SerializeAuthIntent(intent));
    }
    for (AuthIntent intent : kAllAuthIntents) {
      // If the policy has not enabled a configurable intent explicitly, it
      // should be listed as disabled.
      if (!intents_for_auth_factor.contains(intent) &&
          driver.GetIntentConfigurability(intent) !=
              AuthFactorDriver::IntentConfigurability::kNotConfigurable) {
        new_auth_factor_policy.disabled_intents.push_back(
            SerializeAuthIntent(intent));
      }
    }
  }
  std::optional<SerializedUserPolicy> user_policy =
      (*user_policy_file_status)->GetUserPolicy();
  SerializedUserPolicy new_policy;
  new_policy.auth_factor_type_policy.push_back(new_auth_factor_policy);
  // The new user policy should include the policy for all of the auth factors
  // except for the updated auth factor. The last policy for this auth factor
  // should be entirely discarded as the modify doesn't update the policy and
  // rather replaces it.
  if (user_policy.has_value()) {
    for (auto policy : user_policy->auth_factor_type_policy) {
      if (policy.type.has_value() &&
          *policy.type != SerializeAuthFactorType(*type)) {
        new_policy.auth_factor_type_policy.push_back(policy);
      }
    }
  }
  (*user_policy_file_status)->UpdateUserPolicy(new_policy);
  CryptohomeStatus user_policy_store_status =
      (*user_policy_file_status)->StoreInFile();
  if (!user_policy_store_status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocCouldntStoreUserPolicyFileInModifyAuthFactorIntents))
            .Wrap(std::move(user_policy_store_status).err_status()));
    return;
  }
  SetAuthIntentsForAuthFactorType(*type, driver, new_auth_factor_policy,
                                  /*is_persistent_user=*/!is_ephemeral_user,
                                  /*is_ephemeral_user=*/is_ephemeral_user,
                                  reply.mutable_auth_intents());
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::GetAuthFactorExtendedInfo(
    user_data_auth::GetAuthFactorExtendedInfoRequest request,
    OnDoneCallback<user_data_auth::GetAuthFactorExtendedInfoReply> on_done) {
  AssertOnMountThread();

  user_data_auth::GetAuthFactorExtendedInfoReply reply;

  // Compute the account_id and obfuscated user name from the request.
  ObfuscatedUsername obfuscated_username =
      SanitizeUserName(GetAccountId(request.account_id()));

  // Try to find the relevant auth factor with the given label and convert it
  // into an auth factor proto.
  user_data_auth::AuthFactor auth_factor_proto;
  std::optional<AuthFactorType> auth_factor_type;
  for (const auto& [label, type] :
       auth_factor_manager_->ListAuthFactors(obfuscated_username)) {
    if (label == request.auth_factor_label()) {
      // Save the type.
      auth_factor_type = type;
      // Attempt to load the factor and then load it into the response.
      auto auth_factor = auth_factor_manager_->LoadAuthFactor(
          obfuscated_username, type, label);
      if (auth_factor.ok()) {
        const AuthFactorDriver& driver =
            auth_factor_driver_manager_->GetDriver(type);
        if (auto converted_to_proto =
                driver.ConvertToProto(label, auth_factor->metadata())) {
          auth_factor_proto = std::move(*converted_to_proto);
        }
      }
      // Stop searching because we found the factor with the requested label,
      // even if loading it or converting it into a proto failed.
      break;
    }
  }

  // If we at least found the type, also load any type-specific extended info.
  if (!auth_factor_type) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthFactorExtendedInfoTypeFailure),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_KEY_NOT_FOUND));
    return;
  }
  switch (*auth_factor_type) {
    case AuthFactorType::kCryptohomeRecovery: {
      if (!request.has_recovery_info_request()) {
        ReplyWithError(
            std::move(on_done), reply,
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocUserDataAuthFactorExtendedInfoRecoveryIdFailure),
                ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
                user_data_auth::CryptohomeErrorCode::
                    CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }
      std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
          cryptorecovery::RecoveryCryptoImpl::Create(recovery_crypto_,
                                                     platform_);
      if (!recovery) {
        ReplyWithError(
            std::move(on_done), reply,
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocUserDataAuthRecoveryObjectFailureGetRecoveryId),
                ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
                user_data_auth::CryptohomeErrorCode::
                    CRYPTOHOME_ERROR_RECOVERY_FATAL));
        return;
      }
      std::vector<std::string> recovery_ids = recovery->GetLastRecoveryIds(
          request.account_id(), request.recovery_info_request().max_depth());
      user_data_auth::RecoveryExtendedInfoReply recovery_reply;
      for (const std::string& recovery_id : recovery_ids) {
        recovery_reply.add_recovery_ids(recovery_id);
      }
      std::string recovery_seed =
          recovery->LoadStoredRecoverySeed(request.account_id());
      recovery_reply.set_recovery_seed(recovery_seed);
      *reply.mutable_recovery_info_reply() = std::move(recovery_reply);
      break;
    }
    default: {
      LOG(WARNING) << AuthFactorTypeToString(*auth_factor_type)
                   << " factor type does not support extended info.";
    }
  }
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::GenerateFreshRecoveryId(
    user_data_auth::GenerateFreshRecoveryIdRequest request,
    OnDoneCallback<user_data_auth::GenerateFreshRecoveryIdReply> on_done) {
  AssertOnMountThread();

  user_data_auth::GenerateFreshRecoveryIdReply reply;
  std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
      cryptorecovery::RecoveryCryptoImpl::Create(recovery_crypto_, platform_);
  if (!recovery || recovery->GenerateFreshRecoveryId(request.account_id())) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthFreshRecoveryIdFailure),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_RECOVERY_FATAL));
    return;
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::PrepareAuthFactor(
    user_data_auth::PrepareAuthFactorRequest request,
    OnDoneCallback<user_data_auth::PrepareAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthPrepareAuthFactorAuthSessionNotFound),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::PrepareAuthFactorWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::PrepareAuthFactorWithSession(
    user_data_auth::PrepareAuthFactorRequest request,
    OnDoneCallback<user_data_auth::PrepareAuthFactorReply> on_done,
    InUseAuthSession auth_session) {
  AuthSession* auth_session_ptr = auth_session.Get();
  auth_session_ptr->PrepareAuthFactor(
      request,
      base::BindOnce(
          [](InUseAuthSession auth_session, std::optional<AuthFactorType> type,
             OnDoneCallback<user_data_auth::PrepareAuthFactorReply> on_done,
             CryptohomeStatus status) {
            user_data_auth::PrepareAuthFactorReply reply;
            if (type) {
              if (AuthSession* auth_session_ptr = auth_session.Get()) {
                if (const PrepareOutput* prepare_output =
                        auth_session_ptr->GetFactorTypePrepareOutput(*type)) {
                  *reply.mutable_prepare_output() =
                      PrepareOutputToProto(*prepare_output);
                }
              }
            }
            ReplyWithError<user_data_auth::PrepareAuthFactorReply>(
                std::move(on_done), std::move(reply), std::move(status));
          },
          std::move(auth_session).BindForCallback(),
          AuthFactorTypeFromProto(request.auth_factor_type()),
          std::move(on_done)));
}

void UserDataAuth::TerminateAuthFactor(
    user_data_auth::TerminateAuthFactorRequest request,
    OnDoneCallback<user_data_auth::TerminateAuthFactorReply> on_done) {
  AssertOnMountThread();
  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthTerminateAuthFactorNoAuthSession),
      std::move(request), std::move(on_done),
      base::BindOnce(
          [](user_data_auth::TerminateAuthFactorRequest request,
             OnDoneCallback<user_data_auth::TerminateAuthFactorReply> on_done,
             InUseAuthSession auth_session) {
            AuthSession* auth_session_ptr = auth_session.Get();
            auth_session_ptr->TerminateAuthFactor(
                request,
                base::BindOnce(
                    &ReplyWithStatus<user_data_auth::TerminateAuthFactorReply>,
                    std::move(auth_session).BindForCallback(),
                    std::move(on_done)));
          }));
}

void UserDataAuth::GetAuthSessionStatus(
    user_data_auth::GetAuthSessionStatusRequest request,
    OnDoneCallback<user_data_auth::GetAuthSessionStatusReply> on_done) {
  AssertOnMountThread();
  RunWithAuthSessionWhenAvailable(
      auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthGetAuthSessionStatusNoAuthSession),
      std::move(request), std::move(on_done),
      base::BindOnce(
          [](user_data_auth::GetAuthSessionStatusRequest request,
             OnDoneCallback<user_data_auth::GetAuthSessionStatusReply> on_done,
             InUseAuthSession auth_session) {
            user_data_auth::GetAuthSessionStatusReply reply;
            PopulateAuthSessionProperties(auth_session,
                                          reply.mutable_auth_properties());
            ReplyWithError(std::move(on_done), std::move(reply),
                           OkStatus<CryptohomeError>());
          }));
}

void UserDataAuth::LockFactorUntilReboot(
    user_data_auth::LockFactorUntilRebootRequest request,
    OnDoneCallback<user_data_auth::LockFactorUntilRebootReply> on_done) {
  AssertOnMountThread();
  user_data_auth::LockFactorUntilRebootReply reply;

  if (request.auth_factor_type() !=
      user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthWrongFactorTypeInLockFactor),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
    return;
  }

  if (!platform_->FileExists(GetRecoveryFactorLockPath()) &&
      !platform_->TouchFileDurable(GetRecoveryFactorLockPath())) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthTouchFailedInLockFactor),
            ErrorActionSet({PossibleAction::kRetry, PossibleAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    return;
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::CreateVaultKeyset(
    user_data_auth::CreateVaultKeysetRequest request,
    OnDoneCallback<user_data_auth::CreateVaultKeysetReply> on_done) {
  user_data_auth::CreateVaultKeysetReply reply;

  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInCreateVaultKeyset),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthInCreateVaultKeyset),
      std::move(request), std::move(on_done),
      base::BindOnce(
          [](CreateVaultKeysetRpcImpl* create_vault_keyset_impl,
             user_data_auth::CreateVaultKeysetRequest request,
             OnDoneCallback<user_data_auth::CreateVaultKeysetReply> on_done,
             InUseAuthSession auth_session) {
            AuthSession* auth_session_ptr = auth_session.Get();
            create_vault_keyset_impl->CreateVaultKeyset(
                request, *auth_session_ptr,
                base::BindOnce(
                    &ReplyWithStatus<user_data_auth::CreateVaultKeysetReply>,
                    std::move(auth_session).BindForCallback(),
                    std::move(on_done)));
          },
          create_vault_keyset_impl_.get()));
}

void UserDataAuth::MigrateLegacyFingerprints(
    user_data_auth::MigrateLegacyFingerprintsRequest request,
    OnDoneCallback<user_data_auth::MigrateLegacyFingerprintsReply> on_done) {
  AssertOnMountThread();
  RunWithAuthorizedAuthSessionWhenAvailable(
      AuthIntent::kDecrypt, auth_session_manager_,
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInMigrateFps),
      CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthInMigrateFps),
      std::move(request), std::move(on_done),
      base::BindOnce(&UserDataAuth::MigrateLegacyFingerprintsWithSession,
                     base::Unretained(this)));
}

void UserDataAuth::MigrateLegacyFingerprintsWithSession(
    user_data_auth::MigrateLegacyFingerprintsRequest request,
    OnDoneCallback<user_data_auth::MigrateLegacyFingerprintsReply> on_done,
    InUseAuthSession auth_session) {
  user_data_auth::MigrateLegacyFingerprintsReply reply;
  if (auth_session->ephemeral_user()) {
    auto status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthEphemeralAuthSessionAttemptMigrateFps),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    ReplyWithError(std::move(on_done), reply, status);
    return;
  }

  // Only AuthSession for decrypt supports legacy fingerprint migration.
  auto* session_decrypt = auth_session->GetAuthForDecrypt();
  if (!session_decrypt) {
    auto status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionDecryptFailedInMigrateFps),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    ReplyWithError(std::move(on_done), reply, status);
    return;
  }

  // Check the user is already mounted.
  UserSession* const session = sessions_->Find(auth_session->username());
  if (!session || !session->IsActive()) {
    auto status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthGetSessionFailedInMigrateFps),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    ReplyWithError(std::move(on_done), reply, status);
    return;
  }

  session_decrypt->MigrateLegacyFingerprints(base::BindOnce(
      &ReplyWithStatus<user_data_auth::MigrateLegacyFingerprintsReply>,
      std::move(auth_session).BindForCallback(), std::move(on_done)));
}

}  // namespace cryptohome
