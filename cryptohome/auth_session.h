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
#include <vector>

#include <base/containers/flat_set.h>
#include <base/functional/callback.h>
#include <base/functional/callback_forward.h>
#include <base/memory/weak_ptr.h>
#include <base/timer/wall_clock_timer.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/structures/explicit_init.h>
#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/manager.h"
#include "cryptohome/auth_factor/map.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/user_policy.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/recoverable_key_store/backend_cert_provider.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/username.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {

using AuthFactorStatusUpdateCallback = base::RepeatingCallback<void(
    user_data_auth::AuthFactorWithStatus, const std::string&)>;

// This class starts a session for the user to authenticate with their
// credentials.
class AuthSession final {
 private:
  // Base class for all the AuthForXxx classes. Provides the standard
  // boilerplate that every one of those classes supports:
  //   * They all provide kIntent static member of their AuthIntent
  //   * Their constructor takes a Passkey parameter that only AuthSession
  //     can construct, so that only AuthSession can create these
  //   * They can't be copied or moved.
  //
  // Each AuthIntent should have a corresponding subclass of this base that
  // defines any AuthSession functions which REQUIRE the session to be
  // authorized for this intent. The goal of this is to make it very difficult
  // to accidentally write a bug where a function that requires authorization
  // fails to check for it, by putting the function into a subobject that only
  // gets created when the authorization happens.
  template <AuthIntent intent>
  class AuthForBase {
   public:
    class Passkey {
     private:
      friend class AuthSession;
      Passkey() = default;
    };

    static constexpr AuthIntent kIntent = intent;

    // Construct an AuthFor object for the given session. The passkey parameter
    // is used just to restrict construction to classes and functions that can
    // create the Passkey object (i.e. AuthSession).
    AuthForBase(AuthSession& session, Passkey) : session_(&session) {}

    AuthForBase(const AuthForBase&) = delete;
    AuthForBase& operator=(const AuthForBase&) = delete;

   protected:
    AuthSession* session_;
  };

 public:
  // Parameter struct used to specify all the base parameters of AuthSession.
  // These parameters do not include the underlying interfaces that AuthSession
  // depends on, which are defined below in a separate parameter struct.
  struct Params {
    hwsec::ExplicitInit<Username> username;
    hwsec::ExplicitInit<bool> is_ephemeral_user;
    hwsec::ExplicitInit<AuthIntent> intent;
    std::unique_ptr<base::WallClockTimer> auth_factor_status_update_timer;
    hwsec::ExplicitInit<bool> user_exists;
    AuthFactorMap auth_factor_map;
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
    AuthFactorDriverManager* auth_factor_driver_manager = nullptr;
    AuthFactorManager* auth_factor_manager = nullptr;
    UssStorage* user_secret_stash_storage = nullptr;
    AsyncInitFeatures* features = nullptr;
    AsyncInitPtr<RecoverableKeyStoreBackendCertProvider>
        key_store_cert_provider{nullptr};
  };

