// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USERDATAAUTH_H_
#define CRYPTOHOME_USERDATAAUTH_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <absl/container/node_hash_map.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/location.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/platform_thread.h>
#include <base/threading/thread.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>
#include <libhwsec-foundation/status/status_chain_or.h>
#include <libhwsec/factory/factory.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/cryptorecovery/service.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_factor/manager.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_session/auth_session.h"
#include "cryptohome/auth_session/manager.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/cleanup/low_disk_space_handler.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/create_vault_keyset_rpc_impl.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/device_management_client_proxy.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/features.h"
#include "cryptohome/fingerprint_manager.h"
#include "cryptohome/flatbuffer_schemas/user_policy.h"
#include "cryptohome/fp_migration/utility.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/pkcs11_token_factory.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/recoverable_key_store/backend_cert_provider.h"
#include "cryptohome/signalling.h"
#include "cryptohome/storage/cryptohome_vault_factory.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_factory.h"
#include "cryptohome/user_policy_file.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_session/user_session.h"
#include "cryptohome/user_session/user_session_factory.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/username.h"
#include "cryptohome/vault_keyset_factory.h"

namespace cryptohome {

class UserDataAuth {
 public:
  struct MountArgs {
    // Whether to create the vault if it is missing.
    bool create_if_missing = false;
    // Whether the mount has to be ephemeral.
    bool is_ephemeral = false;
    // When creating a new cryptohome from scratch, use ecryptfs.
    bool create_as_ecryptfs = false;
    // Forces dircrypto, i.e., makes it an error to mount ecryptfs.
    bool force_dircrypto = false;
    // Enables version 2 fscrypt interface.
    bool enable_dircrypto_v2 = false;
    // Mount the existing ecryptfs vault to a temporary location while setting
    // up a new dircrypto directory.
    bool to_migrate_from_ecryptfs = false;
  };

  // Standard alias for the on_done callback for requests.
  template <typename ReplyType>
  using OnDoneCallback = base::OnceCallback<void(const ReplyType&)>;
  // Standard alias for a callback that handles a request with an
  // InUseAuthSession. The internal helper functions of the form XxxWithSession
  // all generally use this signature and are designed to be used to construct a
  // RunWhenAvailable callback for AuthSessionManager.
  template <typename RequestType, typename ReplyType>
  using HandlerWithSessionCallback = base::OnceCallback<void(
      RequestType, OnDoneCallback<ReplyType>, InUseAuthSession)>;

  // Parameter struct used to supply all of the backing APIs that AuthSession
  // depends on. All of these pointers must be valid for the entire lifetime of
  // the UserDataAuth object and are all required to be non null.
  struct BackingApis {
    libstorage::Platform* platform;
    const hwsec::CryptohomeFrontend* hwsec;
    const hwsec::PinWeaverManagerFrontend* hwsec_pw_manager;
    const hwsec::RecoveryCryptoFrontend* recovery_crypto;
    CryptohomeKeysManager* cryptohome_keys_manager;
    Crypto* crypto;
    CryptohomeRecoveryAuthBlockService* recovery_ab_service;
    DeviceManagementClientProxy* device_management_client;
    UserOldestActivityTimestampManager* user_activity_timestamp_manager;
    KeysetManagement* keyset_management;
    UssStorage* uss_storage;
    UssManager* uss_manager;
    AuthFactorManager* auth_factor_manager;
  };

  explicit UserDataAuth(BackingApis apis);
  ~UserDataAuth();

  // Note that this function must be called from the thread that created this
  // object, so that |origin_task_runner_| is initialized correctly.
  //
  // Initialization can optionally specify the mount thread but but this
  // normally only done in testing. If |mount_thread_bus| is null then the
  // initialization will create one itself which is how it is usually done in
  // non-test usage.
  bool Initialize(scoped_refptr<::dbus::Bus> mount_thread_bus);

  // =============== Mount Related Public DBus API ===============
  // Methods below are used directly by the DBus interface

  // If username is empty, returns true if any mount is mounted, otherwise,
  // returns true if the mount associated with the given |username| is mounted.
  // For |is_ephemeral_out|, if no username is given, then is_ephemeral_out is
  // set to true when any mount is ephemeral. Otherwise, is_ephemeral_out is set
  // to true when the mount associated with the given |username| is mounted in
  // an ephemeral manner. If nullptr is passed in for is_ephemeral_out, then it
  // won't be touched. Ephemeral mount means that the content of the mount is
  // cleared once the user logs out.
  bool IsMounted(const Username& username = Username(),
                 bool* is_ephemeral_out = nullptr);

  // If the user mount does not exist or is ephemeral, function
  // will return INVALID_ARGUMENT error. When the mount exists, the function
  // will return the appropriate encryption type.
  user_data_auth::GetVaultPropertiesReply GetVaultProperties(
      user_data_auth::GetVaultPropertiesRequest request);

  // Calling this function will unmount all mounted cryptohomes. It'll return
  // a reply without error if all mounts are cleanly unmounted.
  // Note: This must only be called on mount thread
  user_data_auth::UnmountReply Unmount();

  // Calling this method will kick start the migration to Dircrypto format (from
  // eCryptfs). |request| contains the account whose cryptohome to migrate, and
  // what whether minimal migration is to be performed. See definition of
  // message StartMigrateToDircryptoRequest for more information on minimal
  // migration. |on_done| is a standard request completion callback that is
  // called when starting the migration is completed (NOT when the migration is
  // completed). |progress_callback| is a callback that will be called whenever
  // there's progress update from the migration, or if the migration
  // completes/fails.
  void StartMigrateToDircrypto(
      user_data_auth::StartMigrateToDircryptoRequest request,
      OnDoneCallback<user_data_auth::StartMigrateToDircryptoReply> on_done,
      Mount::MigrationCallback progress_callback);

  // Determine if the account specified by |account| needs to do Dircrypto
  // migration. Returns CRYPTOHOME_ERROR_NOT_SET if the query is successful, and
  // the result is stored in |result| (true for migration needed). Otherwise, an
  // error code is returned and result is in an undefined state.
  user_data_auth::CryptohomeErrorCode NeedsDircryptoMigration(
      const AccountIdentifier& account, bool* result);

