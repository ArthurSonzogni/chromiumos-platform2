// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Mount.

#include "cryptohome/mount.h"

#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/sha1.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/values.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <brillo/cryptohome.h>
#include <brillo/process.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>

#include "cryptohome/bootlockbox/boot_lockbox.h"
#include "cryptohome/chaps_client_factory.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/obfuscated_username.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"

#include "vault_keyset.pb.h"  // NOLINT(build/include)

using base::FilePath;
using base::StringPrintf;
using brillo::BlobToString;
using brillo::SecureBlob;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::IsSanitizedUserName;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using chaps::IsolateCredentialManager;

namespace {
constexpr char kChromeMountNamespacePath[] = "/run/namespaces/mnt_chrome";

bool __attribute__((unused)) IsolateUserSession()  {
#if USE_USER_SESSION_ISOLATION
  return true;
#else
  return false;
#endif
}
}  // namespace

namespace cryptohome {

const char kChapsUserName[] = "chaps";
const char kDefaultSharedAccessGroup[] = "chronos-access";

// TODO(fes): Remove once UI for BWSI switches to MountGuest()
const char kIncognitoUser[] = "incognito";

const char kKeyFile[] = "master";
const int kKeyFileMax = 100;  // master.0 ... master.99
const mode_t kKeyFilePermissions = 0600;
const char kKeyLegacyPrefix[] = "legacy-";

void StartUserFileAttrsCleanerService(cryptohome::Platform* platform,
                                      const std::string& username) {
  std::unique_ptr<brillo::Process> file_attrs =
      platform->CreateProcessInstance();

  file_attrs->AddArg("/sbin/initctl");
  file_attrs->AddArg("start");
  file_attrs->AddArg("--no-wait");
  file_attrs->AddArg("file_attrs_cleaner_tool");
  file_attrs->AddArg(
      base::StringPrintf("OBFUSCATED_USERNAME=%s", username.c_str()));

  if (file_attrs->Run() != 0)
    PLOG(WARNING) << "Error while running file_attrs_cleaner_tool";
}

Mount::Mount()
    : default_user_(-1),
      chaps_user_(-1),
      default_group_(-1),
      default_access_group_(-1),
      shadow_root_(kDefaultShadowRoot),
      skel_source_(kDefaultSkeletonSource),
      system_salt_(),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      crypto_(NULL),
      default_homedirs_(new HomeDirs()),
      homedirs_(default_homedirs_.get()),
      use_tpm_(true),
      default_current_user_(new UserSession()),
      current_user_(default_current_user_.get()),
      user_timestamp_cache_(NULL),
      enterprise_owned_(false),
      pkcs11_state_(kUninitialized),
      is_pkcs11_passkey_migration_required_(false),
      dircrypto_key_id_(dircrypto::kInvalidKeySerial),
      legacy_mount_(true),
      mount_type_(MountType::NONE),
      shadow_only_(false),
      default_chaps_client_factory_(new ChapsClientFactory()),
      chaps_client_factory_(default_chaps_client_factory_.get()),
      boot_lockbox_(NULL),
      dircrypto_migration_stopped_condition_(&active_dircrypto_migrator_lock_),
      mount_guest_session_out_of_process_(true),
      mount_guest_session_non_root_namespace_(false) {}

Mount::~Mount() {
  if (IsMounted())
    UnmountCryptohome();
}

bool Mount::Init(Platform* platform, Crypto* crypto,
                 UserOldestActivityTimestampCache* cache,
                 PreMountCallback pre_mount_callback) {
  platform_ = platform;
  crypto_ = crypto;
  user_timestamp_cache_ = cache;
  pre_mount_callback_ = pre_mount_callback;

  bool result = true;

  homedirs_->set_platform(platform_);
  homedirs_->set_shadow_root(FilePath(shadow_root_));
  homedirs_->set_enterprise_owned(enterprise_owned_);

  // Make sure |homedirs_| uses the same PolicyProvider instance as we in case
  // it was set by a test.
  if (policy_provider_)
    homedirs_->set_policy_provider(policy_provider_.get());

  if (!homedirs_->Init(platform, crypto, user_timestamp_cache_))
    result = false;

  // Get the user id and group id of the default user
  if (!platform_->GetUserId(kDefaultSharedUser, &default_user_,
                            &default_group_)) {
    result = false;
  }

  // Get the user id of the chaps user.
  gid_t not_used;
  if (!platform_->GetUserId(kChapsUserName, &chaps_user_, &not_used)) {
    result = false;
  }

  // Get the group id of the default shared access group.
  if (!platform_->GetGroupId(kDefaultSharedAccessGroup,
                             &default_access_group_)) {
    result = false;
  }

  {
    brillo::ScopedUmask scoped_umask(kDefaultUmask);
    // Create the shadow root if it doesn't exist
    if (!platform_->DirectoryExists(shadow_root_)) {
      platform_->CreateDirectory(shadow_root_);
    }

    if (use_tpm_ && !boot_lockbox_) {
      default_boot_lockbox_.reset(
          new BootLockbox(Tpm::GetSingleton(), platform_, crypto_));
      boot_lockbox_ = default_boot_lockbox_.get();
    }

    // One-time load of the global system salt (used in generating username
    // hashes)
    FilePath system_salt_file = shadow_root_.Append(kSystemSaltFile);
    if (!crypto_->GetOrCreateSalt(system_salt_file,
                                  CRYPTOHOME_DEFAULT_SALT_LENGTH, false,
                                  &system_salt_)) {
      LOG(ERROR) << "Failed to load or create the system salt";
      result = false;
    }
  }

  current_user_->Init(system_salt_);

  mounter_.reset(new MountHelper(
      default_user_, default_group_, default_access_group_, shadow_root_,
      skel_source_, system_salt_, legacy_mount_, platform_));

  std::unique_ptr<MountNamespace> chrome_mnt_ns;
  if (mount_guest_session_non_root_namespace_) {
    chrome_mnt_ns = std::make_unique<MountNamespace>(
        base::FilePath(kChromeMountNamespacePath), platform_);
    if (!chrome_mnt_ns->Create()) {
      LOG(ERROR) << "Failed to create mount namespace at "
                 << kChromeMountNamespacePath;
      result = false;
    }
  }

  if (mount_guest_session_out_of_process_) {
    out_of_process_mounter_.reset(new OutOfProcessMountHelper(
        system_salt_, std::move(chrome_mnt_ns), legacy_mount_, platform_));
  }

  return result;
}

bool Mount::EnsureCryptohome(const Credentials& credentials,
                             const MountArgs& mount_args,
                             bool* created) {
  // If the user has an old-style cryptohome, delete it.
  FilePath old_image_path = GetUserDirectory(credentials).Append("image");
  if (platform_->FileExists(old_image_path)) {
    platform_->DeleteFile(GetUserDirectory(credentials), true);
  }
  if (!mount_args.shadow_only) {
    if (!mounter_->EnsureUserMountPoints(credentials.username())) {
      return false;
    }
  }
  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  // Now check for the presence of a cryptohome.
  if (homedirs_->CryptohomeExists(obfuscated_username)) {
    // Now check for the presence of a vault directory.
    FilePath vault_path =
        homedirs_->GetEcryptfsUserVaultPath(obfuscated_username);
    if (platform_->DirectoryExists(vault_path)) {
      if (mount_args.to_migrate_from_ecryptfs) {
        // When migrating, set the mount_type_ to dircrypto even if there is an
        // eCryptfs vault.
        mount_type_ = MountType::DIR_CRYPTO;
      } else {
        mount_type_ = MountType::ECRYPTFS;
      }
    } else {
      if (mount_args.to_migrate_from_ecryptfs) {
        LOG(ERROR) << "No eCryptfs vault to migrate.";
        return false;
      } else {
        mount_type_ = MountType::DIR_CRYPTO;
      }
    }
    *created = false;
    return true;
  }
  // Create the cryptohome from scratch.
  // If the kernel supports it, steer toward ext4 crypto.
  if (mount_args.create_as_ecryptfs) {
    mount_type_ = MountType::ECRYPTFS;
  } else {
    dircrypto::KeyState state = platform_->GetDirCryptoKeyState(shadow_root_);
    switch (state) {
      case dircrypto::KeyState::UNKNOWN:
      case dircrypto::KeyState::ENCRYPTED:
        LOG(ERROR) << "Unexpected state " << static_cast<int>(state);
        return false;
      case dircrypto::KeyState::NOT_SUPPORTED:
        mount_type_ = MountType::ECRYPTFS;
        break;
      case dircrypto::KeyState::NO_KEY:
        mount_type_ = MountType::DIR_CRYPTO;
        break;
    }
  }
  *created = CreateCryptohome(credentials);
  return *created;
}

bool Mount::MountCryptohome(const Credentials& credentials,
                            const Mount::MountArgs& mount_args,
                            MountError* mount_error) {
  CHECK(boot_lockbox_ || !use_tpm_);
  if (boot_lockbox_ && !boot_lockbox_->FinalizeBoot()) {
    LOG(WARNING) << "Failed to finalize boot lockbox.";
  }

  if (!pre_mount_callback_.is_null()) {
    pre_mount_callback_.Run();
  }

  if (IsMounted()) {
    if (mount_error)
      *mount_error = MOUNT_ERROR_MOUNT_POINT_BUSY;
    return false;
  }

  MountError local_mount_error = MOUNT_ERROR_NONE;
  bool result = MountCryptohomeInner(credentials,
                                     mount_args,
                                     true,
                                     &local_mount_error);
  // Retry once if there is a TPM communications failure
  if (!result && local_mount_error == MOUNT_ERROR_TPM_COMM_ERROR) {
    result = MountCryptohomeInner(credentials,
                                  mount_args,
                                  true,
                                  &local_mount_error);
  }
  if (mount_error) {
    *mount_error = local_mount_error;
  }
  return result;
}

bool Mount::AddEcryptfsAuthToken(const VaultKeyset& vault_keyset,
                                 std::string* key_signature,
                                 std::string* filename_key_signature) const {
  // Add the File Encryption key (FEK) from the vault keyset.  This is the key
  // that is used to encrypt the file contents when the file is persisted to the
  // lower filesystem by eCryptfs.
  *key_signature = CryptoLib::SecureBlobToHex(vault_keyset.fek_sig());
  if (!platform_->AddEcryptfsAuthToken(
        vault_keyset.fek(), *key_signature,
        vault_keyset.fek_salt())) {
    LOG(ERROR) << "Couldn't add eCryptfs file encryption key to keyring.";
    return false;
  }

  // Add the File Name Encryption Key (FNEK) from the vault keyset.  This is the
  // key that is used to encrypt the file name when the file is persisted to the
  // lower filesystem by eCryptfs.
  *filename_key_signature = CryptoLib::SecureBlobToHex(vault_keyset.fnek_sig());
  if (!platform_->AddEcryptfsAuthToken(
        vault_keyset.fnek(), *filename_key_signature,
        vault_keyset.fnek_salt())) {
    LOG(ERROR) << "Couldn't add eCryptfs filename encryption key to keyring.";
    return false;
  }

  return true;
}

bool Mount::MountCryptohomeInner(const Credentials& credentials,
                                 const Mount::MountArgs& mount_args,
                                 bool recreate_on_decrypt_fatal,
                                 MountError* mount_error) {
  current_user_->Reset();

  std::string username = credentials.username();
  if (username.compare(kIncognitoUser) == 0) {
    // TODO(fes): Have guest set error conditions?
    *mount_error = MOUNT_ERROR_NONE;
    return MountGuestCryptohome();
  }

  // Remove all existing cryptohomes, except for the owner's one, if the
  // ephemeral users policy is on.
  // Note that a fresh policy value is read here, which in theory can conflict
  // with the one used for calculation of |mount_args.is_ephemeral|. However,
  // this inconsistency (whose probability is anyway pretty low in practice)
  // should only lead to insignificant transient glitches, like an attempt to
  // mount a non existing anymore cryptohome.
  if (homedirs_->AreEphemeralUsersEnabled())
    homedirs_->RemoveNonOwnerCryptohomes();

  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  const bool is_owner = homedirs_->IsOrWillBeOwner(username);

  // Process ephemeral mounts in a special manner.
  if (mount_args.is_ephemeral) {
    if (!mount_args.create_if_missing) {
      NOTREACHED() << "An ephemeral cryptohome can only be mounted when its "
                      "creation on-the-fly is allowed.";
      *mount_error = MOUNT_ERROR_INVALID_ARGS;
      return false;
    }

    if (is_owner) {
      LOG(ERROR) << "An ephemeral cryptohome can only be mounted when the user "
                    "is not the owner.";
      *mount_error = MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER;
      return false;
    }

    // This callback will be executed in the destructor at the latest so |this|
    // will always be valid.
    base::Closure cleanup = base::Bind(
        [](Mount* m) {
          m->UnmountAndDropKeys();
          m->CleanUpEphemeral();
        },
        base::Unretained(this));

    // Ephemeral cryptohomes for regular users are mounted in-process.
    if (!MountEphemeralCryptohome(credentials.username(), mounter_.get(),
                                  std::move(cleanup))) {
      homedirs_->Remove(credentials.username());
      *mount_error = MOUNT_ERROR_FATAL;
      return false;
    }

    // Ephemeral and guest users will not have a key index.
    current_user_->SetUser(credentials);
    *mount_error = MOUNT_ERROR_NONE;
    return true;
  }

  if (!mount_args.create_if_missing &&
      !homedirs_->CryptohomeExists(obfuscated_username)) {
    LOG(ERROR) << "Asked to mount nonexistent user";
    *mount_error = MOUNT_ERROR_USER_DOES_NOT_EXIST;
    return false;
  }

  bool created = false;
  if (!EnsureCryptohome(credentials, mount_args, &created)) {
    LOG(ERROR) << "Error creating cryptohome.";
    *mount_error = MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
    return false;
  }

  // Attempt to decrypt the vault keyset with the specified credentials.
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(platform_, crypto_);
  SerializedVaultKeyset serialized;
  MountError local_mount_error = MOUNT_ERROR_NONE;
  int index = -1;
  if (!DecryptVaultKeyset(credentials, &vault_keyset, &serialized, &index,
                          &local_mount_error)) {
    *mount_error = local_mount_error;
    if (recreate_on_decrypt_fatal && local_mount_error == MOUNT_ERROR_FATAL) {
      LOG(ERROR) << "cryptohome must be re-created because of fatal error.";
      if (!homedirs_->Remove(credentials.username())) {
        LOG(ERROR) << "Fatal decryption error, but unable to remove "
                   << "cryptohome.";
        *mount_error = MOUNT_ERROR_REMOVE_INVALID_USER_FAILED;
        return false;
      }
      // Allow one recursion into MountCryptohomeInner by blocking re-create on
      // fatal.
      bool local_result = MountCryptohomeInner(credentials,
                                               mount_args,
                                               false,
                                               mount_error);
      // If the mount was successful, set the status to indicate that the
      // cryptohome was recreated.
      if (local_result) {
        *mount_error = MOUNT_ERROR_RECREATED;
      }
      return local_result;
    }
    return false;
  }

  // It's safe to generate a reset_seed here.
  if (!serialized.has_wrapped_reset_seed()) {
    vault_keyset.CreateRandomResetSeed();
  }

  if (!serialized.has_wrapped_chaps_key()) {
    is_pkcs11_passkey_migration_required_ = true;
    vault_keyset.CreateRandomChapsKey();
    ReEncryptVaultKeyset(credentials, index, &vault_keyset, &serialized);
  }

  SecureBlob local_chaps_key(vault_keyset.chaps_key().begin(),
                             vault_keyset.chaps_key().end());
  pkcs11_token_auth_data_.swap(local_chaps_key);
  platform_->ClearUserKeyring();

  // Before we use the matching keyset, make sure it isn't being misused.
  // Note, privileges don't protect against information leakage, they are
  // just software/DAC policy enforcement mechanisms.
  //
  // In the future we may provide some assurance by wrapping privileges
  // with the wrapped_key, but that is still of limited benefit.
  if (serialized.has_key_data() &&  // legacy keys are full privs
      !serialized.key_data().privileges().mount()) {
    // TODO(wad): Convert to CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED
    // TODO(wad): Expose the safe-printable label rather than the Chrome
    //            supplied one for log output.
    LOG(ERROR) << "Mount attempt with unprivileged key.";
    *mount_error = MOUNT_ERROR_UNPRIVILEGED_KEY;
    return false;
  }

  // Checks whether migration from ecryptfs to dircrypto is needed, and returns
  // an error when necessary. Do this after the check by DecryptVaultKeyset,
  // because a correct credential is required before switching to migration UI.
  if (homedirs_->EcryptfsCryptohomeExists(obfuscated_username) &&
      homedirs_->DircryptoCryptohomeExists(obfuscated_username) &&
      !mount_args.to_migrate_from_ecryptfs) {
    // If both types of home directory existed, it implies that the migration
    // attempt was aborted in the middle before doing clean up.
    LOG(ERROR) << "Mount failed because both eCryptfs and dircrypto home"
               << " directories were found. Need to resume and finish"
               << " migration first.";
    *mount_error = MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE;
    return false;
  }

  if (mount_type_ == MountType::ECRYPTFS && mount_args.force_dircrypto) {
    // If dircrypto is forced, it's an error to mount ecryptfs home.
    LOG(ERROR) << "Mount attempt with force_dircrypto on eCryptfs.";
    *mount_error = MOUNT_ERROR_OLD_ENCRYPTION;
    return false;
  }

  if (!platform_->SetupProcessKeyring()) {
    LOG(ERROR) << "Failed to set up a process keyring.";
    *mount_error = MOUNT_ERROR_SETUP_PROCESS_KEYRING_FAILED;
    return false;
  }
  // When migrating, mount both eCryptfs and dircrypto.
  const bool should_mount_ecryptfs = mount_type_ == MountType::ECRYPTFS ||
      mount_args.to_migrate_from_ecryptfs;
  const bool should_mount_dircrypto = mount_type_ == MountType::DIR_CRYPTO;
  if (!should_mount_ecryptfs && !should_mount_dircrypto) {
    NOTREACHED() << "Unexpected mount type " << static_cast<int>(mount_type_);
    *mount_error = MOUNT_ERROR_UNEXPECTED_MOUNT_TYPE;
    return false;
  }

  // Ensure we don't leave any mounts hanging on intermediate errors.
  // The closure won't outlive the class so |this| will always be valid.
  base::ScopedClosureRunner unmount_and_drop_keys_runner(
      base::Bind(&Mount::UnmountAndDropKeys, base::Unretained(this)));

  std::string key_signature, fnek_signature;
  if (should_mount_ecryptfs) {
    // Add the decrypted key to the keyring so that ecryptfs can use it.
    if (!AddEcryptfsAuthToken(vault_keyset, &key_signature,
                              &fnek_signature)) {
      LOG(ERROR) << "Error adding eCryptfs keys.";
      *mount_error = MOUNT_ERROR_KEYRING_FAILED;
      return false;
    }
  }
  if (should_mount_dircrypto) {
    LOG_IF(WARNING, dircrypto_key_id_ != dircrypto::kInvalidKeySerial)
        << "Already mounting with key " << dircrypto_key_id_;
    if (!platform_->AddDirCryptoKeyToKeyring(
            vault_keyset.fek(), vault_keyset.fek_sig(), &dircrypto_key_id_)) {
      LOG(ERROR) << "Error adding dircrypto key.";
      *mount_error = MOUNT_ERROR_KEYRING_FAILED;
      return false;
    }
  }

  // Mount cryptohome
  // /home/.shadow: owned by root
  // /home/.shadow/$hash: owned by root
  // /home/.shadow/$hash/vault: owned by root
  // /home/.shadow/$hash/mount: owned by root
  // /home/.shadow/$hash/mount/root: owned by root
  // /home/.shadow/$hash/mount/user: owned by chronos
  // /home/chronos: owned by chronos
  // /home/chronos/user: owned by chronos
  // /home/user/$hash: owned by chronos
  // /home/root/$hash: owned by root

  FilePath vault_path =
      homedirs_->GetEcryptfsUserVaultPath(obfuscated_username);

  mount_point_ = homedirs_->GetUserMountDirectory(obfuscated_username);
  if (!platform_->CreateDirectory(mount_point_)) {
    PLOG(ERROR) << "Directory creation failed for " << mount_point_.value();
    *mount_error = MOUNT_ERROR_DIR_CREATION_FAILED;
    return false;
  }
  if (mount_args.to_migrate_from_ecryptfs) {
    FilePath temporary_mount_point =
        GetUserTemporaryMountDirectory(obfuscated_username);
    if (!platform_->CreateDirectory(temporary_mount_point)) {
      PLOG(ERROR) << "Directory creation failed for "
                  << temporary_mount_point.value();
      *mount_error = MOUNT_ERROR_DIR_CREATION_FAILED;
      return false;
    }
  }

  // Since Service::Mount cleans up stale mounts, we should only reach
  // this point if someone attempts to re-mount an in-use mount point.
  if (platform_->IsDirectoryMounted(mount_point_)) {
    LOG(ERROR) << "Mount point is busy: " << mount_point_.value()
               << " for " << vault_path.value();
    *mount_error = MOUNT_ERROR_FATAL;
    return false;
  }

  if (should_mount_dircrypto) {
    if (!platform_->SetDirCryptoKey(mount_point_, vault_keyset.fek_sig())) {
      LOG(ERROR) << "Failed to set directory encryption policy for "
                 << mount_point_.value();
      *mount_error = MOUNT_ERROR_SET_DIR_CRYPTO_KEY_FAILED;
      return false;
    }
  }

  // Set the current user here so we can rely on it in the helpers.
  // On failure, they will linger, but should be reset on a new MountCryptohome
  // request.
  current_user_->SetUser(credentials);
  current_user_->set_key_index(index);
  if (serialized.has_key_data()) {
    current_user_->set_key_data(serialized.key_data());
  }

  MountHelper::Options mount_opts = {
      mount_type_, mount_args.to_migrate_from_ecryptfs, mount_args.shadow_only};

  if (!mounter_->PerformMount(mount_opts, credentials, key_signature,
                              fnek_signature, created, mount_error)) {
    LOG(ERROR) << "MountHelper::PerformMount failed";
    return false;
  }

  if (!UserSignInEffects(true /* is_mount */, is_owner)) {
    LOG(ERROR) << "Failed to set user type, aborting mount";
    *mount_error = MOUNT_ERROR_TPM_COMM_ERROR;
    return false;
  }

  // At this point we're done mounting so move the clean-up closure to the
  // instance variable.
  mount_cleanup_ = unmount_and_drop_keys_runner.Release();

  *mount_error = MOUNT_ERROR_NONE;

  switch (mount_type_) {
    case MountType::ECRYPTFS:
      ReportHomedirEncryptionType(HomedirEncryptionType::kEcryptfs);
      break;
    case MountType::DIR_CRYPTO:
      ReportHomedirEncryptionType(HomedirEncryptionType::kDircrypto);
      break;
    default:
      // We're only interested in encrypted home directories.
      NOTREACHED() << "Unknown homedir encryption type: "
                   << static_cast<int>(mount_type_);
      break;
  }

  if (is_pkcs11_passkey_migration_required_) {
    credentials.GetPasskey(&legacy_pkcs11_passkey_);
  }

  // Start file attribute cleaner service.
  StartUserFileAttrsCleanerService(platform_, obfuscated_username);

  // TODO(fqj,b/116072767) Ignore errors since unlabeled files are currently
  // still okay during current development progress.
  LOG(INFO) << "Restoring SELinux context for homedir.";
  platform_->RestoreSELinuxContexts(
      homedirs_->GetUserMountDirectory(obfuscated_username),
      true);

  return true;
}

void Mount::CleanUpEphemeral() {
  if (!mounter_->CleanUpEphemeral()) {
    ReportCryptohomeError(kEphemeralCleanUpFailed);
  }
}

bool Mount::MountEphemeralCryptohome(
    const std::string& username,
    EphemeralMountHelperInterface* ephemeral_mounter,
    base::Closure cleanup) {
  // Ephemeral cryptohome can't be mounted twice.
  CHECK(ephemeral_mounter->CanPerformEphemeralMount());

  base::ScopedClosureRunner cleanup_runner(cleanup);

  if (!ephemeral_mounter->PerformEphemeralMount(username)) {
    LOG(ERROR) << "PerformEphemeralMount() failed, aborting ephemeral mount";
    return false;
  }

  if (!UserSignInEffects(true /* is_mount */, false /* is_owner */)) {
    LOG(ERROR) << "Failed to set user type, aborting ephemeral mount";
    return false;
  }

  // Mount succeeded, move the clean-up closure to the instance variable.
  mount_cleanup_ = cleanup_runner.Release();

  mount_type_ = MountType::EPHEMERAL;
  return true;
}

void Mount::UnmountAndDropKeys() {
  mounter_->UnmountAll();

  // Invalidate dircrypto key to make directory contents inaccessible.
  if (dircrypto_key_id_ != dircrypto::kInvalidKeySerial) {
    platform_->InvalidateDirCryptoKey(dircrypto_key_id_, shadow_root_);
    dircrypto_key_id_ = dircrypto::kInvalidKeySerial;
  }
}

bool Mount::UnmountCryptohome() {
  if (!UserSignInEffects(false /* is_mount */, false /* is_owner */)) {
    LOG(WARNING) << "Failed to set user type, but continuing with unmount";
  }

  // There should be no file access when unmounting.
  // Stop dircrypto migration if in progress.
  MaybeCancelActiveDircryptoMigrationAndWait();

  if (!mount_cleanup_.is_null()) {
    std::move(mount_cleanup_).Run();
  }

  if (homedirs_->AreEphemeralUsersEnabled())
    homedirs_->RemoveNonOwnerCryptohomes();
  else
    UpdateCurrentUserActivityTimestamp(0);

  RemovePkcs11Token();
  current_user_->Reset();
  mount_type_ = MountType::NONE;

  platform_->ClearUserKeyring();

  return true;
}

bool Mount::IsMounted() const {
  return (mounter_ && mounter_->MountPerformed()) ||
         (out_of_process_mounter_ && out_of_process_mounter_->MountPerformed());
}

bool Mount::IsNonEphemeralMounted() const {
  return IsMounted() && mount_type_ != MountType::EPHEMERAL;
}

bool Mount::OwnsMountPoint(const FilePath& path) const {
  return (mounter_ && mounter_->IsPathMounted(path)) ||
         (out_of_process_mounter_ && mounter_->IsPathMounted(path));
}

bool Mount::CreateCryptohome(const Credentials& credentials) const {
  brillo::ScopedUmask scoped_umask(kDefaultUmask);

  // Create the user's entry in the shadow root
  FilePath user_dir(GetUserDirectory(credentials));
  platform_->CreateDirectory(user_dir);

  // Generate a new master key
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(platform_, crypto_);
  vault_keyset.CreateRandom();
  SerializedVaultKeyset serialized;
  if (!AddVaultKeyset(credentials, &vault_keyset, &serialized)) {
    LOG(ERROR) << "Failed to add vault keyset to new user";
    return false;
  }
  // Merge in the key data from credentials using the label() as
  // the existence test. (All new-format calls must populate the
  // label on creation.)
  if (!credentials.key_data().label().empty()) {
    *serialized.mutable_key_data() = credentials.key_data();
  }
  if (credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    *serialized.mutable_signature_challenge_info() =
        credentials.challenge_credentials_keyset_info();
  }

  // TODO(wad) move to storage by label-derivative and not number.
  if (!StoreVaultKeysetForUser(
       credentials.GetObfuscatedUsername(system_salt_),
       0,  // first key
       serialized)) {
    LOG(ERROR) << "Failed to store vault keyset for new user";
    return false;
  }

  if (mount_type_ == MountType::ECRYPTFS) {
    // Create the user's vault.
    FilePath vault_path = homedirs_->GetEcryptfsUserVaultPath(
        credentials.GetObfuscatedUsername(system_salt_));
    if (!platform_->CreateDirectory(vault_path)) {
      LOG(ERROR) << "Couldn't create vault path: " << vault_path.value();
      return false;
    }
  }

  return true;
}

bool Mount::CreateTrackedSubdirectories(const Credentials& credentials,
                                        bool is_new) const {
  return mounter_->CreateTrackedSubdirectories(credentials, mount_type_,
                                               is_new);
}

bool Mount::UpdateCurrentUserActivityTimestamp(int time_shift_sec) {
  std::string obfuscated_username;
  current_user_->GetObfuscatedUsername(&obfuscated_username);
  if (!obfuscated_username.empty() && mount_type_ != MountType::EPHEMERAL) {
    SerializedVaultKeyset serialized;
    // TODO(wad) Start using current_user_'s key_data label when
    //           it is defined.
    LoadVaultKeysetForUser(obfuscated_username, current_user_->key_index(),
                           &serialized);
    base::Time timestamp = platform_->GetCurrentTime();
    if (time_shift_sec > 0)
      timestamp -= base::TimeDelta::FromSeconds(time_shift_sec);
    serialized.set_last_activity_timestamp(timestamp.ToInternalValue());
    // Only update the key in use.
    StoreVaultKeysetForUser(obfuscated_username, current_user_->key_index(),
                            serialized);
    if (user_timestamp_cache_->initialized()) {
      user_timestamp_cache_->UpdateExistingUser(
          FilePath(GetUserDirectoryForUser(obfuscated_username)), timestamp);
    }
    return true;
  }
  return false;
}

bool Mount::AreSameUser(const std::string& obfuscated_username) {
  return current_user_->CheckUser(obfuscated_username);
}

const UserSession* Mount::GetCurrentUserSession() const {
  return current_user_;
}

bool Mount::AreValid(const Credentials& credentials) {
  // If the current logged in user matches, use the UserSession to verify the
  // credentials.  This is less costly than a trip to the TPM, and only verifies
  // a user during their logged in session.
  if (current_user_->CheckUser(
          credentials.GetObfuscatedUsername(system_salt_))) {
    return current_user_->Verify(credentials);
  }
  return false;
}

bool Mount::LoadVaultKeyset(const Credentials& credentials,
                            int index,
                            SerializedVaultKeyset* serialized) const {
  return LoadVaultKeysetForUser(credentials.GetObfuscatedUsername(system_salt_),
                                index,
                                serialized);
}

bool Mount::LoadVaultKeysetForUser(const std::string& obfuscated_username,
                                   int index,
                                   SerializedVaultKeyset* serialized) const {
  if (index < 0 || index > kKeyFileMax) {
    LOG(ERROR) << "Attempted to load an invalid key index: " << index;
    return false;
  }
  // Load the encrypted keyset.
  FilePath user_key_file =
      GetUserLegacyKeyFileForUser(obfuscated_username, index);
  if (!platform_->FileExists(user_key_file)) {
    return false;
  }
  brillo::Blob cipher_text;
  if (!platform_->ReadFile(user_key_file, &cipher_text)) {
    LOG(ERROR) << "Failed to read keyset file for user " << obfuscated_username;
    return false;
  }
  if (!serialized->ParseFromArray(cipher_text.data(), cipher_text.size())) {
    LOG(ERROR) << "Failed to parse keyset for user " << obfuscated_username;
    return false;
  }
  return true;
}

bool Mount::StoreVaultKeysetForUser(
    const std::string& obfuscated_username,
    int index,
    const SerializedVaultKeyset& serialized) const {
  if (index < 0 || index > kKeyFileMax) {
    LOG(ERROR) << "Attempted to store an invalid key index: " << index;
    return false;
  }
  brillo::Blob final_blob(serialized.ByteSize());
  serialized.SerializeWithCachedSizesToArray(
      static_cast<google::protobuf::uint8*>(final_blob.data()));
  return platform_->WriteFileAtomicDurable(
      GetUserLegacyKeyFileForUser(obfuscated_username, index),
      final_blob,
      kKeyFilePermissions);
}

bool Mount::DecryptVaultKeyset(const Credentials& credentials,
                               VaultKeyset* vault_keyset,
                               SerializedVaultKeyset* serialized,
                               int* index,
                               MountError* error) const {
  *error = MOUNT_ERROR_NONE;

  if (!homedirs_->GetValidKeyset(credentials, vault_keyset, index, error))
    return false;
  *serialized = vault_keyset->serialized();

  // Calling EnsureTpm here handles the case where a user logged in while
  // cryptohome was taking TPM ownership.  In that case, their vault keyset
  // would be scrypt-wrapped and the TPM would not be connected.  If we're
  // configured to use the TPM, calling EnsureTpm will try to connect, and
  // if successful, the call to has_tpm() below will succeed, allowing
  // re-wrapping (migration) using the TPM.
  if (use_tpm_) {
    crypto_->EnsureTpm(false);
  }

  // If the vault keyset's TPM state is not the same as that configured for
  // the device, re-save the keyset (this will save in the device's default
  // method).
  // In the table below: X = true, - = false, * = any value
  //
  //                 1   2   3   4   5   6   7   8   9
  // should_tpm      X   X   X   X   -   -   -   *   X
  //
  // pcr_bound       -   X   *   -   -   *   -   *   -
  //
  // tpm_wrapped     -   X   X   -   -   X   -   X   *
  //
  // scrypt_wrapped  -   -   -   X   -   -   X   X   *
  //
  // scrypt_derived  *   X   -   *   *   *   *   *   *
  //
  // migrate         Y   N   Y   Y   Y   Y   N   Y   Y
  //
  // If the vault keyset is signature-challenge protected, we should not
  // re-encrypt it at all (that is unnecessary).
  const unsigned crypt_flags = serialized->flags();
  bool pcr_bound =
      (crypt_flags & SerializedVaultKeyset::PCR_BOUND) != 0;
  bool tpm_wrapped =
      (crypt_flags & SerializedVaultKeyset::TPM_WRAPPED) != 0;
  bool scrypt_wrapped =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_WRAPPED) != 0;
  bool scrypt_derived =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_DERIVED) != 0;
  bool is_signature_challenge_protected =
      (crypt_flags & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) != 0;
  bool should_tpm = (crypto_->has_tpm() && use_tpm_ &&
                     crypto_->is_cryptohome_key_loaded() &&
                     !is_signature_challenge_protected);
  bool is_le_credential =
      (crypt_flags & SerializedVaultKeyset::LE_CREDENTIAL) != 0;
  bool can_unseal_with_user_auth = crypto_->CanUnsealWithUserAuth();
  do {
    if (is_signature_challenge_protected)
      break;
    // If the keyset was TPM-wrapped, but there was no public key hash,
    // always re-save.  Otherwise, check the table.
    if (serialized->has_tpm_public_key_hash() || is_le_credential) {
      if (is_le_credential && !crypto_->NeedsPcrBinding(serialized->le_label()))
        break;
      if (tpm_wrapped && should_tpm && scrypt_derived && !scrypt_wrapped) {
        if ((pcr_bound && can_unseal_with_user_auth) ||
            (!pcr_bound && !can_unseal_with_user_auth)) {
          break;  // 2
        }
      }
      if (scrypt_wrapped && !should_tpm && !tpm_wrapped)
        break;  // 7
    }
    LOG(INFO) << "Migrating keyset " << *index << ": should_tpm=" << should_tpm
              << ", has_hash=" << serialized->has_tpm_public_key_hash()
              << ", flags=" << crypt_flags << ", pcr_bound=" << pcr_bound
              << ", can_unseal_with_user_auth=" << can_unseal_with_user_auth;
    // This is not considered a fatal error.  Re-saving with the desired
    // protection is ideal, but not required.
    SerializedVaultKeyset new_serialized;
    new_serialized.CopyFrom(*serialized);
    if (ReEncryptVaultKeyset(credentials, *index, vault_keyset,
                             &new_serialized)) {
      serialized->CopyFrom(new_serialized);
    }
  } while (false);

  return true;
}

