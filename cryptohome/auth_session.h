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
#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
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

  // Creates new auth session for account_id. This method returns a unique_ptr
  // to the created AuthSession for the auth_session_manager to hold.
  static CryptohomeStatusOr<std::unique_ptr<AuthSession>> Create(
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
      bool enable_create_backup_vk_with_uss);

  ~AuthSession();

  // Returns the full unhashed user name.
  const std::string& username() const { return username_; }
  // Returns the obfuscated (sanitized) user name.
  const std::string& obfuscated_username() const {
    return obfuscated_username_;
  }

  AuthIntent auth_intent() const { return auth_intent_; }

  // Returns the token which is used to identify the current AuthSession.
  const base::UnguessableToken& token() const { return token_; }
  const std::string& serialized_token() const { return serialized_token_; }

  // This function return the current status of this AuthSession.
  const AuthStatus GetStatus() const { return status_; }

  // Returns the intents that the AuthSession has been authorized for.
  const base::flat_set<AuthIntent>& authorized_intents() const {
    return authorized_intents_;
  }

  // OnUserCreated is called when the user and their homedir are newly created.
  // Must be called no more than once.
  CryptohomeStatus OnUserCreated();

  // AddCredentials is called when newly created or existing user wants to add
  // new credentials.
  void AddCredentials(const user_data_auth::AddCredentialsRequest& request,
                      StatusCallback on_done);

  // UpdateCredential is called when an existing user wants to update
  // an existing credential.
  void UpdateCredential(const user_data_auth::UpdateCredentialRequest& request,
                        StatusCallback on_done);

  // AddAuthFactor is called when newly created or existing user wants to add
  // new AuthFactor.
  void AddAuthFactor(const user_data_auth::AddAuthFactorRequest& request,
                     StatusCallback on_done);

  // Authenticate is called when the user wants to authenticate the current
  // AuthSession. It may be called multiple times depending on errors or various
  // steps involved in multi-factor authentication.
  void Authenticate(
      const cryptohome::AuthorizationRequest& authorization_request,
      StatusCallback on_done);

  // Authenticate is called when the user wants to authenticate the current
  // AuthSession via an auth factor. It may be called multiple times depending
  // on errors or various steps involved in multi-factor authentication.
  // Note: only USS users are supported currently.
  bool AuthenticateAuthFactor(
      const user_data_auth::AuthenticateAuthFactorRequest& request,
      StatusCallback on_done);

  // RemoveAuthFactor is called when the user wants to remove auth factor
  // provided in the `request`. Note: only USS users are supported currently.
  // TODO(b/236869367): Implement for VaultKeyset users.
  void RemoveAuthFactor(const user_data_auth::RemoveAuthFactorRequest& request,
                        StatusCallback on_done);

  // UpdateAuthFactor is called when the user wants to update auth factor
  // provided in the `request`. Note: only USS users are supported currently.
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
  bool GetRecoveryRequest(
      user_data_auth::GetRecoveryRequestRequest request,
      base::OnceCallback<void(const user_data_auth::GetRecoveryRequestReply&)>
          on_done);

  // Return a const reference to FileSystemKeyset.
  // FileSystemKeyset is set when the auth session gets into an authenticated
  // state. So, the caller must ensure that AuthSession is in authenticated
  // state before requesting the file system keyset.
  const FileSystemKeyset& file_system_keyset() const;

  // This function returns if the user existed when the auth session started.
  bool user_exists() const { return user_exists_; }

  // This function returns if the user has any credential configured. When a
  // credential is added, this value changes from false to true.
  bool user_has_configured_credential() const {
    return user_has_configured_credential_;
  }

  // This function returns if the user has any auth factors configured. When an
  // auth factor is added, this value changes from false to true.
  bool user_has_configured_auth_factor() const {
    return user_has_configured_auth_factor_;
  }

  // This function returns if the AuthSession is being setup for an ephemeral
  // user.
  bool ephemeral_user() const { return is_ephemeral_user_; }

  // Returns the key data with which this AuthSession is authenticated with.
  cryptohome::KeyData current_key_data() const { return key_data_; }

  // Returns the map of Key label and KeyData that will be used as a result of
  // StartAuthSession request.
  const std::map<std::string, cryptohome::KeyData>& key_label_data() const {
    return key_label_data_;
  }

  // Returns the map from the label to the auth factor.
  const std::map<std::string, std::unique_ptr<AuthFactor>>&
  label_to_auth_factor() const {
    return label_to_auth_factor_;
  }

  // Returns the decrypted USS object, or null if it's not available. Exposed
  // only for unit tests.
  const UserSecretStash* user_secret_stash_for_testing() const {
    return user_secret_stash_.get();
  }

  // Returns the decrypted USS Main Key, or nullopt if it's not available.
  // Exposed only for unit tests.
  const std::optional<brillo::SecureBlob>&
  user_secret_stash_main_key_for_testing() const {
    return user_secret_stash_main_key_;
  }

  const std::optional<brillo::SecureBlob>&
  cryptohome_recovery_ephemeral_pub_key_for_testing() const {
    return cryptohome_recovery_ephemeral_pub_key_;
  }

  // Sets |vault_keyset_| for testing purpose.
  void set_vault_keyset_for_testing(std::unique_ptr<VaultKeyset> value) {
    vault_keyset_ = std::move(value);
  }

  // Sets |label_to_auth_factor_| which maps existing AuthFactor labels to their
  // corresponding AuthFactors for testing purpose.
  void set_label_to_auth_factor_for_testing(
      std::map<std::string, std::unique_ptr<AuthFactor>> value) {
    label_to_auth_factor_ = std::move(value);
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

 private:
  // Caller needs to ensure that the passed raw pointers outlive the instance of
  // AuthSession.
  AuthSession(
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
      bool enable_create_backup_vk_with_uss);

  AuthSession() = delete;

  // Initialize boolean variables and AuthFactor maps such as
  // |label_to_auth_factor_|.
  CryptohomeStatus Initialize();

  // AuthSessionTimedOut is called when the session times out and cleans up
  // credentials that may be in memory. |on_timeout_| is also called to remove
  // this |AuthSession| reference from |UserDataAuth|.
  void AuthSessionTimedOut();

  // Emits a debug log message with this Auth Session's initial state.
  void RecordAuthSessionStart() const;

  // Switches the state to authorize the specified intents. Starts or restarts
  // the timer when applicable.
  void SetAuthSessionAsAuthenticated(
      base::span<const AuthIntent> new_authorized_intents);

  // This function returns credentials based on the state of the current
  // |AuthSession|.
  MountStatusOr<std::unique_ptr<Credentials>> GetCredentials(
      const cryptohome::AuthorizationRequest& authorization_request);

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

  // Initializes a ChallengeCredentialAuthInput, i.e.
  // {.public_key_spki_der, .challenge_signature_algorithms} from
  // the challenge_response_key values in in authorization
  std::optional<ChallengeCredentialAuthInput>
  CreateChallengeCredentialAuthInput(
      const cryptohome::AuthorizationRequest& authorization);

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

  // Determines which AuthBlockType to use, instantiates an AuthBlock of that
  // type, and uses that AuthBlock to derive KeyBlobs for the AuthSession to
  // add a VaultKeyset.
  void CreateKeyBlobsToAddKeyset(const AuthInput& auth_input,
                                 const KeyData& key_data,
                                 bool initial_keyset,
                                 std::unique_ptr<AuthSessionPerformanceTimer>
                                     auth_session_performance_timer,
                                 StatusCallback on_done);

  // Determines which AuthBlockType to use, instantiates an AuthBlock of that
  // type, and uses that AuthBlock to create KeyBlobs for the AuthSession to
  // update a VaultKeyset.
  void CreateKeyBlobsToUpdateKeyset(const Credentials& credentials,
                                    StatusCallback on_done);

  // Adds VaultKeyset for the |obfuscated_username_| by calling
  // KeysetManagement::AddInitialKeyset() or KeysetManagement::AddKeyset()
  // based on |initial_keyset|.
  CryptohomeStatus AddVaultKeyset(const KeyData& key_data,
                                  bool initial_keyset,
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
  // TODO(b/204482221): Make `auth_factor_type` mandatory.
  void UpdateVaultKeyset(std::optional<AuthFactorType> auth_factor_type,
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

  // Process the completion of a verify-only authentication attempt. The
  // |on_done| callback will be called after the results of the verification are
  // processed. Designed to be used in conjunction with
  // VerifyWithAuthFactorAsync as the CryptohomeStatusCallback.
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
  // credential. Currently, this resets all low entropy credentials. In the
  // USSv2 world, with passwords or even other auth types backed by PinWeaver,
  // the code will need to reset specific LE credentials.
  void ResetLECredentials();

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
  // TODO(b/204482221): Make `request_auth_factor_type` mandatory.
  bool AuthenticateViaVaultKeyset(
      std::optional<AuthFactorType> request_auth_factor_type,
      const std::string& key_label,
      const AuthInput& auth_input,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done);

  // Fetches a valid VaultKeyset for |obfuscated_username_| that matches the
  // label provided by key_data_.label(). The VaultKeyset is loaded and
  // initialized into |vault_keyset_| through
  // KeysetManagement::GetValidKeysetWithKeyBlobs(). This function is needed for
  // processing callback results in an asynchronous manner through the |on_done|
  // callback.
  // TODO(b/204482221): Make `request_auth_factor_type` mandatory.
  void LoadVaultKeysetAndFsKeys(
      std::optional<AuthFactorType> request_auth_factor_type,
      const std::optional<brillo::SecureBlob> passkey,
      const AuthBlockType& auth_block_type,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done,
      CryptoStatus error,
      std::unique_ptr<KeyBlobs> key_blobs);

  // Updates, wraps and resaves |vault_keyset_| and restores on failure.
  // |user_input| is needed to generate the AuthInput used for key blob creation
  // to wrap the updated keyset.
  void ResaveVaultKeysetIfNeeded(
      const std::optional<brillo::SecureBlob> user_input);

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

  // Prepares the WebAuthn secret using file_system_keyset.
  CryptohomeStatus PrepareWebAuthnSecret();

  const std::string username_;
  const std::string obfuscated_username_;
  const base::UnguessableToken token_;
  const std::string serialized_token_;

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
  // Used to decrypt/ encrypt & store credentials.
  std::unique_ptr<VaultKeyset> vault_keyset_;
  // A stateless object to convert AuthFactor API to VaultKeyset KeyData and
  // VaultKeysets to AuthFactor API.
  std::unique_ptr<AuthFactorVaultKeysetConverter> converter_;
  // Used to store key meta data.
  cryptohome::KeyData key_data_;
  // FileSystemKeyset is needed by cryptohome to mount a user.
  std::optional<FileSystemKeyset> file_system_keyset_ = std::nullopt;
  // Whether the user existed at the time this object was constructed.
  bool user_exists_ = false;
  // Whether the user has any credential configured so far.
  bool user_has_configured_credential_ = false;
  // Whether the user has any authfactor/uss configured so far.
  bool user_has_configured_auth_factor_ = false;
  // Map to store the label and public KeyData.
  // TODO(crbug.com/1171024): Change this to AuthFactor
  std::map<std::string, cryptohome::KeyData> key_label_data_;
  // Map containing the auth factors already configured for this user.
  std::map<std::string, std::unique_ptr<AuthFactor>> label_to_auth_factor_;
  // Key used by AuthenticateAuthFactor for cryptohome recovery AuthFactor.
  // It's set only after GetRecoveryRequest() call, and is std::nullopt in other
  // cases.
  std::optional<brillo::SecureBlob> cryptohome_recovery_ephemeral_pub_key_;
  // Switch to enable creation of the backup VaultKeysets together with the USS.
  bool enable_create_backup_vk_with_uss_;
  // A container to keep all async auth factor types that needs to be stopped
  // when the AuthSession lifetime ends.
  // TODO(b/246826331): keep a token that is returned by PrepareAuthFactor
  // instead of the type.
  std::set<AuthFactorType> active_auth_factor_types;

  // Should be the last member.
  base::WeakPtrFactory<AuthSession> weak_factory_{this};

  friend class AuthSessionTest;
  friend class AuthSessionInterfaceTest;
  friend class AuthSessionManagerTest;
  friend class AuthSessionTestWithKeysetManagement;
  FRIEND_TEST(AuthSessionManagerTest, CreateExpire);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewUser);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewUserTwice);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewEphemeralUser);
  FRIEND_TEST(AuthSessionTest, AuthenticateExistingUser);
  FRIEND_TEST(AuthSessionTest, AuthenticateWithPIN);
  FRIEND_TEST(AuthSessionTest, AuthenticateExistingUserFailure);
  FRIEND_TEST(AuthSessionTest, ExtensionTest);
  FRIEND_TEST(AuthSessionTest, InitiallyNotAuthenticated);
  FRIEND_TEST(AuthSessionTest, InitiallyNotAuthenticatedForExistingUser);
  FRIEND_TEST(AuthSessionTest, Username);
  FRIEND_TEST(AuthSessionTest, Intent);
  FRIEND_TEST(AuthSessionTest, TimeoutTest);
  FRIEND_TEST(AuthSessionTest, GetCredentialRegularUser);
  FRIEND_TEST(AuthSessionTest, GetCredentialKioskUser);
  FRIEND_TEST(AuthSessionTestWithKeysetManagement, USSEnabledRemovesBackupVKs);
  FRIEND_TEST(AuthSessionTestWithKeysetManagement, USSEnabledUpdateBackupVKs);
  FRIEND_TEST(AuthSessionTestWithKeysetManagement,
              USSRollbackAuthWithBackupVKSuccess);
  FRIEND_TEST(AuthSessionWithUssExperimentTest, AddPasswordAuthFactorViaUss);
  FRIEND_TEST(AuthSessionWithUssExperimentTest,
              AddPasswordAuthFactorViaAsyncUss);
  FRIEND_TEST(AuthSessionWithUssExperimentTest, PrepareLegacyFingerprintAuth);
  FRIEND_TEST(AuthSessionWithUssExperimentTest, RemoveAuthFactor);
  FRIEND_TEST(UserDataAuthExTest, MountUnauthenticatedAuthSession);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
  FRIEND_TEST(UserDataAuthExTest, ExtendAuthSession);
  FRIEND_TEST(UserDataAuthExTest, CheckTimeoutTimerSetAfterAuthentication);
  FRIEND_TEST(UserDataAuthExTest,
              StartMigrateToDircryptoWithAuthenticatedAuthSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_H_