  // Return the size of the user's home directory in number of bytes. If the
  // |account| given is non-existent, then 0 is returned.
  // Negative values are reserved for future cases whereby we need to do some
  // form of error reporting.
  int64_t GetAccountDiskUsage(const AccountIdentifier& account);

  // =============== Mount Related Public Utilities ===============

  // Called during initialization (and on mount events) to ensure old mounts
  // are marked for unmount when possible by the kernel.  Returns true if any
  // mounts were stale and not cleaned up (because of open files).
  // Note: This must only be called on mount thread
  //
  // Parameters
  // - force: if true, unmounts all existing shadow mounts.
  //          if false, unmounts shadows mounts with no open files.
  bool CleanUpStaleMounts(bool force);

  // Ensures the cryptohome keys had been loaded.
  void EnsureCryptohomeKeys();

  // Set the |force_ecryptfs_| variable, if true, all mounts will use eCryptfs
  // for encryption. If eCryptfs is not used, then dircrypto (the ext4
  // directory encryption mechanism) is used. Note that this is usually used in
  // main() because there's a command line switch for selecting dircrypto or
  // eCryptfs.
  void set_force_ecryptfs(bool force_ecryptfs) {
    force_ecryptfs_ = force_ecryptfs;
  }

  // Enable version 2 of fscrypt interface.
  void set_fscrypt_v2(bool enable_v2) { fscrypt_v2_ = enable_v2; }

  // Enable creating LVM volumes for applications.
  void set_enable_application_containers(bool value) {
    enable_application_containers_ = value;
  }

  // Set the |legacy_mount_| variable. For more information on legacy_mount_,
  // see comment of Mount::MountLegacyHome(). Note that this is usually used in
  // main() because there's a command line switch for selecting this.
  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }

  // Set thresholds for automatic disk cleanup.
  void set_cleanup_threshold(uint64_t cleanup_threshold);
  void set_aggressive_cleanup_threshold(uint64_t aggressive_cleanup_threshold);
  void set_critical_cleanup_threshold(uint64_t critical_cleanup_threshold);
  void set_target_free_space(uint64_t target_free_space);

  // Set the D-Bus signalling interface.
  void SetSignallingInterface(SignallingInterface& signalling);

  // Remove the cryptohome (user's home directory) specified in
  // |request.identifier|. See definition of RemoveReply for what is returned.
  void Remove(user_data_auth::RemoveRequest request,
              OnDoneCallback<user_data_auth::RemoveReply> on_done);

  // Reset the application container specified in the request for the user
  // identified by authsession id.
  user_data_auth::ResetApplicationContainerReply ResetApplicationContainer(
      const user_data_auth::ResetApplicationContainerRequest& request);

  // Sets the user data directories (MyFiles, etc) to be writable (as default)
  // or read-only.
  user_data_auth::SetUserDataStorageWriteEnabledReply
  SetUserDataStorageWriteEnabled(
      const user_data_auth::SetUserDataStorageWriteEnabledRequest& request);

  // Return true if we support low entropy credential.
  bool IsLowEntropyCredentialSupported();

  // =============== PKCS#11 Related Public Methods ===============

  // Returns true if and only if PKCS#11 tokens are ready for all mounts.
  bool Pkcs11IsTpmTokenReady();

  // Return the information regarding a token. If username is empty, then system
  // token's information is given. Otherwise, the corresponding user token
  // information is given. Note that this function doesn't check if the given
  // username is valid or not. If a non-existent user is given, then the result
  // is undefined.
  // Note that if this method fails to get the slot associated with the token,
  // then -1 will be supplied for slot.
  user_data_auth::TpmTokenInfo Pkcs11GetTpmTokenInfo(const Username& username);

  // Calling this method will remove PKCS#11 tokens on all mounts.
  // Note that this should only be called from mount thread.
  void Pkcs11Terminate();

  // Calling this method will restore all the tokens to chaps.
  // Note that this should only be called from mount thread.
  void Pkcs11RestoreTpmTokens();

  // ============= WebAuthn / Passkey Related Public Methods ==============

  // TODO(b/184393647): This api is not currently used because secret
  // enforcement in the WebAuthn flow haven't been implemented yet. After
  // implemented, u2fd calls this api to retrieve the WebAuthn secret to use in
  // the sign command.
  user_data_auth::GetWebAuthnSecretReply GetWebAuthnSecret(
      const user_data_auth::GetWebAuthnSecretRequest& request);

  user_data_auth::GetWebAuthnSecretHashReply GetWebAuthnSecretHash(
      const user_data_auth::GetWebAuthnSecretHashRequest& request);

  // Get all the recoverable key stores of the user. These key stores, for
  // example, allow the user to recover their passkeys on other devices by
  // providing this device's knowledge factors.
  void GetRecoverableKeyStores(
      user_data_auth::GetRecoverableKeyStoresRequest request,
      base::OnceCallback<
          void(const user_data_auth::GetRecoverableKeyStoresReply&)> on_done);

  // =============== Miscellaneous Public APIs ===============

  // Retrieve the current system salt. This method call is always successful.
  // Note that this should never be called before Initialize() is successful,
  // otherwise an assertion will fail.
  const brillo::SecureBlob& GetSystemSalt();

  // Update the current user activity timestamp for all mounts. time_shift_sec
  // is the time, expressed in number of seconds since the last user activity.
  // For instance, if the unix timestamp now is x, if this value is 5, then the
  // last user activity happened at x-5 unix timestamp.
  // This method will return true if the update is successful for all mounts.
  // Note that negative |time_shift_sec| values are reserved and should not be
  // used.
  bool UpdateCurrentUserActivityTimestamp(int time_shift_sec);

  // Calling this method will prevent another user from logging in later by
  // extending PCR, causing PCR-bound VKKs to be inaccessible. This is used by
  // ARC++. |account_id| contains the user that we'll lock to before reboot.
  user_data_auth::CryptohomeErrorCode LockToSingleUserMountUntilReboot(
      const AccountIdentifier& account_id);