bool Mount::AddVaultKeyset(const Credentials& credentials,
                           VaultKeyset* vault_keyset,
                           SerializedVaultKeyset* serialized) const {
  // We don't do passkey to wrapper conversion because it is salted during save
  SecureBlob passkey;
  credentials.GetPasskey(&passkey);

  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  if (credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    vault_keyset->mutable_serialized()->set_flags(
        vault_keyset->serialized().flags() |
        SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  }

  // Encrypt the vault keyset
  const auto salt =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  if (!crypto_->EncryptVaultKeyset(*vault_keyset, passkey, salt,
                                   obfuscated_username, serialized)) {
    LOG(ERROR) << "Encrypting vault keyset failed";
    return false;
  }

  return true;
}

bool Mount::ReEncryptVaultKeyset(const Credentials& credentials,
                                 int key_index,
                                 VaultKeyset* vault_keyset,
                                 SerializedVaultKeyset* serialized) const {
  std::string obfuscated_username =
    credentials.GetObfuscatedUsername(system_salt_);
  std::vector<FilePath> files(2);
  files[0] = GetUserSaltFileForUser(obfuscated_username, key_index);
  files[1] = GetUserLegacyKeyFileForUser(obfuscated_username, key_index);
  if (!CacheOldFiles(files)) {
    LOG(ERROR) << "Couldn't cache old key material.";
    return false;
  }
  uint64_t label = serialized->le_label();
  if (!AddVaultKeyset(credentials, vault_keyset, serialized)) {
    LOG(ERROR) << "Couldn't add keyset.";
    RevertCacheFiles(files);
    return false;
  }

  if ((serialized->flags() & SerializedVaultKeyset::LE_CREDENTIAL) != 0) {
    if (!crypto_->RemoveLECredential(label)) {
      // This is non-fatal error.
      LOG(ERROR) << "Failed to remove label = " << label;
    }
  }

  // Note that existing legacy keysets are not automatically annotated.
  // All _new_ interfaces that support KeyData will implicitly translate
  // master.<index> to label=<kKeyLegacyFormat,index> for checking on
  // label uniqueness.  This means that we will still be able to use the
  // lack of KeyData in the future as input to migration.
  if (!StoreVaultKeysetForUser(
        credentials.GetObfuscatedUsername(system_salt_),
        key_index, *serialized)) {
    LOG(ERROR) << "Write to master key failed";
    RevertCacheFiles(files);
    return false;
  }
  DeleteCacheFiles(files);
  return true;
}

bool Mount::MountGuestCryptohome() {
  CHECK(boot_lockbox_ || !use_tpm_);
  if (boot_lockbox_ && !boot_lockbox_->FinalizeBoot()) {
    LOG(WARNING) << "Failed to finalize boot lockbox.";
  }

  if (!pre_mount_callback_.is_null()) {
    pre_mount_callback_.Run();
  }

  current_user_->Reset();

  EphemeralMountHelperInterface* ephemeral_mounter = nullptr;
  base::Closure cleanup;

  if (mount_guest_session_out_of_process_) {
    // Ephemeral cryptohomes for Guest sessions are mounted out-of-process.
    ephemeral_mounter = out_of_process_mounter_.get();
    // This callback will be executed in the destructor at the latest so
    // |out_of_process_mounter_| will always be valid.
    cleanup = base::Bind(&OutOfProcessMountHelper::TearDownEphemeralMount,
                         base::Unretained(out_of_process_mounter_.get()));
  } else {
    ephemeral_mounter = mounter_.get();
    // This callback will be executed in the destructor at the latest so
    // |this| will always be valid.
    cleanup = base::Bind(
        [](Mount* m) {
          m->UnmountAndDropKeys();
          m->CleanUpEphemeral();
        },
        base::Unretained(this));
  }
  return MountEphemeralCryptohome(kGuestUserName, ephemeral_mounter,
                                  std::move(cleanup));
}

FilePath Mount::GetUserDirectory(
    const Credentials& credentials) const {
  return GetUserDirectoryForUser(
      credentials.GetObfuscatedUsername(system_salt_));
}

FilePath Mount::GetUserDirectoryForUser(
    const std::string& obfuscated_username) const {
  return shadow_root_.Append(obfuscated_username);
}

FilePath Mount::GetUserSaltFileForUser(
    const std::string& obfuscated_username, int index) const {
  return GetUserLegacyKeyFileForUser(obfuscated_username, index)
      .AddExtension("salt");
}

FilePath Mount::GetUserLegacyKeyFileForUser(
    const std::string& obfuscated_username, int index) const {
  DCHECK(index < kKeyFileMax && index >= 0);
  return shadow_root_.Append(obfuscated_username)
                     .Append(kKeyFile)
                     .AddExtension(base::IntToString(index));
}

// This is the new planned format for keyfile storage.
FilePath Mount::GetUserKeyFileForUser(
    const std::string& obfuscated_username, const std::string& label) const {
  DCHECK(!label.empty());
  // SHA1 is not for any other purpose than to provide a reasonably
  // collision-resistant, fixed length, path-safe file suffix.
  std::string digest = base::SHA1HashString(label);
  std::string safe_label = base::HexEncode(digest.c_str(), digest.length());
  return shadow_root_.Append(obfuscated_username)
                     .Append(kKeyFile).AddExtension(safe_label);
}

FilePath Mount::GetUserTemporaryMountDirectory(
    const std::string& obfuscated_username) const {
  return mounter_->GetUserTemporaryMountDirectory(obfuscated_username);
}

bool Mount::CheckChapsDirectory(const FilePath& dir,
                                const FilePath& legacy_dir) {
  const Platform::Permissions kChapsDirPermissions = {
    chaps_user_,                 // chaps
    default_access_group_,       // chronos-access
    S_IRWXU | S_IRGRP | S_IXGRP  // 0750
  };
  const Platform::Permissions kChapsFilePermissions = {
    chaps_user_,                 // chaps
    default_access_group_,       // chronos-access
    S_IRUSR | S_IWUSR | S_IRGRP  // 0640
  };
  const Platform::Permissions kChapsSaltPermissions = {
    0,                 // root
    0,                 // root
    S_IRUSR | S_IWUSR  // 0600
  };

  // If the Chaps database directory does not exist, create it.
  if (!platform_->DirectoryExists(dir)) {
    if (platform_->DirectoryExists(legacy_dir)) {
      LOG(INFO) << "Moving chaps directory from " << legacy_dir.value()
                << " to " << dir.value();
      if (!platform_->CopyWithPermissions(legacy_dir, dir)) {
        return false;
      }
      if (!platform_->DeleteFile(legacy_dir, true)) {
        PLOG(WARNING) << "Failed to clean up " << legacy_dir.value();
        return false;
      }
    } else {
      if (!platform_->CreateDirectory(dir)) {
        LOG(ERROR) << "Failed to create " << dir.value();
        return false;
      }
      if (!platform_->SetOwnership(dir,
                                   kChapsDirPermissions.user,
                                   kChapsDirPermissions.group,
                                   true)) {
        LOG(ERROR) << "Couldn't set file ownership for " << dir.value();
        return false;
      }
      if (!platform_->SetPermissions(dir, kChapsDirPermissions.mode)) {
        LOG(ERROR) << "Couldn't set permissions for " << dir.value();
        return false;
      }
    }
    return true;
  }
  // Directory already exists so check permissions and log a warning
  // if not as expected then attempt to apply correct permissions.
  std::map<FilePath, Platform::Permissions> special_cases;
  special_cases[dir.Append("auth_data_salt")] = kChapsSaltPermissions;
  if (!platform_->ApplyPermissionsRecursive(dir,
                                            kChapsFilePermissions,
                                            kChapsDirPermissions,
                                            special_cases)) {
    LOG(ERROR) << "Chaps permissions failure.";
    return false;
  }
  return true;
}

bool Mount::InsertPkcs11Token() {
  std::string username = current_user_->username();
  FilePath token_dir = homedirs_->GetChapsTokenDir(username);
  FilePath legacy_token_dir = homedirs_->GetLegacyChapsTokenDir(username);
  if (!CheckChapsDirectory(token_dir, legacy_token_dir))
    return false;
  // We may create a salt file and, if so, we want to restrict access to it.
  brillo::ScopedUmask scoped_umask(kDefaultUmask);

  // Derive authorization data for the token from the passkey.
  FilePath salt_file = homedirs_->GetChapsTokenSaltPath(username);

  std::unique_ptr<chaps::TokenManagerClient> chaps_client(
      chaps_client_factory_->New());

  // If migration is required, send it before the login event.
  if (is_pkcs11_passkey_migration_required_) {
    LOG(INFO) << "Migrating authorization data.";
    SecureBlob old_auth_data;
    if (!crypto_->PasskeyToTokenAuthData(legacy_pkcs11_passkey_,
                                         salt_file,
                                         &old_auth_data))
      return false;
    chaps_client->ChangeTokenAuthData(
        token_dir,
        old_auth_data,
        pkcs11_token_auth_data_);
    is_pkcs11_passkey_migration_required_ = false;
    legacy_pkcs11_passkey_.clear();
  }

  Pkcs11Init pkcs11init;
  int slot_id = 0;
  if (!chaps_client->LoadToken(
      IsolateCredentialManager::GetDefaultIsolateCredential(),
      token_dir,
      pkcs11_token_auth_data_,
      pkcs11init.GetTpmTokenLabelForUser(current_user_->username()),
      &slot_id)) {
    LOG(ERROR) << "Failed to load PKCS #11 token.";
    ReportCryptohomeError(kLoadPkcs11TokenFailed);
  }
  pkcs11_token_auth_data_.clear();
  ReportTimerStop(kPkcs11InitTimer);
  return true;
}

void Mount::RemovePkcs11Token() {
  std::string username = current_user_->username();
  FilePath token_dir = homedirs_->GetChapsTokenDir(username);
  std::unique_ptr<chaps::TokenManagerClient> chaps_client(
      chaps_client_factory_->New());
  chaps_client->UnloadToken(
      IsolateCredentialManager::GetDefaultIsolateCredential(),
      token_dir);
}

bool Mount::CacheOldFiles(const std::vector<FilePath>& files) const {
  for (const auto& file : files) {
    FilePath file_bak = file.AddExtension("bak");
    if (platform_->FileExists(file_bak)) {
      if (!platform_->DeleteFile(file_bak, false)) {
        return false;
      }
    }
    if (platform_->FileExists(file)) {
      if (!platform_->Move(file, file_bak)) {
        return false;
      }
    }
  }
  return true;
}

bool Mount::RevertCacheFiles(const std::vector<FilePath>& files) const {
  for (const auto& file : files) {
    FilePath file_bak = file.AddExtension("bak");
    if (platform_->FileExists(file_bak)) {
      if (!platform_->Move(file_bak, file)) {
        return false;
      }
    }
  }
  return true;
}

bool Mount::DeleteCacheFiles(const std::vector<FilePath>& files) const {
  for (const auto& file : files) {
    FilePath file_bak = file.AddExtension("bak");
    if (platform_->FileExists(file_bak)) {
      if (!platform_->DeleteFile(file_bak, false)) {
        return false;
      }
    }
  }
  return true;
}

void Mount::GetUserSalt(const Credentials& credentials, bool force,
                        int key_index, SecureBlob* salt) const {
  FilePath path(GetUserSaltFileForUser(
                  credentials.GetObfuscatedUsername(system_salt_),
                  key_index));
  crypto_->GetOrCreateSalt(path, CRYPTOHOME_DEFAULT_SALT_LENGTH, force, salt);
}

std::unique_ptr<base::Value> Mount::GetStatus() {
  std::string user;
  SerializedVaultKeyset keyset;
  auto dv = std::make_unique<base::DictionaryValue>();
  current_user_->GetObfuscatedUsername(&user);
  auto keysets = std::make_unique<base::ListValue>();
  std::vector<int> key_indices;
  if (user.length() && homedirs_->GetVaultKeysets(user, &key_indices)) {
    for (auto key_index : key_indices) {
      auto keyset_dict = std::make_unique<base::DictionaryValue>();
      if (LoadVaultKeysetForUser(user, key_index, &keyset)) {
        bool tpm = keyset.flags() & SerializedVaultKeyset::TPM_WRAPPED;
        bool scrypt = keyset.flags() & SerializedVaultKeyset::SCRYPT_WRAPPED;
        keyset_dict->SetBoolean("tpm", tpm);
        keyset_dict->SetBoolean("scrypt", scrypt);
        keyset_dict->SetBoolean("ok", true);
        keyset_dict->SetInteger("last_activity",
                                keyset.last_activity_timestamp());
        if (keyset.has_key_data()) {
          // TODO(wad) Add additional KeyData
          keyset_dict->SetString("label", keyset.key_data().label());
        }
      } else {
        keyset_dict->SetBoolean("ok", false);
      }
      // TODO(wad) Replace key_index use with key_label() use once
      //           legacy keydata is populated.
      if (mount_type_ != MountType::EPHEMERAL &&
          key_index == current_user_->key_index())
        keyset_dict->SetBoolean("current", true);
      keyset_dict->SetInteger("index", key_index);
      keysets->Append(std::move(keyset_dict));
    }
  }
  dv->Set("keysets", std::move(keysets));
  dv->SetBoolean("mounted", IsMounted());
  std::string obfuscated_owner;
  homedirs_->GetOwner(&obfuscated_owner);
  dv->SetString("owner", obfuscated_owner);
  dv->SetBoolean("enterprise", enterprise_owned_);

  std::string mount_type_string;
  switch (mount_type_) {
    case MountType::NONE:
      mount_type_string = "none";
      break;
    case MountType::ECRYPTFS:
      mount_type_string = "ecryptfs";
      break;
    case MountType::DIR_CRYPTO:
      mount_type_string = "dircrypto";
      break;
    case MountType::EPHEMERAL:
      mount_type_string = "ephemeral";
      break;
  }
  dv->SetString("type", mount_type_string);

  return std::move(dv);
}

bool Mount::SetUserCreds(const Credentials& credentials, int key_index) {
  if (!current_user_->SetUser(credentials))
    return false;
  current_user_->set_key_index(key_index);
  return true;
}

bool Mount::MigrateToDircrypto(
    const dircrypto_data_migrator::MigrationHelper::ProgressCallback& callback,
    MigrationType migration_type) {
  std::string obfuscated_username;
  current_user_->GetObfuscatedUsername(&obfuscated_username);
  FilePath temporary_mount =
      GetUserTemporaryMountDirectory(obfuscated_username);
  if (!IsMounted() || mount_type_ != MountType::DIR_CRYPTO ||
      !platform_->DirectoryExists(temporary_mount) ||
      !mounter_->IsPathMounted(temporary_mount)) {
    LOG(ERROR) << "Not mounted for eCryptfs->dircrypto migration.";
    return false;
  }
  // Do migration.
  constexpr uint64_t kMaxChunkSize = 128 * 1024 * 1024;
  dircrypto_data_migrator::MigrationHelper migrator(
      platform_,
      temporary_mount,
      mount_point_,
      GetUserDirectoryForUser(obfuscated_username),
      kMaxChunkSize,
      migration_type);
  {  // Abort if already cancelled.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    if (is_dircrypto_migration_cancelled_)
      return false;
    CHECK(!active_dircrypto_migrator_);
    active_dircrypto_migrator_ = &migrator;
  }
  bool success = migrator.Migrate(callback);
  UnmountAndDropKeys();
  {  // Signal the waiting thread.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    active_dircrypto_migrator_ = nullptr;
    dircrypto_migration_stopped_condition_.Signal();
  }
  if (!success) {
    LOG(ERROR) << "Failed to migrate.";
    return false;
  }
  // Clean up.
  FilePath vault_path =
      homedirs_->GetEcryptfsUserVaultPath(obfuscated_username);
  if (!platform_->DeleteFile(temporary_mount, true /* recursive */) ||
      !platform_->DeleteFile(vault_path, true /* recursive */)) {
    LOG(ERROR) << "Failed to delete the old vault.";
    return false;
  }
  return true;
}

void Mount::MaybeCancelActiveDircryptoMigrationAndWait() {
  base::AutoLock lock(active_dircrypto_migrator_lock_);
  is_dircrypto_migration_cancelled_ = true;
  while (active_dircrypto_migrator_) {
    active_dircrypto_migrator_->Cancel();
    LOG(INFO) << "Waiting for dircrypto migration to stop.";
    dircrypto_migration_stopped_condition_.Wait();
    LOG(INFO) << "Dircrypto migration stopped.";
  }
}

bool Mount::IsShadowOnly() const { return shadow_only_; }

// TODO(chromium:795310): include all side-effects and move out of mount.cc.
// Sign-in/sign-out effects hook.
// Performs actions that need to follow a mount/unmount operation as a part of
// user sign-in/sign-out.
// Parameters:
//   |mount| - the mount instance that was just mounted/unmounted.
//   |tpm| - the TPM instance.
//   |is_mount| - true for mount operation, false for unmount.
//   |is_owner| - true if mounted for an owner user, false otherwise.
// Returns true if successful, false otherwise.
bool Mount::UserSignInEffects(bool is_mount, bool is_owner) {
  Tpm* tpm = crypto_->get_tpm();
  if (!tpm) {
    return true;
  }

  Tpm::UserType user_type =
      (is_mount & is_owner) ? Tpm::UserType::Owner : Tpm::UserType::NonOwner;
  return tpm->SetUserType(user_type);
}

}  // namespace cryptohome
