// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_H_
#define CRYPTOHOME_AUTH_SESSION_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/containers/flat_set.h>
#include <base/containers/span.h>
#include <base/memory/weak_ptr.h>
#include <base/timer/timer.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <libhwsec/structures/explicit_init.h>
#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_map.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/cryptohome_mount_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/username.h"
#include "featured/c_feature_library.h"
#include "featured/feature_library.h"

namespace cryptohome {

// This enum holds the states an AuthSession could be in during the session.
enum class AuthStatus {
  // kAuthStatusFurtherFactorRequired is a state where the session is waiting
  // for one or more factors so that the session can continue the processes of
  // authenticating a user. This is the state the AuthSession starts in by
  // default.
  kAuthStatusFurtherFactorRequired,
  // kAuthStatusTimedOut tells the user to restart the AuthSession because
  // the session has timed out.
  kAuthStatusTimedOut,
  // kAuthStatusAuthenticated tells the user that the session is authenticated
  // and that file system keys are available should they be required.
  kAuthStatusAuthenticated
  // TODO(crbug.com/1154912): Complete the implementation of AuthStatus.
};

// Control switch value for enabling backup VaultKeyset creation with USS.
inline constexpr struct VariationsFeature
    kCrOSLateBootMigrateToUserSecretStash = {
        .name = "CrOSLateBootMigrateToUserSecretStash",
        .default_state = FEATURE_ENABLED_BY_DEFAULT,
};

// The list of all intents satisfied when the auth session is "fully
// authenticated". Useful for places that want to set the "fully authenticated"
// state.
constexpr AuthIntent kAuthorizedIntentsForFullAuth[] = {
    AuthIntent::kDecrypt, AuthIntent::kVerifyOnly};

// This class starts a session for the user to authenticate with their
// credentials.
class AuthSession final {
 public:
  using StatusCallback = base::OnceCallback<void(CryptohomeStatus)>;

  // Parameter struct used to specify all the base parameters of AuthSession.
  // These parameters do not include the underlying interfaces that AuthSession
  // depends on, which are defined below in a separate parameter struct.
  struct Params {
    hwsec::ExplicitInit<Username> username;
    hwsec::ExplicitInit<ObfuscatedUsername> obfuscated_username;
    hwsec::ExplicitInit<bool> is_ephemeral_user;
    hwsec::ExplicitInit<AuthIntent> intent;
    hwsec::ExplicitInit<bool> user_exists;
    AuthFactorMap auth_factor_map;
    hwsec::ExplicitInit<bool> migrate_to_user_secret_stash;
  };

  // Parameter struct used to supply all of the backing APIs that AuthSession
  // depends on. All of these pointers must be valid for the entire lifetime of
  // the AuthSession object.
  struct BackingApis {
    Crypto* crypto = nullptr;
    Platform* platform = nullptr;
    UserSessionMap* user_session_map = nullptr;
    KeysetManagement* keyset_management = nullptr;
    AuthBlockUtility* auth_block_utility = nullptr;
    AuthFactorManager* auth_factor_manager = nullptr;
    UserSecretStashStorage* user_secret_stash_storage = nullptr;
  };

  // Creates new auth session for account_id. This method returns a unique_ptr
  // to the created AuthSession for the auth_session_manager to hold.
  static std::unique_ptr<AuthSession> Create(
      Username username,
      unsigned int flags,
      AuthIntent intent,
      feature::PlatformFeaturesInterface* feature_lib,
      BackingApis backing_apis);

  // Construct an AuthSession initialized with all of the given state. This
  // should generally only be used directly in testing; production code should
  // prefer to call the Create factory function.
  AuthSession(Params params, BackingApis backing_apis);

  ~AuthSession();

  // Returns the full unhashed user name.
  const Username& username() const { return username_; }
  // Returns the obfuscated (sanitized) user name.
  const ObfuscatedUsername& obfuscated_username() const {
    return obfuscated_username_;
  }

  AuthIntent auth_intent() const { return auth_intent_; }

  // Returns the token which is used to identify the current AuthSession.
  const base::UnguessableToken& token() const { return token_; }
  const std::string& serialized_token() const { return serialized_token_; }

  // This function return the current status of this AuthSession.
  AuthStatus status() const { return status_; }

  // Returns the intents that the AuthSession has been authorized for.
  const base::flat_set<AuthIntent>& authorized_intents() const {
    return authorized_intents_;
  }