  // Retrieve the RSU Device ID, return true if and only if |rsu_device_id| is
  // set to the RSU Device ID.
  bool GetRsuDeviceId(std::string* rsu_device_id);

  // Return true iff powerwash is required. i.e. cannot unseal with user auth.
  bool RequiresPowerwash();

  // Returns true if and only if the loaded device policy specifies an owner
  // user.
  bool OwnerUserExists();

  // Returns whether ARC quota is supported.
  bool IsArcQuotaSupported();

  // Returns the device PinWeaver information.
  user_data_auth::GetPinWeaverInfoReply GetPinWeaverInfo();

  // =============== Miscellaneous ===============

  // This will be called after hwsec is ready.
  // Note: This can only be called on mount thread.
  void HwsecReadyCallback(hwsec::Status status);

  // ================= Threading Utilities ==================

  // Returns true if we are currently running on the origin thread
  bool IsOnOriginThread() const {
    // Note that this function should not solely rely on |origin_task_runner_|
    // because it may be unavailable when this function is first called by
    // UserDataAuth::Initialize()
    if (origin_task_runner_) {
      return origin_task_runner_->RunsTasksInCurrentSequence();
    }
    return base::PlatformThread::CurrentId() == origin_thread_id_;
  }

  // Returns true if we are currently running on the mount thread
  bool IsOnMountThread() const {
    if (mount_task_runner_) {
      return mount_task_runner_->RunsTasksInCurrentSequence();
    }
    // GetThreadId blocks if the thread is not started yet.
    return mount_thread_->IsRunning() &&
           base::PlatformThread::CurrentId() == mount_thread_->GetThreadId();
  }

  // DCHECK if we are running on the origin thread. Will have no effect
  // in production.
  void AssertOnOriginThread() const { DCHECK(IsOnOriginThread()); }

  // DCHECK if we are running on the mount thread. Will have no effect
  // in production.
  void AssertOnMountThread() const { DCHECK(IsOnMountThread()); }

  // Post Task to origin thread. For the caller, from_here is usually FROM_HERE
  // macro, while task is a callback function to be posted. Will return true if
  // the task may be run sometime in the future, false if it will definitely not
  // run. Specify |delay| if you want the task to be deferred for |delay| amount
  // of time.
  bool PostTaskToOriginThread(const base::Location& from_here,
                              base::OnceClosure task,
                              const base::TimeDelta& delay = base::TimeDelta());

  // Post Task to mount thread. For the caller, from_here is usually FROM_HERE
  // macro, while task is a callback function to be posted. Will return true if
  // the task may be run sometime in the future, false if it will definitely not
  // run. Specify |delay| if you want the task to be deferred for |delay| amount
  // of time.
  bool PostTaskToMountThread(const base::Location& from_here,
                             base::OnceClosure task,
                             const base::TimeDelta& delay = base::TimeDelta());

  // ================= Testing Utilities ==================
  // Note that all functions below in this section should only be used for unit
  // testing purpose only.

  // Override |keyset_management_| for testing purpose.
  void set_auth_block_utility(AuthBlockUtility* value) {
    auth_block_utility_ = value;
  }

  // Override |auth_factor_driver_manager_| for testing purpose.
  void set_auth_factor_driver_manager_for_testing(
      AuthFactorDriverManager* value) {
    auth_factor_driver_manager_ = value;
  }

  void set_user_session_map_for_testing(UserSessionMap* user_session_map) {
    sessions_ = user_session_map;
  }

  // Override |auth_session_manager_| for testing purpose.
  void set_auth_session_manager(AuthSessionManager* value) {
    auth_session_manager_ = value;
  }

  // Override |vault_factory_| for testing purpose.
  void set_vault_factory_for_testing(CryptohomeVaultFactory* vault_factory) {
    vault_factory_ = vault_factory;
  }

  // Override |homedirs_| for testing purpose.
  void set_homedirs(HomeDirs* homedirs) { homedirs_ = homedirs; }

  // override |chaps_client_| for testing purpose.
  void set_chaps_client(chaps::TokenManagerClient* chaps_client) {
    chaps_client_ = chaps_client;
  }

  // Override |device_management_client_| for testing purpose
  void set_device_management_client(
      DeviceManagementClientProxy* device_management_client) {
    device_management_client_ = device_management_client;
  }

  // Override |pkcs11_init_| for testing purpose.
  void set_pkcs11_init(Pkcs11Init* pkcs11_init) { pkcs11_init_ = pkcs11_init; }

  // Override |pkcs11_token_factory_| for testing purpose.
  void set_pkcs11_token_factory(Pkcs11TokenFactory* pkcs11_token_factory) {
    pkcs11_token_factory_ = pkcs11_token_factory;
  }

  // Override |fingerprint_manager_| for testing purpose.
  void set_fingerprint_manager(FingerprintManager* fingerprint_manager) {
    fingerprint_manager_ = fingerprint_manager;
  }

  // Override |biometrics_service_| for testing purpose.
  void set_biometrics_service(BiometricsAuthBlockService* biometrics_service) {
    biometrics_service_ = biometrics_service;
  }

  // Override |key_store_cert_provider_| for testing purpose.
  void set_key_store_cert_provider(
      RecoverableKeyStoreBackendCertProvider* key_store_cert_provider) {
    key_store_cert_provider_ = key_store_cert_provider;
  }

  // Override |mount_factory_| for testing purpose.
  void set_mount_factory_for_testing(MountFactory* mount_factory) {
    mount_factory_ = mount_factory;
  }

  // Override |user_session_factory_| for testing purpose.
  void set_user_session_factory(UserSessionFactory* user_session_factory) {
    user_session_factory_ = user_session_factory;
  }

  // Override |challenge_credentials_helper_| for testing purpose.
  void set_challenge_credentials_helper(
      ChallengeCredentialsHelper* challenge_credentials_helper) {
    challenge_credentials_helper_ = challenge_credentials_helper;
  }

  // Override |key_challenge_service_factory_| for testing purpose.
  void set_key_challenge_service_factory(
      KeyChallengeServiceFactory* key_challenge_service_factory) {
    key_challenge_service_factory_ = key_challenge_service_factory;
  }