  // Creates new auth session for account_id. This method returns a unique_ptr
  // to the created AuthSession for the auth_session_manager to hold.
  static std::unique_ptr<AuthSession> Create(Username username,
                                             unsigned int flags,
                                             AuthIntent intent,
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

  // Returns the token which is used as the secondary identifier for the
  // session. This is not used to identify the session internally in cryptohome
  // but it is used in external APIs where the session needs to be identified
  // without providing the ability to act on the session.
  const base::UnguessableToken& public_token() const { return public_token_; }
  const std::string& serialized_public_token() const {
    return serialized_public_token_;
  }

  // Returns the intents that the AuthSession has been authorized for.
  base::flat_set<AuthIntent> authorized_intents() const;

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

  // Indicates if the session has a User Secret Stash enabled.
  bool has_user_secret_stash() const { return decrypted_uss_.has_value(); }

  // Indicates if the session has migration to User Secret Stash enabled.
  bool has_migrate_to_user_secret_stash() const {
    return has_user_secret_stash();
  }

  // Indicates if there is a reset_secret in session's User Secret Stash for
  // the given label.
  inline bool HasResetSecretInUssForTesting(const std::string& label) const {
    return decrypted_uss_ && decrypted_uss_->GetResetSecret(label);
  }

  // OnUserCreated is called when the user and their homedir are newly created.
  // Must be called no more than once.
  CryptohomeStatus OnUserCreated();

  // Whether the AuthenticateAuthFactor request should be forced to perform full
  // auth.
  enum class ForceFullAuthFlag : bool {
    kNone = false,
    kForce = true,
  };

  // Flags to adjust behavior of the AuthenticateAuthFactor request.
  struct AuthenticateAuthFactorFlags {
    ForceFullAuthFlag force_full_auth;
  };

  // Packs necessary input parameters of AuthenticateAuthFactor request into a
  // struct.
  struct AuthenticateAuthFactorRequest {
    std::vector<std::string> auth_factor_labels;
    user_data_auth::AuthInput auth_input_proto;
    AuthenticateAuthFactorFlags flags;
  };

  // Action that needs to be performed after an AuthenticateAuthFactor request
  // is completed.
  enum class PostAuthActionType {
    kNone,
    // Repeat the request with |repeat_request|. This will be used to reset the
    // credential lockouts.
    kRepeat,
    // Terminate and prepare the auth factor again. This is used for the factors
    // where an authentication attempts actually consumes the prepare token. To
    // hide this implementation detail from Chrome, we need to implement it as a
    // post-action.
    kReprepare,
  };
  struct PostAuthAction {
    PostAuthActionType action_type;
    std::optional<AuthenticateAuthFactorRequest> repeat_request;
    std::optional<user_data_auth::PrepareAuthFactorRequest> reprepare_request;
  };

  using AuthenticateAuthFactorCallback =
      base::OnceCallback<void(const PostAuthAction&, CryptohomeStatus)>;

  // Authenticate is called when the user wants to authenticate the current
  // AuthSession via an auth factor. It may be called multiple times depending
  // on errors or various steps involved in multi-factor authentication.
  // Note: only USS users are supported currently.
  void AuthenticateAuthFactor(
      const AuthenticateAuthFactorRequest& request,
      const SerializedUserAuthFactorTypePolicy& user_policy,
      AuthenticateAuthFactorCallback callback);

  // PrepareUserForRemoval is called to perform the necessary steps before
  // removing a user, like preparing each auth factor for removal.
  void PrepareUserForRemoval(base::OnceClosure on_finish);

  // UpdateAuthFactorMetadata updates the auth factor without new credentials.
  void UpdateAuthFactorMetadata(
      const user_data_auth::UpdateAuthFactorMetadataRequest request,
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

  // Subobjects that implement operations that require the session to be
  // authforized for a specific intent. While these are technically classes they
  // are not really intended to be used as independent types. Instead it is
  // best to think of them as sets of additional AuthSession functions with a
  // specific precondition, "must be authorized for intent X".
  //
  // For consistency we define a class (AuthForXxx) and function (GetAuthForXxx)
  // for each intent, although some of them are just empty stubs because the
  // authorization opens up no additional operations. The GetAuthForXxx are
  // similar to an "is authorized for Xxx" check except that instead of
  // returning a bool the return either null (not authorized) or not null
  // (authorized). If not null the returned objects have the same lifetime as
  // the AuthSession object itself.

  // AuthForDecrypt: operations that require AuthIntent::kDecrypt.
  class AuthForDecrypt : public AuthForBase<AuthIntent::kDecrypt> {
   public:
    using AuthForBase::AuthForBase;

    // Add a new auth factor from the given request.
    void AddAuthFactor(const user_data_auth::AddAuthFactorRequest& request,
                       StatusCallback on_done);

    // Update an existing auth factor from the given request.
    void UpdateAuthFactor(
        const user_data_auth::UpdateAuthFactorRequest& request,
        StatusCallback on_done);

    // Relabel an auth factor as specified by the given request.
    void RelabelAuthFactor(
        const user_data_auth::RelabelAuthFactorRequest& request,
        StatusCallback on_done);

    // Replace an auth factor as specified by the given request.
    void ReplaceAuthFactor(
        const user_data_auth::ReplaceAuthFactorRequest& request,
        StatusCallback on_done);

    // Remove an auth factor specified by the given request.
    void RemoveAuthFactor(
        const user_data_auth::RemoveAuthFactorRequest& request,
        StatusCallback on_done);

    // Prepares an auth factor type for the add purpose.
    void PrepareAuthFactorForAdd(AuthFactorType auth_factor_type,
                                 StatusCallback on_done);

   private:
    // Special case for relabel of an ephemeral user's factor.
    void RelabelAuthFactorEphemeral(
        const user_data_auth::RelabelAuthFactorRequest& request,
        StatusCallback on_done);

    // Special case for replace of an ephemeral user's factor.
    void ReplaceAuthFactorEphemeral(
        const user_data_auth::ReplaceAuthFactorRequest& request,
        StatusCallback on_done);

    // After successful creation of a replacement factor, swap it into USS as a
    // replacement for the old factor. Designed to be bindable into an
    // AuthBlock::CreateCallback.
    void ReplaceAuthFactorIntoUss(
        AuthFactor original_auth_factor,
        AuthInput auth_input,
        AuthFactorType auth_factor_type,
        std::string auth_factor_label,
        AuthFactorMetadata auth_factor_metadata,
        std::unique_ptr<AuthSessionPerformanceTimer> perf_timer,
        StatusCallback on_done,
        CryptohomeStatus error,
        std::unique_ptr<KeyBlobs> key_blobs,
        std::unique_ptr<AuthBlockState> auth_block_state);

    // Creates AuthInput for preparing an auth factor type for adding.
    // As in this case the auth factor hasn't been decided yet, the AuthInput
    // will typically be simpler than Add and Authenticate cases, and derivable
    // from solely the |auth_factor_type|.
    CryptohomeStatusOr<AuthInput> CreateAuthInputForPrepareForAdd(
        AuthFactorType auth_factor_type);

    // The last member, to invalidate weak references first on destruction.
    base::WeakPtrFactory<AuthForDecrypt> weak_factory_{this};
  };
  friend class AuthForDecrypt;
  AuthForDecrypt* GetAuthForDecrypt();

  // AuthForXxx classes for intents which don't add any new operations. These
  // classes are just simple stubs of AuthForBase that don't really do anything
  // other than acting as indicators that the session is authorized for a
  // specific intent, but we still define them this way for consistency. This
  // allows most of the auth intent handling code to be written generically.
  class AuthForVerifyOnly : public AuthForBase<AuthIntent::kVerifyOnly> {
   public:
    using AuthForBase::AuthForBase;
  };
  friend class AuthForVerifyOnly;
  AuthForVerifyOnly* GetAuthForVerifyOnly();

  class AuthForWebAuthn : public AuthForBase<AuthIntent::kWebAuthn> {
   public:
    using AuthForBase::AuthForBase;
  };
  friend class AuthForWebAuthn;
  AuthForWebAuthn* GetAuthForWebAuthn();

  // Generates a payload that will be sent to the server for cryptohome recovery
  // AuthFactor authentication. GetRecoveryRequest saves data in the
  // AuthSession state. This call is required before the AuthenticateAuthFactor
  // call for cryptohome recovery AuthFactor.
  void GetRecoveryRequest(
      user_data_auth::GetRecoveryRequestRequest request,
      base::OnceCallback<void(const user_data_auth::GetRecoveryRequestReply&)>
          on_done);

  // OnMigrationUssCreated is the callback function to be called after
  // migration secret is generated and added to UserSecretStash during the
  // AuthenticateViaVaultKeysetAndMigrateToUss() operation.
  void OnMigrationUssCreated(AuthBlockType auth_block_type,
                             AuthFactorType auth_factor_type,
                             const AuthFactorMetadata& auth_factor_metadata,
                             const AuthInput& auth_input,
                             CryptohomeStatus pre_migration_status,
                             StatusCallback on_done,
                             std::optional<DecryptedUss> loaded_uss);

  // OnMigrationUssCreatedForUpdate is the callback function to be called after
  // migration secret is generated and added to UserSecretStash during the
  // MigrateToUssDuringUpdateVaultKeyset() operation.
  void OnMigrationUssCreatedForUpdate(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const AuthInput& auth_input,
      StatusCallback on_done,
      CryptohomeStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_state,
      std::optional<DecryptedUss> loaded_uss);

  // Sets |vault_keyset_| for testing purpose.
  void set_vault_keyset_for_testing(std::unique_ptr<VaultKeyset> value) {
    vault_keyset_ = std::move(value);
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

  // Get the hibernate secret, derived from the file system keyset.
  std::unique_ptr<brillo::SecureBlob> GetHibernateSecret();

  // Add a callback to call when the AuthSession is authenticated. This callback
  // is triggered immediately if the session is already authenticated.
  void AddOnAuthCallback(base::OnceClosure on_auth);

  // Send the auth factor status update signal and also set the timer for the
  // next signal based on |kAuthFactorStatusUpdateDelay|.
  void SendAuthFactorStatusUpdateSignal();

  // Set the auth factor status update callback.
  void SetAuthFactorStatusUpdateCallback(
      const AuthFactorStatusUpdateCallback& callback);

  // Adds AuthFactor to |auth_factor_map_|, registered as a VaultKeyset backed
  // factor. This functionality is only used by the cryptohome-test-tool and
  // through UserDataAuth::CreateVaultKeyset() for testing purposes.
  void RegisterVaultKeysetAuthFactor(AuthFactor auth_factor);

  // Cancels the execution of any outstanding callbacks bound to this session.
  // The can be used to "time out" an outstanding async operation that the
  // session is executing.
  void CancelAllOutstandingAsyncCallbacks();

 private:
  // Emits a debug log message with this Auth Session's initial state.
  void RecordAuthSessionStart() const;

  // Switches the state to authorize the specified intents. Starts or restarts
  // the timer when applicable.
  void SetAuthorizedForIntents(
      base::flat_set<AuthIntent> new_authorized_intents);

  // Sets the auth session as fully authenticated by the given factor type. What
  // specific intents the session is authorized for depends on the factor type.
  void SetAuthorizedForFullAuthIntents(
      AuthFactorType auth_factor_type,
      const SerializedUserAuthFactorTypePolicy& user_policy);

  // Converts the D-Bus AuthInput proto into the C++ struct. Returns nullopt on
  // failure.
  CryptohomeStatusOr<AuthInput> CreateAuthInputForAuthentication(
      const user_data_auth::AuthInput& auth_input_proto,
      const AuthFactorMetadata& auth_factor_metadata);
  // Same as above, but additionally sets extra fields for resettable factors.
  // Can also be supplied with an already constructed AuthInput instead of the
  // raw proto, for cases where you need to construct both and don't want to
  // redo the same work.
  CryptohomeStatusOr<AuthInput> CreateAuthInputForAdding(
      const user_data_auth::AuthInput& auth_input_proto,
      AuthFactorType auth_factor_type,
      const AuthFactorMetadata& auth_factor_metadata);
  CryptohomeStatusOr<AuthInput> CreateAuthInputForAdding(
      AuthInput auth_input,
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

  // Creates AuthInput for selecting the correct auth factor to be used for
  // authentication. As in this case the auth factor hasn't been decided yet,
  // the AuthInput will typically be simpler than Add and Authenticate cases,
  // and derivable from solely the |auth_factor_type|.
  CryptohomeStatusOr<AuthInput> CreateAuthInputForSelectFactor(
      AuthFactorType auth_factor_type);

  // Creates AuthInput for preparing an auth factor type for authentication.
  // As in this case the auth factor hasn't been decided yet, the AuthInput will
  // typically be simpler than Add and Authenticate cases, and derivable from
  // solely the |auth_factor_type|.
  CryptohomeStatusOr<AuthInput> CreateAuthInputForPrepareForAuth(
      AuthFactorType auth_factor_type);

  // This function attempts to add verifier for the given label based on the
  // AuthInput. If it succeeds it will return a pointer to the verifier.
  // Otherwise it will return null.
  CredentialVerifier* AddCredentialVerifier(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthInput& auth_input,
      const AuthFactorMetadata& auth_factor_metadata);

  // Helper function to update a keyset on disk on KeyBlobs generated. If update
  // succeeds |vault_keyset_| is also updated. Failure doesn't return error and
  // doesn't block authentication operations.
  void ResaveKeysetOnKeyBlobsGenerated(
      VaultKeyset updated_vault_keyset,
      CryptohomeStatus error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // Updates the secret of an AuthFactor backed by a VaultKeyset and migrates it
  // to UserSecretStash. Failures during this operation fails to both update and
  // migrate the factor.
  void MigrateToUssDuringUpdateVaultKeyset(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const KeyData& key_data,
      const AuthInput& auth_input,
      StatusCallback on_done,
      CryptohomeStatus callback_error,
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
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done,
      CryptohomeStatus callback_error,
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
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done,
      CryptohomeStatus pre_migration_status,
      CryptohomeStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // The implementation function to persists an AuthFactor and a USS key
  // block for a new secret.
  CryptohomeStatus PersistAuthFactorToUserSecretStashImpl(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const AuthInput& auth_input,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      CryptohomeStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // Migrates the reset secret for a PIN factor from VaultKeysets to
  // UserSecretStash by calculating it from |reset_seed| of the authenticated
  // VaultKeyset and the |reset_salt| of the Pinweaver VaultKeysets and adding
  // to USS in memory.
  bool MigrateResetSecretToUss();

  // Process the completion of a verify-only authentication attempt. The
  // |on_done| callback will be called after the results of the verification are
  // processed. This takes |auth_factor_type| parameter because it needs to
  // determine whether the factor can be used for resetting credentials.
  // Designed to be used in conjunction with CredentialVerifier::Verify as the
  // CryptohomeStatusCallback.
  void CompleteVerifyOnlyAuthentication(
      AuthenticateAuthFactorCallback on_done,
      AuthenticateAuthFactorRequest original_request,
      AuthFactorType auth_factor_type,
      CryptohomeStatus error);

  // Add the given auth factor to an ongoing transaction.
  CryptohomeStatus AddAuthFactorToUssTransaction(
      AuthFactor& auth_factor,
      const KeyBlobs& key_blobs,
      DecryptedUss::Transaction& transaction);

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
                                 const AuthFactorMetadata& auth_factor_metadata,
                                 StatusCallback on_done);

  // Loads and decrypts the USS payload with |auth_factor| using the
  // given KeyBlobs. Designed to be used in conjunction with an async
  // DeriveKeyBlobs call by binding all of the initial parameters to make an
  // AuthBlock::DeriveCallback.
  void LoadUSSMainKeyAndFsKeyset(
      const AuthFactor& auth_factor,
      const AuthInput& auth_input,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      const SerializedUserAuthFactorTypePolicy& user_policy,
      StatusCallback on_done,
      CryptohomeStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::optional<AuthBlock::SuggestedAction> suggested_action);

  // This function is used after a load to re-create an auth factor. It is
  // generally used after a post-authenticate load, when the key derivation
  // suggests that the factor needs to be recreated.
  void RecreateUssAuthFactor(AuthFactorType auth_factor_type,
                             const std::string& auth_factor_label,
                             AuthInput auth_input,
                             std::unique_ptr<AuthSessionPerformanceTimer>
                                 auth_session_performance_timer,
                             CryptohomeStatus original_status,
                             StatusCallback on_done);

  // This function is used to reset the attempt count for a low entropy
  // credential. Currently, this resets all low entropy credentials both stored
  // in USS and VK. In the USSv2 world, with passwords or even other auth types
  // backed by PinWeaver, the code will need to reset specific LE credentials.
  void ResetLECredentials();

  // This function is used to reset the attempt count and expiration (depending
  // on |capability|) for rate-limiters. Normally credentials guarded by
  // rate-limiters will never be locked, but we still check them to see if
  // they're accidentally locked. In that case, the reset secret is the same as
  // the rate-limiter's.
  void ResetRateLimiterCredentials(
      AuthFactorDriver::ResetCapability capability);

  // Whether there are some credentials that can be reset after a full auth.
  bool NeedsFullAuthForReset(AuthFactorDriver::ResetCapability capability);

  // Authenticate the user with the single given auth factor. Additional
  // parameters are provided to aid legacy vault keyset authentication and
  // migration.
  void AuthenticateViaSingleFactor(
      const AuthFactorType& request_auth_factor_type,
      const std::string& auth_factor_label,
      const AuthInput& auth_input,
      const AuthFactorMetadata& metadata,
      const AuthFactorMap::ValueView& stored_auth_factor,
      const SerializedUserAuthFactorTypePolicy& user_policy,
      StatusCallback on_done);

  // Authenticates the user using USS with the |auth_factor_label|, |auth_input|
  // and the |auth_factor|.
  void AuthenticateViaUserSecretStash(
      const std::string& auth_factor_label,
      const AuthInput auth_input,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      const AuthFactor& auth_factor,
      const SerializedUserAuthFactorTypePolicy& user_policy,
      StatusCallback on_done);

  // Authenticates the user using VaultKeysets with the given |auth_input|.
  void AuthenticateViaVaultKeysetAndMigrateToUss(
      AuthFactorType request_auth_factor_type,
      const std::string& key_label,
      const AuthInput& auth_input,
      const AuthFactorMetadata& metadata,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      const SerializedUserAuthFactorTypePolicy& user_policy,
      StatusCallback on_done);

  // Authenticates the user using the selected |auth_factor|. Used when the
  // auth factor type takes multiple labels during authentication, and used as
  // the callback for AuthBlockUtility::SelectAuthFactorWithAuthBlock.
  void AuthenticateViaSelectedAuthFactor(
      const SerializedUserAuthFactorTypePolicy& user_policy,
      StatusCallback on_done,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      CryptohomeStatus callback_error,
      std::optional<AuthInput> auth_input,
      std::optional<AuthFactor> auth_factor);

  // Fetches a valid VaultKeyset for |obfuscated_username_| that matches the
  // label provided by key_data_.label(). The VaultKeyset is loaded and
  // initialized into |vault_keyset_| through
  // KeysetManagement::GetValidKeyset(). This function is needed for
  // processing callback results in an asynchronous manner through the |on_done|
  // callback.
  void LoadVaultKeysetAndFsKeys(
      AuthFactorType request_auth_factor_type,
      const AuthInput& auth_input,
      AuthBlockType auth_block_type,
      const AuthFactorMetadata& metadata,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      const SerializedUserAuthFactorTypePolicy& user_policy,
      StatusCallback on_done,
      CryptohomeStatus error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::optional<AuthBlock::SuggestedAction> suggested_action);

  // Updates, wraps and resaves |vault_keyset_| and restores on failure.
  // |user_input| is needed to generate the AuthInput used for key blob creation
  // to wrap the updated keyset.
  AuthBlockType ResaveVaultKeysetIfNeeded(
      const std::optional<brillo::SecureBlob> user_input,
      AuthBlockType auth_block_type);

  // Removes the key block with the provided `auth_factor_label` from the USS
  // and removes the `auth_factor` from disk.
  void RemoveAuthFactorViaUserSecretStash(const std::string& auth_factor_label,
                                          const AuthFactor& auth_factor,
                                          StatusCallback on_done);

  // Removes the auth factor from the in-memory map, the existing keyset and
  // verifier indexed by the `auth_factor_label`.
  void ClearAuthFactorInMemoryObjects(
      const std::string& auth_factor_label,
      const AuthFactorMap::ValueView& stored_auth_factor,
      const base::TimeTicks& remove_timer_start,
      StatusCallback on_done,
      CryptohomeStatus status);

  // Re-persists the USS and removes in memory objects related to the
  // removed auth factor.
  void ResaveUssWithFactorRemoved(const std::string& auth_factor_label,
                                  const AuthFactor& auth_factor,
                                  StatusCallback on_done,
                                  CryptohomeStatus status);

  // Creates a new per-credential secret, updates the secret in the USS and
  // updates the auth block state on disk.
  void UpdateAuthFactorViaUserSecretStash(
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label,
      const AuthFactorMetadata& auth_factor_metadata,
      const AuthInput& auth_input,
      std::unique_ptr<AuthSessionPerformanceTimer>
          auth_session_performance_timer,
      StatusCallback on_done,
      CryptohomeStatus callback_error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state);

  // Persists the updated USS and
  // re-creates the related credential verifier if applicable.
  void ResaveUssWithFactorUpdated(AuthFactorType auth_factor_type,
                                  AuthFactor auth_factor,
                                  std::unique_ptr<KeyBlobs> key_blobs,
                                  const AuthInput& auth_input,
                                  std::unique_ptr<AuthSessionPerformanceTimer>
                                      auth_session_performance_timer,
                                  StatusCallback on_done,
                                  CryptohomeStatus status);

  // Handles the completion of an async PrepareAuthFactor call. Captures the
  // token and forwards the result to the given status callback.
  void OnPrepareAuthFactorDone(
      StatusCallback on_done,
      CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>> token);

  // Prepares the WebAuthn secret using file_system_keyset.
  CryptohomeStatus PrepareWebAuthnSecret();

  // Prepares the chaps key using file_system_keyset.
  CryptohomeStatus PrepareChapsKey();

  // RemoveRateLimiters remove all rate-limiter leaves in the UserMetadata. This
  // doesn't leave the USS with a consistent state, and should only be called
  // during PrepareUserForRemoval.
  void RemoveRateLimiters();

  // Check whether the recoverable key store state of |auth_factor| is outdated,
  // and update it if so.
  void MaybeUpdateRecoverableKeyStore(const AuthFactor& auth_factor,
                                      AuthInput auth_input);

  Username username_;
  ObfuscatedUsername obfuscated_username_;

  // Is the user of this session ephemeral?
  const bool is_ephemeral_user_;
  // The intent this session was started for.
  const AuthIntent auth_intent_;

  // AuthFor objects representing which intents the session is authorized for.
  // These are null when the session is not authorized.
  //
  // Once a session is authorized for a specific intent that
  // authorization cannot be cleared as clients may have references to the
  // AuthFor object. In other words, intents cannot be "deauthorized".
  std::optional<AuthForDecrypt> auth_for_decrypt_;
  std::optional<AuthForVerifyOnly> auth_for_verify_only_;
  std::optional<AuthForWebAuthn> auth_for_web_authn_;

  // The wall clock timer object to send AuthFactor status update periodically.
  std::unique_ptr<base::WallClockTimer> auth_factor_status_update_timer_;

  base::TimeTicks auth_session_creation_time_;
  base::TimeTicks authenticated_time_;

  // Callbacks to be triggered when authentication occurs.
  std::vector<base::OnceClosure> on_auth_;
  // The repeating callback to send AuthFactorStatusUpdateSignal.
  AuthFactorStatusUpdateCallback auth_factor_status_update_callback_;

  // The decrypted USS. Only populated if the session is able to decrypt the USS
  // which generally requires it to have been decrypt-authorized.
  std::optional<DecryptedUss> decrypted_uss_;
  UserUssStorage uss_storage_;

  Crypto* const crypto_;
  Platform* const platform_;
  // The user session map and a verifier forwarder associated with it.
  UserSessionMap* const user_session_map_;
  UserSessionMap::VerifierForwarder verifier_forwarder_;
  // TODO(crbug.com/1171024): Change KeysetManagement to use AuthBlock.
  KeysetManagement* const keyset_management_;
  AuthBlockUtility* const auth_block_utility_;
  AuthFactorDriverManager* const auth_factor_driver_manager_;
  AuthFactorManager* const auth_factor_manager_;
  // Unowned pointer.
  AsyncInitFeatures* const features_;
  AsyncInitPtr<RecoverableKeyStoreBackendCertProvider> key_store_cert_provider_;
  // A stateless object to convert AuthFactor API to VaultKeyset KeyData and
  // VaultKeysets to AuthFactor API.
  AuthFactorVaultKeysetConverter converter_;

  // Tokens (and their serialized forms) used to identify the session.
  const base::UnguessableToken token_;
  const std::string serialized_token_;
  const base::UnguessableToken public_token_;
  const std::string serialized_public_token_;

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
  std::optional<brillo::Blob> cryptohome_recovery_ephemeral_pub_key_;
  // Tokens from active auth factors, keyed off of the token's auth factor type.
  std::map<AuthFactorType, std::unique_ptr<PreparedAuthFactorToken>>
      active_auth_factor_tokens_;

  // Weak factory for creating weak pointers to |this| for timed tasks.
  base::WeakPtrFactory<AuthSession> weak_factory_for_timed_tasks_{this};
  // Weak factory for creating "normal" weak pointers to |this|. Tasks bound to
  // these pointers will be cancelled by CancelAllOutstandingAsyncCallbacks.
  base::WeakPtrFactory<AuthSession> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_H_