  // Returns the file system keyset used to access the filesystem. This is set
  // when the session gets into an authenticated state and so the caller must
  // ensure that the AuthSession has been authenticated before accessing this.
  const FileSystemKeyset& file_system_keyset() const;

  // This function returns if the user existed when the auth session started.
  bool user_exists() const { return user_exists_; }

  // This function returns if the AuthSession is being setup for an ephemeral
  // user.
  bool ephemeral_user() const { return is_ephemeral_user_; }

  // Returns the key data with which this AuthSession is authenticated with.
  const KeyData& current_key_data() const { return key_data_; }

  // Returns the map from the label to the auth factor.
  const AuthFactorMap& auth_factor_map() const { return auth_factor_map_; }

  // Indicates if the session has a User Secret Stash configured.
  bool has_user_secret_stash() const {
    return user_secret_stash_ && user_secret_stash_main_key_;
  }

  // Indicates if there is a reset_secret in session's User Secret Stash for
  // the given label.
  inline bool HasResetSecretInUssForTesting(const std::string& label) const {
    return user_secret_stash_ &&
           user_secret_stash_->GetResetSecretForLabel(label).has_value();
  }

  // OnUserCreated is called when the user and their homedir are newly created.
  // Must be called no more than once.
  CryptohomeStatus OnUserCreated();

  // AddAuthFactor is called when newly created or existing user wants to add
  // new AuthFactor.
  void AddAuthFactor(const user_data_auth::AddAuthFactorRequest& request,
                     StatusCallback on_done);

  // Authenticate is called when the user wants to authenticate the current
  // AuthSession via an auth factor. It may be called multiple times depending
  // on errors or various steps involved in multi-factor authentication.
  // Note: only USS users are supported currently.
  void AuthenticateAuthFactor(base::span<const std::string> auth_factor_labels,
                              const user_data_auth::AuthInput& auth_input_proto,
                              StatusCallback on_done);

  // RemoveAuthFactor is called when the user wants to remove auth factor
  // provided in the `request`.
  void RemoveAuthFactor(const user_data_auth::RemoveAuthFactorRequest& request,
                        StatusCallback on_done);

  // UpdateAuthFactor is called when the user wants to update auth factor
  // provided in the `request`.
  void UpdateAuthFactor(const user_data_auth::UpdateAuthFactorRequest& request,
                        StatusCallback on_done);

  // PrepareAuthFactor prepares an auth factor, e.g. fingerprint auth factor
  // which is not directly associated with a knowledge factor.
  void PrepareAuthFactor(
      const user_data_auth::PrepareAuthFactorRequest& request,
      StatusCallback on_done);

  // TerminatesAuthFactor stops an async auth factor, e.g. fingerprint auth
  // factor.
  void TerminateAuthFactor(
      const user_data_auth::TerminateAuthFactorRequest& request,
      StatusCallback on_done);

  // Generates a payload that will be sent to the server for cryptohome recovery
  // AuthFactor authentication. GetRecoveryRequest saves data in the
  // AuthSession state. This call is required before the AuthenticateAuthFactor
  // call for cryptohome recovery AuthFactor.
  void GetRecoveryRequest(
      user_data_auth::GetRecoveryRequestRequest request,
      base::OnceCallback<void(const user_data_auth::GetRecoveryRequestReply&)>
          on_done);

  void OnMigrationUssCreated(AuthBlockType auth_block_type,
                             AuthFactorType auth_factor_type,
                             const AuthFactorMetadata& auth_factor_metadata,
                             const AuthInput& auth_input,
                             CryptohomeStatus pre_migration_status,
                             StatusCallback on_done,
                             std::unique_ptr<UserSecretStash> user_secret_stash,
                             brillo::SecureBlob uss_main_key);

  // Sets |vault_keyset_| for testing purpose.
  void set_vault_keyset_for_testing(std::unique_ptr<VaultKeyset> value) {
    vault_keyset_ = std::move(value);
  }

  bool enable_create_backup_vk_with_uss_for_testing() const {
    return enable_create_backup_vk_with_uss_;
  }

  // Static function which returns a serialized token in a vector format. The
  // token is serialized into two uint64_t values which are stored in string of
  // size 16 bytes. The first 8 bytes represent the high value of the serialized
  // token, the next 8 represent the low value of the serialized token.
  static std::optional<std::string> GetSerializedStringFromToken(
      const base::UnguessableToken& token);

  // Static function which returns UnguessableToken object after deconstructing
  // the string formed in GetSerializedStringFromToken.
  static std::optional<base::UnguessableToken> GetTokenFromSerializedString(
      const std::string& serialized_token);