  // Override |origin_task_runner_| for testing purpose.
  void set_origin_task_runner(
      scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner) {
    origin_task_runner_ = origin_task_runner;
  }

  // Override |mount_task_runner_| for testing purpose.
  void set_mount_task_runner(
      scoped_refptr<base::SingleThreadTaskRunner> mount_task_runner) {
    mount_task_runner_ = mount_task_runner;
  }

  // Override |low_disk_space_handler_| for testing purpose.
  void set_low_disk_space_handler(LowDiskSpaceHandler* low_disk_space_handler) {
    low_disk_space_handler_ = low_disk_space_handler;
  }

  void set_features(Features* features) { features_ = features; }

  // Retrieve the session associated with the given user, for testing purpose.
  // only.
  UserSession* FindUserSessionForTest(const Username& username) {
    return sessions_->Find(username);
  }

  // Associate a particular session object |session| with the username
  // |username| for testing purpose.
  bool AddUserSessionForTest(const Username& username,
                             std::unique_ptr<UserSession> session) {
    return sessions_->Add(username, std::move(session));
  }

  void StartAuthSession(
      user_data_auth::StartAuthSessionRequest request,
      OnDoneCallback<user_data_auth::StartAuthSessionReply> on_done);

  void InvalidateAuthSession(
      user_data_auth::InvalidateAuthSessionRequest request,
      OnDoneCallback<user_data_auth::InvalidateAuthSessionReply> on_done);

  void ExtendAuthSession(
      user_data_auth::ExtendAuthSessionRequest request,
      OnDoneCallback<user_data_auth::ExtendAuthSessionReply> on_done);

  void PrepareGuestVault(
      user_data_auth::PrepareGuestVaultRequest request,
      OnDoneCallback<user_data_auth::PrepareGuestVaultReply> on_done);

  void PrepareEphemeralVault(
      user_data_auth::PrepareEphemeralVaultRequest request,
      OnDoneCallback<user_data_auth::PrepareEphemeralVaultReply> on_done);

  void PreparePersistentVault(
      user_data_auth::PreparePersistentVaultRequest request,
      OnDoneCallback<user_data_auth::PreparePersistentVaultReply> on_done);

  void PrepareVaultForMigration(
      user_data_auth::PrepareVaultForMigrationRequest request,
      OnDoneCallback<user_data_auth::PrepareVaultForMigrationReply> on_done);

  void CreatePersistentUser(
      user_data_auth::CreatePersistentUserRequest request,
      OnDoneCallback<user_data_auth::CreatePersistentUserReply> on_done);

  void AddAuthFactor(
      user_data_auth::AddAuthFactorRequest request,
      OnDoneCallback<user_data_auth::AddAuthFactorReply> on_done);

  void AuthenticateAuthFactor(
      user_data_auth::AuthenticateAuthFactorRequest request,
      OnDoneCallback<user_data_auth::AuthenticateAuthFactorReply> on_done);

  void UpdateAuthFactor(
      user_data_auth::UpdateAuthFactorRequest request,
      OnDoneCallback<user_data_auth::UpdateAuthFactorReply> on_done);

  void UpdateAuthFactorMetadata(
      user_data_auth::UpdateAuthFactorMetadataRequest request,
      OnDoneCallback<user_data_auth::UpdateAuthFactorMetadataReply> on_done);

  void RelabelAuthFactor(
      user_data_auth::RelabelAuthFactorRequest request,
      OnDoneCallback<user_data_auth::RelabelAuthFactorReply> on_done);

  void ReplaceAuthFactor(
      user_data_auth::ReplaceAuthFactorRequest request,
      OnDoneCallback<user_data_auth::ReplaceAuthFactorReply> on_done);

  void RemoveAuthFactor(
      user_data_auth::RemoveAuthFactorRequest request,
      OnDoneCallback<user_data_auth::RemoveAuthFactorReply> on_done);

  void ListAuthFactors(
      user_data_auth::ListAuthFactorsRequest request,
      OnDoneCallback<user_data_auth::ListAuthFactorsReply> on_done);

  void ModifyAuthFactorIntents(
      user_data_auth::ModifyAuthFactorIntentsRequest request,
      OnDoneCallback<user_data_auth::ModifyAuthFactorIntentsReply> on_done);

  void GetAuthFactorExtendedInfo(
      user_data_auth::GetAuthFactorExtendedInfoRequest request,
      OnDoneCallback<user_data_auth::GetAuthFactorExtendedInfoReply> on_done);

  void GenerateFreshRecoveryId(
      user_data_auth::GenerateFreshRecoveryIdRequest request,
      OnDoneCallback<user_data_auth::GenerateFreshRecoveryIdReply> on_done);

  void PrepareAuthFactor(
      user_data_auth::PrepareAuthFactorRequest request,
      OnDoneCallback<user_data_auth::PrepareAuthFactorReply> on_done);

  void TerminateAuthFactor(
      user_data_auth::TerminateAuthFactorRequest request,
      OnDoneCallback<user_data_auth::TerminateAuthFactorReply> on_done);

  void GetAuthSessionStatus(
      user_data_auth::GetAuthSessionStatusRequest request,
      OnDoneCallback<user_data_auth::GetAuthSessionStatusReply> on_done);

  void LockFactorUntilReboot(
      user_data_auth::LockFactorUntilRebootRequest request,
      OnDoneCallback<user_data_auth::LockFactorUntilRebootReply> on_done);

  void CreateVaultKeyset(
      user_data_auth::CreateVaultKeysetRequest request,
      OnDoneCallback<user_data_auth::CreateVaultKeysetReply> on_done);

  void MigrateLegacyFingerprints(
      user_data_auth::MigrateLegacyFingerprintsRequest request,
      OnDoneCallback<user_data_auth::MigrateLegacyFingerprintsReply> on_done);

 private:
  // base::Thread subclass so we can implement CleanUp.
  class MountThread : public base::Thread {
   public:
    explicit MountThread(const std::string& name, UserDataAuth* uda)
        : base::Thread(name), uda_(uda) {
      CHECK(uda_);
    }
    MountThread(const MountThread&) = delete;
    MountThread& operator=(const MountThread&) = delete;

    ~MountThread() override { Stop(); }

   private:
    void CleanUp() override { uda_->ShutdownTask(); }

    UserDataAuth* const uda_;
  };

  // Shutdown to be run on the worker thread.
  void ShutdownTask();

  // This create a dbus connection whose origin thread is UserDataAuth's mount
  // thread.
  void CreateMountThreadDBus();

  // This will load the policy file for the |obfuscated_username|. If the policy
  // file is not found or could not be loaded, an empty file is saved in place
  // of it as the default user policy file.
  CryptohomeStatusOr<UserPolicyFile*> LoadUserPolicyFile(
      const ObfuscatedUsername& obfuscated_username);

  // =============== Mount Related Utilities ===============

  // Filters out active mounts from |mounts|, populating |active_mounts| set.
  // If |include_busy_mount| is false, then stale mounts with open files and
  // mount points connected to children of the mount source will be treated as
  // active mount, and be moved from |mounts| to |active_mounts|. Otherwise, all
  // stale mounts are included in |mounts|. Returns true if |include_busy_mount|
  // is true and there's at least one stale mount with open file(s) and treated
  // as active mount during the process.
  bool FilterActiveMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts,
      std::multimap<const base::FilePath, const base::FilePath>* active_mounts,
      bool include_busy_mount);

  // Populates |mounts| with ephemeral cryptohome mount points.
  void GetEphemeralLoopDevicesMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts);

  // Unload any user pkcs11 tokens _not_ belonging to one of the mounts in
  // |exclude|. This is used to clean up any stale loaded tokens after a
  // cryptohome crash.
  // Note that system tokens are not affected.
  bool UnloadPkcs11Tokens(const std::vector<base::FilePath>& exclude);

  // Safely empties the UserSessionMap and may requests unmounting.
  // Note: This must only be called on mount thread
  bool RemoveAllMounts();

  // Returns either an existing or a newly created UserSession, if not present.
  UserSession* GetOrCreateUserSession(const Username& username);

  // Removes an inactive user session.
  void RemoveInactiveUserSession(const Username& username);

  // Called on Mount Thread, initializes the challenge_credentials_helper_
  // and the key_challenge_service_factory_.
  void InitForChallengeResponseAuth();

  void GetAuthSessionStatusImpl(
      InUseAuthSession& auth_session,
      user_data_auth::GetAuthSessionStatusReply& reply);

  // Completes remaining steps of |UserDataAuth::Remove| after
  // |AuthSession::PrepareUserForRemoval| has completed.
  void OnPreparedUserForRemoval(
      const ObfuscatedUsername& obfuscated,
      InUseAuthSession auth_session,
      base::OnceCallback<void(const user_data_auth::RemoveReply&)> on_done);

  // Checks the auth factor storage and returns true if the given user has
  // kiosk auth factor. Returns false otherwise.
  bool IsKioskUser(ObfuscatedUsername obfuscated);

  // ================ Fingerprint Auth Related Methods ==================

  // Called on Mount thread. This creates a dbus proxy for Biometrics Daemon
  // and connects to signals.
  void CreateFingerprintManager();

  // OnFingerprintEnrollProgress will be called on every received
  // AuthEnrollmentProgress. It will forward results to
  // |prepare_auth_factor_progress_callback_|.
  void OnFingerprintEnrollProgress(
      user_data_auth::AuthEnrollmentProgress result);

  // OnFingerprintAuthProgress will be called on every received
  // AuthScanDone. It will forward results to
  // |prepare_auth_factor_progress_callback_|.
  void OnFingerprintAuthProgress(user_data_auth::AuthScanDone result);

  // Called on Mount thread. This creates a biometrics service that connects
  // to the biometrics daemon and connect to signals.
  void CreateBiometricsService();

  // ============= WebAuthn / Passkey Related Public Methods ==============

  // Called on Mount thread. This creates a key store cert provider that fetches
  // certificate lists from the server endpoint and maintains the on-disk
  // backend certs.
  void CreateRecoverableKeyStoreBackendCertProvider();

  // =============== PKCS#11 Related Utilities ===============

  // This initializes the PKCS#11 for a particular mount. Note that this is
  // used mostly internally, by Mount related functions to bring up the PKCS#11
  // functionalities after mounting.
  void InitializePkcs11(UserSession* mount);

  // =============== Device Management Related Utilities ===============

  // Method to set device_management_proxy.
  void SetDeviceManagementProxy();

  // =============== Stateful Recovery related Helpers ===============

  // Ensures BootLockbox is finalized;
  void EnsureBootLockboxFinalized();

  // =============== Auth Session Related Helpers ===============

  // Returns a reference to the user session, if the session is mountable. The
  // session is mountable if it is not already mounted, and the guest is not
  // mounted. If user session object doesn't exist, this method will create
  // one.
  CryptohomeStatusOr<UserSession*> GetMountableUserSession(
      AuthSession* auth_session);

  // Pre-mount hook specifies operations that need to be executed before doing
  // mount. Eventually those actions should be triggered outside of mount code.
  // Not applicable to guest user.
  void PreMountHook(const ObfuscatedUsername& obfuscated_username);

  // Post-mount hook specifies operations that need to be executed after doing
  // mount. Eventually those actions should be triggered outside of mount code.
  // Not applicable to guest user.
  void PostMountHook(UserSession* user_session, const MountStatus& error);

  // Helper that will attempt to terminate all existing auth sessions and clear
  // any existing loaded state: USS and AuthFactor objects loaded from storage.
  CryptohomeStatus TerminateAuthSessionsAndClearLoadedState();

  // Converts the Dbus value for encryption type into internal representation.
  libstorage::StorageContainerType DbusEncryptionTypeToContainerType(
      user_data_auth::VaultEncryptionType type);

  // The following methods are implementations for the DBus endpoints of the
  // new API. They are split from the actual end-points to simplify unit
  // testing. The E2E test of the calls is done in tast.

  CryptohomeStatus PrepareGuestVaultImpl();

  // Helper that can prepare persistent vaults with different options, for reuse
  // from different Prepare operations.
  CryptohomeStatus PreparePersistentVaultImpl(
      InUseAuthSession& auth_session,
      const CryptohomeVault::Options& vault_options);

  // =============== Async Subtask Methods ===============

  // Async helper function for StartMigrateToDircrypto which runs once the
  // Username is available. This may be run synchronously if the request is
  // called with an explicit username, but if the operation is executed with an
  // auth session it gets run asynchronously.
  void StartMigrateToDircryptoWithUsername(
      user_data_auth::StartMigrateToDircryptoRequest request,
      OnDoneCallback<user_data_auth::StartMigrateToDircryptoReply> on_done,
      Mount::MigrationCallback progress_callback,
      Username username);

  // Async helper functions for public APIs that require auth sessions. Executed
  // when the AuthSession becomes available.
  void RemoveWithSession(user_data_auth::RemoveRequest request,
                         OnDoneCallback<user_data_auth::RemoveReply> on_done,
                         InUseAuthSession auth_session);
  void StartAuthSessionWithSession(
      user_data_auth::StartAuthSessionRequest request,
      OnDoneCallback<user_data_auth::StartAuthSessionReply> on_done,
      InUseAuthSession auth_session);
  void PrepareEphemeralVaultWithSession(
      user_data_auth::PrepareEphemeralVaultRequest request,
      OnDoneCallback<user_data_auth::PrepareEphemeralVaultReply> on_done,
      InUseAuthSession auth_session);
  void PreparePersistentVaultWithSession(
      user_data_auth::PreparePersistentVaultRequest request,
      OnDoneCallback<user_data_auth::PreparePersistentVaultReply> on_done,
      InUseAuthSession auth_session);
  void PrepareVaultForMigrationWithSession(
      user_data_auth::PrepareVaultForMigrationRequest request,
      OnDoneCallback<user_data_auth::PrepareVaultForMigrationReply> on_done,
      InUseAuthSession auth_session);
  void CreatePersistentUserWithSession(
      user_data_auth::CreatePersistentUserRequest request,
      OnDoneCallback<user_data_auth::CreatePersistentUserReply> on_done,
      InUseAuthSession auth_session);
  void AddAuthFactorWithSession(
      user_data_auth::AddAuthFactorRequest request,
      OnDoneCallback<user_data_auth::AddAuthFactorReply> on_done,
      InUseAuthSession auth_session);
  void AuthenticateAuthFactorWithSession(
      user_data_auth::AuthenticateAuthFactorRequest request,
      OnDoneCallback<user_data_auth::AuthenticateAuthFactorReply> on_done,
      InUseAuthSession auth_session);
  void UpdateAuthFactorWithSession(
      user_data_auth::UpdateAuthFactorRequest request,
      OnDoneCallback<user_data_auth::UpdateAuthFactorReply> on_done,
      InUseAuthSession auth_session);
  void UpdateAuthFactorMetadataWithSession(
      user_data_auth::UpdateAuthFactorMetadataRequest request,
      OnDoneCallback<user_data_auth::UpdateAuthFactorMetadataReply> on_done,
      InUseAuthSession auth_session);
  void RelabelAuthFactorWithSession(
      user_data_auth::RelabelAuthFactorRequest request,
      OnDoneCallback<user_data_auth::RelabelAuthFactorReply> on_done,
      InUseAuthSession auth_session);
  void ReplaceAuthFactorWithSession(
      user_data_auth::ReplaceAuthFactorRequest request,
      OnDoneCallback<user_data_auth::ReplaceAuthFactorReply> on_done,
      InUseAuthSession auth_session);
  void RemoveAuthFactorWithSession(
      user_data_auth::RemoveAuthFactorRequest request,
      OnDoneCallback<user_data_auth::RemoveAuthFactorReply> on_done,
      InUseAuthSession auth_session);
  void ModifyAuthFactorIntentsWithSession(
      user_data_auth::ModifyAuthFactorIntentsRequest request,
      OnDoneCallback<user_data_auth::ModifyAuthFactorIntentsReply> on_done,
      InUseAuthSession auth_session);
  void PrepareAuthFactorWithSession(
      user_data_auth::PrepareAuthFactorRequest request,
      OnDoneCallback<user_data_auth::PrepareAuthFactorReply> on_done,
      InUseAuthSession auth_session);
  void MigrateLegacyFingerprintsWithSession(
      user_data_auth::MigrateLegacyFingerprintsRequest request,
      OnDoneCallback<user_data_auth::MigrateLegacyFingerprintsReply> on_done,
      InUseAuthSession auth_session);

  // =============== Feature Experiment Related Methods ===============

  // Called on Mount thread. This initializes feature library and sets it's
  // value in AuthSession manager.
  void InitializeFeatureLibrary();

  // Called on Mount thread. This returns the feature library, or null if it has
  // not yet been initialized.
  Features* GetFeatures();

  // =============== PinWeaver Related Methods ===============

  // Called on Mount thread. Pairing secret (Pk) is established once per
  // powerwash cycle after the device first boots. An ECDH protocol is used
  // between biometrics AuthStacks and GSC to establish Pk. This function blocks
  // future Pk establishment attempts made by biometrics AuthStacks, as we
  // considered device state becoming more vulnerable after entering user
  // session. For example, an attacker can try to send EC commands to FPMCU and
  // send vendor commands to GSC to complete a person-in-the-middle attack on
  // the ECDH protocol used for Pk establishment.
  void BlockPkEstablishment();

  // =============== Threading Related Variables ===============

  // The task runner that belongs to the thread that created this UserDataAuth
  // object. Currently, this is required to be the same as the dbus thread's
  // task runner.
  scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner_;

  // The thread ID of the thread that created this UserDataAuth object.
  // Currently, this is required to be th esame as the dbus thread's task
  // runner.
  base::PlatformThreadId origin_thread_id_;

  // The thread for performing long running, or mount related operations
  std::unique_ptr<MountThread> mount_thread_;

  // The task runner that belongs to the mount thread.
  scoped_refptr<base::SingleThreadTaskRunner> mount_task_runner_;

  // The thread and task runner used for performing scrypt operations.
  std::unique_ptr<base::Thread> scrypt_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> scrypt_task_runner_;

  // =============== Basic Utilities Related Variables ===============
  // The system salt that is used for obfuscating the username
  brillo::SecureBlob system_salt_;

  // Object for accessing platform related functionalities.
  libstorage::Platform* platform_;
  // Object for accessing the HWSec related functions.
  const hwsec::CryptohomeFrontend* hwsec_;
  // Object for accessing the pinweaver manager related functions.
  const hwsec::PinWeaverManagerFrontend* hwsec_pw_manager_;
  // Object for accessing the recovery crypto related functions.
  const hwsec::RecoveryCryptoFrontend* recovery_crypto_;
  // The cryptohome key loader object.
  CryptohomeKeysManager* cryptohome_keys_manager_;
  // The crypto object used by this class.
  Crypto* crypto_;
  // The Recovery auth block service.
  CryptohomeRecoveryAuthBlockService* recovery_ab_service_;

  // The default token manager client for accessing chapsd's PKCS#11 interface
  std::unique_ptr<chaps::TokenManagerClient> default_chaps_client_;

  // The actual token manager client used by this class, usually set to
  // default_chaps_client_, but can be overridden for testing.
  chaps::TokenManagerClient* chaps_client_;

  // A dbus connection, this is used by any code in this class that needs access
  // to the system DBus and accesses it on the mount thread. Such as when
  // creating an instance of KeyChallengeService.
  scoped_refptr<::dbus::Bus> mount_thread_bus_;

  // The default PKCS#11 init object that is used to supply some PKCS#11 related
  // information.
  std::unique_ptr<Pkcs11Init> default_pkcs11_init_;

  // The actual PKCS#11 init object that is used by this class, but can be
  // overridden for testing.
  Pkcs11Init* pkcs11_init_;

  // The default factory for Pkcs11Token objects.
  std::unique_ptr<Pkcs11TokenFactory> default_pkcs11_token_factory_;

  // The actual factory for Pkcs11TokenObjects.
  Pkcs11TokenFactory* pkcs11_token_factory_;

  // The default Fingerprint Manager object for fingerprint authentication.
  std::unique_ptr<FingerprintManager> default_fingerprint_manager_;

  // Each user has a user policy file. If the file could not be found, it will
  // be created with default settings. The user policy file is loaded lazily.
  absl::node_hash_map<ObfuscatedUsername, UserPolicyFile> user_policy_files_;

  // The actual Fingerprint Manager object that is used by this class, but
  // can be overridden for testing.
  FingerprintManager* fingerprint_manager_ = nullptr;

  // The fingerprint service object that wraps the fingerprint manager for auth
  // block usage.
  std::unique_ptr<FingerprintAuthBlockService> fingerprint_service_;

  // The default Biometrics Service object for biometrics authentication.
  std::unique_ptr<BiometricsAuthBlockService> default_biometrics_service_;

  // The actual Biometrics Service object that is used by this class, but
  // can be overridden for testing.
  BiometricsAuthBlockService* biometrics_service_ = nullptr;

  // The default Recoverable Key Store Backend Cert Provider object for
  // recoverable key store generation.
  std::unique_ptr<RecoverableKeyStoreBackendCertProvider>
      default_key_store_cert_provider_;

  // The actual Recoverable Key Store Backend Cert Provider object that is used
  // by this class, but can be overridden for testing.
  RecoverableKeyStoreBackendCertProvider* key_store_cert_provider_ = nullptr;

  // The object that handles construction and saving of VaultKeysets to disk,
  // used for testing purposes.
  std::unique_ptr<CreateVaultKeysetRpcImpl> create_vault_keyset_impl_;

  // =============== Mount Related Variables ===============

  // This holds a timestamp for each user that is the time that the user was
  // active.
  UserOldestActivityTimestampManager* user_activity_timestamp_manager_;
  // This holds the object that records information about the vault keysets.
  // This is to be accessed from the mount thread only because there's no
  // guarantee on thread safety of the HomeDirs object.
  KeysetManagement* keyset_management_;
  // User secret stash storage helper.
  UssStorage* uss_storage_;
  // Manages all of the loaded USS objects.
  UssManager* uss_manager_;
  // Manager of auth factor files.
  AuthFactorManager* auth_factor_manager_;

  std::unique_ptr<CryptohomeVaultFactory> default_vault_factory_;
  // Usually points to |default_vault_factory_|, but can be overridden for
  // testing.
  CryptohomeVaultFactory* vault_factory_ = nullptr;

  // The homedirs_ object in normal operation
  std::unique_ptr<HomeDirs> default_homedirs_;

  // This holds the object that records information about the homedirs.
  // This is usually set to default_homedirs_, but can be overridden for
  // testing.
  // This is to be accessed from the mount thread only because there's no
  // guarantee on thread safety of the HomeDirs object.
  HomeDirs* homedirs_ = nullptr;

  // To communicate with device_management service.
  std::unique_ptr<DeviceManagementClientProxy>
      default_device_management_client_;
  DeviceManagementClientProxy* device_management_client_ = nullptr;

  // Default challenge credential helper utility object. This object is required
  // for doing a challenge response style login, and is only lazily created when
  // mounting a mount that requires challenge response login type is performed.
  std::unique_ptr<ChallengeCredentialsHelper>
      default_challenge_credentials_helper_;

  // Actual challenge credential helper utility object used by this class.
  // Usually set to |default_challenge_credentials_helper_|, but can be
  // overridden for testing.
  ChallengeCredentialsHelper* challenge_credentials_helper_ = nullptr;

  bool challenge_credentials_helper_initialized_ = false;

  // The signalling interface, including a null default interface.
  NullSignalling default_signalling_;
  SignallingInterface* signalling_intf_ = &default_signalling_;

  // The object used to instantiate AuthBlocks.
  std::unique_ptr<AuthBlockUtility> default_auth_block_utility_;
  // This holds the object that records information about the
  // auth_block_utility. This is usually set to default_auth_block_utility_, but
  // can be overridden for testing. This is to be accessed from the mount thread
  // only because there's no guarantee on thread safety of the HomeDirs object.
  AuthBlockUtility* auth_block_utility_ = nullptr;

  // Manager of the auth factor drivers.
  std::unique_ptr<AuthFactorDriverManager> default_auth_factor_driver_manager_;
  // Usually set to |default_auth_factor_manager_|, but can be overridden for
  // tests.
  AuthFactorDriverManager* auth_factor_driver_manager_ = nullptr;

  // Utility object for functions specific to legacy fingerprint migration flow.
  std::unique_ptr<FpMigrationUtility> default_fp_migration_utility_;
  // Usually set to |default_fp_migration_utility_|, but can be overridden for
  // tests.
  FpMigrationUtility* fp_migration_utility_ = nullptr;

  // Records the UserSession objects associated with each username.
  // This and its content should only be accessed from the mount thread.
  UserSessionMap default_sessions_;
  // Usually points to |default_sessions_|, but can be overridden for tests.
  UserSessionMap* sessions_ = &default_sessions_;

  // Manager for auth session objects.
  std::unique_ptr<AuthSessionManager> default_auth_session_manager_;
  // Usually set to default_auth_session_manager_, but can be overridden for
  // tests.
  AuthSessionManager* auth_session_manager_ = nullptr;

  // The low_disk_space_handler_ object in normal operation
  std::unique_ptr<LowDiskSpaceHandler> default_low_disk_space_handler_;

  // This holds the object that checks for low disk space and performs disk
  // cleanup.
  // This is to be accessed from the mount thread only because there's no
  // guarantee on thread safety of the HomeDirs object.
  LowDiskSpaceHandler* low_disk_space_handler_ = nullptr;

  // TODO(dlunev): This three variables are a hack to pass cleanup parameters
  // from main to the actual object. The reason it is done like this is that
  // the object is created in UserDataAuth::Initialize, which is called from the
  // daemonization function, but they are attempted to be set from the main,
  // before the daemonization. Once service.cc is gone, we shall refactor the
  // whole initialization process of UserDataAuth to avoid such hacks.
  uint64_t disk_cleanup_threshold_;
  uint64_t disk_cleanup_aggressive_threshold_;
  uint64_t disk_cleanup_critical_threshold_;
  uint64_t disk_cleanup_target_free_space_;

  // Factory for creating |Mount| objects.
  std::unique_ptr<MountFactory> default_mount_factory_;
  // This usually points to |default_mount_factory_|, but can be overridden in
  // tests.
  MountFactory* mount_factory_ = nullptr;

  // The default user session factory instance that can be used by this class to
  // create UserSession object.
  std::unique_ptr<UserSessionFactory> default_user_session_factory_;

  // The user session factory instance that can be overridden for tests.
  UserSessionFactory* user_session_factory_ = nullptr;

  // This holds the salt that is used to derive the passkey for public mounts.
  brillo::SecureBlob public_mount_salt_;

  // Default factory of key challenge services. This object is required for
  // doing a challenge response style login.
  KeyChallengeServiceFactoryImpl default_key_challenge_service_factory_;

  // Actual factory of key challenge services that is used by this class.
  // Usually set to |default_key_challenge_service_factory_|, but can be
  // overridden for testing.
  KeyChallengeServiceFactory* key_challenge_service_factory_ =
      &default_key_challenge_service_factory_;

  // Guest user's username.
  Username guest_user_;

  // Force the use of eCryptfs. If eCryptfs is not used, then dircrypto (the
  // ext4 directory encryption) is used.
  bool force_ecryptfs_ = true;

  // Force v2 version for fscrypt interface.
  bool fscrypt_v2_ = false;

  // Enable creation of LVM volumes for applications.
  bool enable_application_containers_ = false;

  // Whether we are using legacy mount. See Mount::MountLegacyHome()'s comment
  // for more information.
  bool legacy_mount_ = true;

  // A counter to count the number of parallel tasks on mount thread.
  // Recorded when a requests comes in. Counts of 1 will not reported.
  std::atomic<int> parallel_task_count_ = 0;

  // Flag to cache the status of whether Pk establishment is blocked
  // successfully, so we don't have to do this multiple times.
  bool pk_establishment_blocked_ = false;

  // Feature library to fetch enabled feature on Finch.
  std::unique_ptr<Features> default_features_;

  // This holds the object that checks for feature enabled.
  Features* features_ = nullptr;
  AsyncInitFeatures async_init_features_;

  friend class AuthSessionTestWithKeysetManagement;
  FRIEND_TEST(AuthSessionTestWithKeysetManagement,
              StartAuthSessionWithoutKeyData);

  friend class UserDataAuthTestTasked;
  FRIEND_TEST(UserDataAuthTest, Unmount_AllDespiteFailures);
  FRIEND_TEST(UserDataAuthTest, InitializePkcs11Unmounted);

  friend class UserDataAuthExTest;
  friend class AuthSessionInterfaceTest;
  FRIEND_TEST(UserDataAuthTest, CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly);
  FRIEND_TEST(UserDataAuthTest,
              CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly_FirstBoot);
  FRIEND_TEST(UserDataAuthExTest, ExtendAuthSession);
  FRIEND_TEST(UserDataAuthExTest, ExtendUnAuthenticatedAuthSessionFail);
  FRIEND_TEST(UserDataAuthExTest, CheckTimeoutTimerSetAfterAuthentication);
  FRIEND_TEST(UserDataAuthExTest, InvalidateAuthSession);
  FRIEND_TEST(UserDataAuthExTest, MountUnauthenticatedAuthSession);
  FRIEND_TEST(UserDataAuthExTest, RemoveValidityWithAuthSession);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSessionUnusableClobber);
  FRIEND_TEST(UserDataAuthExTest,
              StartMigrateToDircryptoWithAuthenticatedAuthSession);
  FRIEND_TEST(UserDataAuthExTest,
              StartMigrateToDircryptoWithUnAuthenticatedAuthSession);
  friend class AuthSessionInterfaceTestBase;
  friend class AuthSessionInterfaceTest;
  friend class AuthSessionInterfaceMockAuthTest;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USERDATAAUTH_H_