  // Extends the timer for the AuthSession by kAuthSessionExtensionInMinutes.
  CryptohomeStatus ExtendTimeoutTimer(
      const base::TimeDelta kAuthSessionExtension);

  // Set status for testing only.
  void SetStatus(const AuthStatus status) { status_ = status; }

  // Get the time remaining for this AuthSession's life.
  base::TimeDelta GetRemainingTime() const;

  // Get the hibernate secret, derived from the file system keyset.
  std::unique_ptr<brillo::SecureBlob> GetHibernateSecret();

  // Sets a callback to call when the AuthSession is timed out. Note that this
  // may trigger immediately if the session is already timed out.
  void SetOnTimeoutCallback(
      base::OnceCallback<void(const base::UnguessableToken&)> on_timeout);

 private:
  // AuthSessionTimedOut is called when the session times out and cleans up
  // credentials that may be in memory. Be aware that this may destroy the
  // AuthSession object if the owner of the object is using a callback to clean
  // up the objects when they time out.
  void AuthSessionTimedOut();

  // Emits a debug log message with this Auth Session's initial state.
  void RecordAuthSessionStart() const;

  // Switches the state to authorize the specified intents. Starts or restarts
  // the timer when applicable.
  void SetAuthSessionAsAuthenticated(
      base::span<const AuthIntent> new_authorized_intents);

  // Converts the D-Bus AuthInput proto into the C++ struct. Returns nullopt on
  // failure.
  CryptohomeStatusOr<AuthInput> CreateAuthInputForAuthentication(
      const user_data_auth::AuthInput& auth_input_proto,
      const AuthFactorMetadata& auth_factor_metadata);
  // Same as above, but additionally sets extra fields for resettable factors.
  CryptohomeStatusOr<AuthInput> CreateAuthInputForAdding(
      const user_data_auth::AuthInput& auth_input_proto,
      AuthFactorType auth_factor_type,
      const AuthFactorMetadata& auth_factor_metadata);

  // Creates AuthInput for migration from an AuthInput by adding `reset_secret`
  // if needed. If this is called during the AuthenticateAuthFactor, after the
  // successful authentication of the PIN factor, reset secret is obtained
  // directly from the decrypted PIN VaultKeyset. If this is called during the
  // UpdateAuthFactor, when AuthSession has a decrypted password VaultKesyet
  // available, |reset_secret| is derived from the |reset_seed| on the
  // password VaultKeyset. In each case a new backend pinweaver node is created.
  CryptohomeStatusOr<AuthInput> CreateAuthInputForMigration(
      const AuthInput& auth_input, AuthFactorType auth_factor_type);

  // Initializes a ChallengeCredentialAuthInput, i.e.
  // {.public_key_spki_der, .challenge_signature_algorithms} from
  // the challenge_response_key values in in authorization
  std::optional<ChallengeCredentialAuthInput>
  CreateChallengeCredentialAuthInput(const AuthorizationRequest& authorization);

  // This function attempts to add verifier for the given label based on the
  // AuthInput. If it succeeds it will return a pointer to the verifier.
  // Otherwise it will return null.
  CredentialVerifier* AddCredentialVerifier(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthInput& auth_input);

  // Set the timeout timer to now + delay
  void SetTimeoutTimer(const base::TimeDelta& delay);

  // Helper function to update a keyset on disk on KeyBlobs generated. If update
  // succeeds |vault_keyset_| is also updated. Failure doesn't return error and
  // doesn't block authentication operations.
  void ResaveKeysetOnKeyBlobsGenerated(
      VaultKeyset updated_vault_keyset,
      CryptoStatus error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // Adds VaultKeyset for the |obfuscated_username_| by calling
  // KeysetManagement::AddInitialKeyset() or KeysetManagement::AddKeyset()
  // based on |is_initial_keyset|.
  CryptohomeStatus AddVaultKeyset(const std::string& key_label,
                                  const KeyData& key_data,
                                  bool is_initial_keyset,
                                  VaultKeysetIntent vk_backup_intent,
                                  std::unique_ptr<KeyBlobs> key_blobs,
                                  std::unique_ptr<AuthBlockState> auth_state);

  // Creates and persist VaultKeyset for the |obfuscated_username_|. This
  // function is needed for processing callback results in an asynchronous
  // manner through |on_done| callback.
  void CreateAndPersistVaultKeyset(const KeyData& key_data,
                                   AuthInput auth_input,
                                   std::unique_ptr<AuthSessionPerformanceTimer>
                                       auth_session_performance_timer,
                                   StatusCallback on_done,
                                   CryptoStatus callback_error,
                                   std::unique_ptr<KeyBlobs> key_blobs,
                                   std::unique_ptr<AuthBlockState> auth_state);

  // Updates a VaultKeyset for the |obfuscated_username_| by calling
  // KeysetManagement::UpdateKeysetWithKeyBlobs(). The VaultKeyset and it's
  // corresponding label are updated through the information provided by
  // |key_data|. This function is needed for processing callback results in an
  // asynchronous manner through |on_done| callback.
  void UpdateVaultKeyset(AuthFactorType auth_factor_type,
                         const KeyData& key_data,
                         const AuthInput& auth_input,
                         std::unique_ptr<AuthSessionPerformanceTimer>
                             auth_session_performance_timer,
                         StatusCallback on_done,
                         CryptoStatus callback_error,
                         std::unique_ptr<KeyBlobs> key_blobs,
                         std::unique_ptr<AuthBlockState> auth_state);

  // Persists key blocks for a new secret to the USS and onto disk. Upon
  // completion the |on_done| callback will be called. Designed to be used in
  // conjunction with an async CreateKeyBlobs call by binding all of the
  // initial parameters to make an AuthBlock::CreateCallback.
  void PersistAuthFactorToUserSecretStash(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const AuthInput& auth_input,
      const KeyData& key_data,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done,
      CryptoStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // Persists from a migrated VaultKeyset AuthFactor and USS key block. Upon
  // completion the |on_done| callback will be called with the pre-migration
  // state.
  void PersistAuthFactorToUserSecretStashOnMigration(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const AuthInput& auth_input,
      const KeyData& key_data,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done,
      CryptohomeStatus pre_migration_status,
      CryptoStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // The implementation function to persists an AuthFactor and a USS key
  // block for a new secret.
  CryptohomeStatus PersistAuthFactorToUserSecretStashImpl(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const AuthInput& auth_input,
      const KeyData& key_data,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      CryptoStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // Process the completion of a verify-only authentication attempt. The
  // |on_done| callback will be called after the results of the verification are
  // processed. Designed to be used in conjunction with
  // CredentialVerifier::Verify as the CryptohomeStatusCallback.
  void CompleteVerifyOnlyAuthentication(StatusCallback on_done,
                                        CryptohomeStatus error);

  // Add the new factor into the USS in-memory.
  CryptohomeStatus AddAuthFactorToUssInMemory(
      AuthFactor& auth_factor,
      const AuthInput& auth_input,
      const brillo::SecureBlob& uss_credential_secret);

  // Implements the AddauthFactor by adding the credential backing store either
  // with AuthFactor & UsersecretStash or VaultKeyset.
  void AddAuthFactorImpl(AuthFactorType auth_factor_type,
                         const std::string& auth_factor_label,
                         const AuthFactorMetadata& auth_factor_metadata,
                         const AuthInput& auth_input,
                         std::unique_ptr<AuthSessionPerformanceTimer>
                             auth_session_performance_timer,
                         StatusCallback on_done);

  // Returns the callback function to add and AuthFactor for the right key store
  // type.
  AuthBlock::CreateCallback GetAddAuthFactorCallback(
      const AuthFactorType& auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const KeyData& key_data,
      const AuthInput& auth_input,
      const AuthFactorStorageType auth_factor_storage_type,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done);

  // Returns the callback function to update and AuthFactor for the right key
  // store type.
  AuthBlock::CreateCallback GetUpdateAuthFactorCallback(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const KeyData& key_data,
      const AuthInput& auth_input,
      const AuthFactorStorageType auth_factor_storage_type,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done);

  // Adds a credential verifier for the ephemeral user session.
  void AddAuthFactorForEphemeral(AuthFactorType auth_factor_type,
                                 const std::string& auth_factor_label,
                                 const AuthInput& auth_input,
                                 StatusCallback on_done);

  // Loads and decrypts the USS payload with |auth_factor_label| using the
  // given KeyBlobs. Designed to be used in conjunction with an async
  // DeriveKeyBlobs call by binding all of the initial parameters to make an
  // AuthBlock::DeriveCallback.
  void LoadUSSMainKeyAndFsKeyset(AuthFactorType auth_factor_type,
                                 const std::string& auth_factor_label,
                                 const AuthInput& auth_input,
                                 std::unique_ptr<AuthSessionPerformanceTimer>
                                     auth_session_performance_timer,
                                 StatusCallback on_done,
                                 CryptoStatus callback_error,
                                 std::unique_ptr<KeyBlobs> key_blobs);

  // This function is used to reset the attempt count for a low entropy
  // credential. Currently, this resets all low entropy credentials both stored
  // in USS and VK. In the USSv2 world, with passwords or even other auth types
  // backed by PinWeaver, the code will need to reset specific LE credentials.
  void ResetLECredentials();

  // Authenticate the user with the single given auth factor. Additional
  // parameters are provided to aid legacy vault keyset authentication and
  // migration.
  void AuthenticateViaSingleFactor(
      const AuthFactorType& request_auth_factor_type,
      const std::string& auth_factor_label,
      const AuthInput& auth_input,
      const AuthFactorMetadata& metadata,
      const AuthFactorMap::ValueView& stored_auth_factor,
      StatusCallback on_done);

  // Authenticates the user using USS with the |auth_factor_label|, |auth_input|
  // and the |auth_factor|.
  void AuthenticateViaUserSecretStash(
      const std::string& auth_factor_label,
      const AuthInput auth_input,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      const AuthFactor& auth_factor,
      StatusCallback on_done);

  // Authenticates the user using VaultKeysets with the given |auth_input|.
  void AuthenticateViaVaultKeysetAndMigrateToUss(
      AuthFactorType request_auth_factor_type,
      const std::string& key_label,
      const AuthInput& auth_input,
      const AuthFactorMetadata& metadata,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done);

  // Fetches a valid VaultKeyset for |obfuscated_username_| that matches the
  // label provided by key_data_.label(). The VaultKeyset is loaded and
  // initialized into |vault_keyset_| through
  // KeysetManagement::GetValidKeysetWithKeyBlobs(). This function is needed for
  // processing callback results in an asynchronous manner through the |on_done|
  // callback.
  void LoadVaultKeysetAndFsKeys(AuthFactorType request_auth_factor_type,
                                const AuthInput& auth_input,
                                AuthBlockType auth_block_type,
                                const AuthFactorMetadata& metadata,
                                std::unique_ptr<AuthSessionPerformanceTimer>
                                    auth_session_performance_timer,
                                StatusCallback on_done,
                                CryptoStatus error,
                                std::unique_ptr<KeyBlobs> key_blobs);

  // Updates, wraps and resaves |vault_keyset_| and restores on failure.
  // |user_input| is needed to generate the AuthInput used for key blob creation
  // to wrap the updated keyset.
  AuthBlockType ResaveVaultKeysetIfNeeded(
      const std::optional<brillo::SecureBlob> user_input,
      AuthBlockType auth_block_type);

  // Removes the key block with the provided `auth_factor_label` from the USS
  // and removes the `auth_factor` from disk.
  CryptohomeStatus RemoveAuthFactorViaUserSecretStash(
      const std::string& auth_factor_label, const AuthFactor& auth_factor);

  // Remove the factor from the USS in-memory.
  CryptohomeStatus RemoveAuthFactorFromUssInMemory(
      const std::string& auth_factor_label);

  // Creates a new per-credential secret, updates the secret in the USS and
  // updates the auth block state on disk.
  void UpdateAuthFactorViaUserSecretStash(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const KeyData& key_data,
      const AuthInput& auth_input,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done,
      CryptoStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // Handles the completion of an async PrepareAuthFactor call. Captures the
  // token and forwards the result to the given status callback.
  void OnPrepareAuthFactorDone(
      StatusCallback on_done,
      CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>> token);

  // Prepares the WebAuthn secret using file_system_keyset.
  CryptohomeStatus PrepareWebAuthnSecret();

  Username username_;
  ObfuscatedUsername obfuscated_username_;

  // AuthSession's flag configuration.
  const bool is_ephemeral_user_;
  const AuthIntent auth_intent_;

  AuthStatus status_ = AuthStatus::kAuthStatusFurtherFactorRequired;
  base::flat_set<AuthIntent> authorized_intents_;
  base::OneShotTimer timeout_timer_;
  base::TimeTicks timeout_timer_start_time_;
  base::TimeTicks auth_session_creation_time_;
  base::TimeTicks authenticated_time_;
  base::OnceCallback<void(const base::UnguessableToken&)> on_timeout_;

  std::unique_ptr<AuthFactor> auth_factor_;
  // The decrypted UserSecretStash. Only populated for users who have it (legacy
  // users who only have vault keysets will have this field equal to null).
  std::unique_ptr<UserSecretStash> user_secret_stash_;
  // The UserSecretStash main key. Only populated iff |user_secret_stash_| is.
  std::optional<brillo::SecureBlob> user_secret_stash_main_key_;
  // The creator of the AuthSession object is responsible for the life of
  // Crypto object.
  Crypto* const crypto_;
  // The creator of the AuthSession object is responsible for the life of
  // Platform object.
  Platform* const platform_;
  // The user session map and a verifier forwarder associated with it.
  // Unowned pointer.
  UserSessionMap* const user_session_map_;
  UserSessionMap::VerifierForwarder verifier_forwarder_;
  // The creator of the AuthSession object is responsible for the life of
  // KeysetManagement object.
  // TODO(crbug.com/1171024): Change KeysetManagement to use AuthBlock.
  KeysetManagement* const keyset_management_;
  // Unowned pointer.
  AuthBlockUtility* const auth_block_utility_;
  // Unowned pointer.
  AuthFactorManager* const auth_factor_manager_;
  // Unowned pointer.
  UserSecretStashStorage* const user_secret_stash_storage_;
  // A stateless object to convert AuthFactor API to VaultKeyset KeyData and
  // VaultKeysets to AuthFactor API.
  AuthFactorVaultKeysetConverter converter_;

  const base::UnguessableToken token_;
  const std::string serialized_token_;

  // Used to decrypt/ encrypt & store credentials.
  std::unique_ptr<VaultKeyset> vault_keyset_;
  // Used to store key meta data.
  KeyData key_data_;
  // FileSystemKeyset is needed by cryptohome to mount a user.
  std::optional<FileSystemKeyset> file_system_keyset_;
  // Whether the user existed at the time this object was constructed.
  bool user_exists_;
  // Map containing the auth factors already configured for this user.
  AuthFactorMap auth_factor_map_;
  // Key used by AuthenticateAuthFactor for cryptohome recovery AuthFactor.
  // It's set only after GetRecoveryRequest() call, and is std::nullopt in other
  // cases.
  std::optional<brillo::SecureBlob> cryptohome_recovery_ephemeral_pub_key_;
  // Switch to enable creation of the backup VaultKeysets together with the USS.
  bool enable_create_backup_vk_with_uss_ = true;
  // Tokens from active auth factors, keyed off of the token's auth factor type.
  std::map<AuthFactorType, std::unique_ptr<PreparedAuthFactorToken>>
      active_auth_factor_tokens_;
  // Specify whether USS migration should happen.
  bool migrate_to_user_secret_stash_;

  // Should be the last member.
  base::WeakPtrFactory<AuthSession> weak_factory_{this};

  FRIEND_TEST(AuthSessionInterfaceTest, PreparePersistentVaultNoShadowDir);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewUser);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewUserTwice);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewEphemeralUser);
  FRIEND_TEST(AuthSessionTest, AuthenticateExistingUser);
  FRIEND_TEST(AuthSessionTest, AuthenticateWithPIN);
  FRIEND_TEST(AuthSessionTest, AuthenticateExistingUserFailure);
  FRIEND_TEST(AuthSessionTest, ExtensionTest);
  FRIEND_TEST(AuthSessionTest, UssMigrationFlagCheckFailure);
  FRIEND_TEST(AuthSessionTest, GetCredentialRegularUser);
  FRIEND_TEST(AuthSessionTest, GetCredentialKioskUser);
  FRIEND_TEST(AuthSessionWithUssExperimentTest, AddPasswordAuthFactorViaUss);
  FRIEND_TEST(AuthSessionWithUssExperimentTest,
              AddPasswordAuthFactorViaAsyncUss);
  FRIEND_TEST(AuthSessionWithUssExperimentTest, PrepareLegacyFingerprintAuth);
  FRIEND_TEST(AuthSessionWithUssExperimentTest, RemoveAuthFactor);
  FRIEND_TEST(UserDataAuthTest, CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly);
  FRIEND_TEST(UserDataAuthTest,
              CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly_FirstBoot);
  FRIEND_TEST(UserDataAuthExTest, MountUnauthenticatedAuthSession);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
  FRIEND_TEST(UserDataAuthExTest, ExtendAuthSession);
  FRIEND_TEST(UserDataAuthExTest, CheckTimeoutTimerSetAfterAuthentication);
  FRIEND_TEST(UserDataAuthExTest,
              StartMigrateToDircryptoWithAuthenticatedAuthSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_H_
