// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/bind.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/cryptohome.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <unordered_map>
#include <vector>

#include "cryptohome/bootlockbox/boot_lockbox_client.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_challenge_service_impl.h"
#include "cryptohome/obfuscated_username.h"
#include "cryptohome/tpm.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/userdataauth.h"

using base::FilePath;
using brillo::Blob;
using brillo::SecureBlob;

namespace cryptohome {

const char kMountThreadName[] = "MountThread";
const char kPublicMountSaltFilePath[] = "/var/lib/public_mount_salt";

namespace {
// Some utility functions used by UserDataAuth.

// Get the Account ID for an AccountIdentifier proto.
const std::string& GetAccountId(const AccountIdentifier& id) {
  if (id.has_account_id()) {
    return id.account_id();
  }
  return id.email();
}

// If any of the authorization data contained in the key have a secret that is
// wrapped, then return true. Otherwise, false is returned.
bool KeyHasWrappedAuthorizationSecrets(const Key& k) {
  for (const KeyAuthorizationData& auth_data : k.data().authorization_data()) {
    for (const KeyAuthorizationSecret& secret : auth_data.secrets()) {
      // If wrapping becomes richer in the future, this may change.
      if (secret.wrapped())
        return true;
    }
  }
  return false;
}

// Convert MountError used by mount.cc to CryptohomeErrorCode defined in the
// protos.
user_data_auth::CryptohomeErrorCode MountErrorToCryptohomeError(
    const MountError code) {
  static const std::unordered_map<MountError,
                                  user_data_auth::CryptohomeErrorCode>
      error_code_lut = {
          {MOUNT_ERROR_FATAL, user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL},
          {MOUNT_ERROR_KEY_FAILURE,
           user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED},
          {MOUNT_ERROR_MOUNT_POINT_BUSY,
           user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY},
          {MOUNT_ERROR_TPM_COMM_ERROR,
           user_data_auth::CRYPTOHOME_ERROR_TPM_COMM_ERROR},
          {MOUNT_ERROR_TPM_DEFEND_LOCK,
           user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK},
          {MOUNT_ERROR_USER_DOES_NOT_EXIST,
           user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND},
          {MOUNT_ERROR_TPM_NEEDS_REBOOT,
           user_data_auth::CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT},
          {MOUNT_ERROR_OLD_ENCRYPTION,
           user_data_auth::CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION},
          {MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE,
           user_data_auth::
               CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE},
          {MOUNT_ERROR_RECREATED, user_data_auth::CRYPTOHOME_ERROR_NOT_SET}};

  if (error_code_lut.count(code) != 0) {
    return error_code_lut.at(code);
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

}  // namespace

UserDataAuth::UserDataAuth()
    : origin_thread_id_(base::PlatformThread::CurrentId()),
      mount_thread_(kMountThreadName),
      disable_threading_(false),
      system_salt_(),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      default_crypto_(new Crypto(platform_)),
      crypto_(default_crypto_.get()),
      default_chaps_client_(new chaps::TokenManagerClient()),
      chaps_client_(default_chaps_client_.get()),
      default_pkcs11_init_(new Pkcs11Init()),
      pkcs11_init_(default_pkcs11_init_.get()),
      firmware_management_parameters_(nullptr),
      default_install_attrs_(new cryptohome::InstallAttributes(NULL)),
      install_attrs_(default_install_attrs_.get()),
      enterprise_owned_(false),
      reported_pkcs11_init_fail_(false),
      default_homedirs_(new cryptohome::HomeDirs()),
      homedirs_(default_homedirs_.get()),
      user_timestamp_cache_(new UserOldestActivityTimestampCache()),
      default_mount_factory_(new cryptohome::MountFactory()),
      mount_factory_(default_mount_factory_.get()),
      public_mount_salt_(),
      guest_user_(brillo::cryptohome::home::kGuestUserName),
      force_ecryptfs_(true),
      legacy_mount_(true),
      default_arc_disk_quota_(new cryptohome::ArcDiskQuota(
          homedirs_, platform_, base::FilePath(kArcDiskHome))),
      arc_disk_quota_(default_arc_disk_quota_.get()) {}

UserDataAuth::~UserDataAuth() {
  mount_thread_.Stop();
}

bool UserDataAuth::Initialize() {
  AssertOnOriginThread();

  if (!disable_threading_) {
    // Note that |origin_task_runner_| is initialized here because in some cases
    // such as unit testing, the current thread Task Runner might not be
    // available, so we should not attempt to retrieve the current thread task
    // runner during the creation of this class.
    origin_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  }

  // Note that we check to see if |tpm_| is available here because it may have
  // been set to an overridden value during unit testing before Initialize() is
  // called.
  if (!tpm_) {
    tpm_ = Tpm::GetSingleton();
  }

  // Note that we check to see if |tpm_init_| is available here because it may
  // have been set to an overridden value during unit testing before
  // Initialize() is called.
  if (!tpm_init_) {
    default_tpm_init_.reset(new TpmInit(tpm_, platform_));
    tpm_init_ = default_tpm_init_.get();
  }

  // Initialize Firmware Management Parameters
  if (!firmware_management_parameters_) {
    default_firmware_management_params_.reset(
        new FirmwareManagementParameters(tpm_));
    firmware_management_parameters_ = default_firmware_management_params_.get();
  }

  crypto_->set_use_tpm(true);
  if (!crypto_->Init(tpm_init_)) {
    return false;
  }

  if (!homedirs_->Init(platform_, crypto_, user_timestamp_cache_.get())) {
    return false;
  }

  if (!homedirs_->GetSystemSalt(&system_salt_)) {
    return false;
  }

  // Initialize ARC Disk Quota Service.
  arc_disk_quota_->Initialize();

  // If the TPM is unowned or doesn't exist, it's safe for
  // this function to be called again. However, it shouldn't
  // be called across multiple threads in parallel.
  InitializeInstallAttributes();

  if (!disable_threading_) {
    base::Thread::Options options;
    options.message_loop_type = base::MessageLoop::TYPE_IO;
    mount_thread_.StartWithOptions(options);
  }

  // Clean up any unreferenced mountpoints at startup.
  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(
                            [](UserDataAuth* userdataauth) {
                              userdataauth->CleanUpStaleMounts(false);
                            },
                            base::Unretained(this)));

  // We expect |tpm_| and |tpm_init_| to be available by this point.
  DCHECK(tpm_ && tpm_init_);

  tpm_init_->Init(
      base::Bind(&UserDataAuth::OwnershipCallback, base::Unretained(this)));

  return true;
}

bool UserDataAuth::PostTaskToOriginThread(
    const base::Location& from_here, base::OnceClosure task) {
  if (disable_threading_) {
    std::move(task).Run();
    return true;
  }
  return origin_task_runner_->PostTask(from_here, std::move(task));
}

bool UserDataAuth::PostTaskToMountThread(
    const base::Location& from_here, base::OnceClosure task) {
  if (disable_threading_) {
    std::move(task).Run();
    return true;
  }
  return mount_thread_.task_runner()->PostTask(from_here, std::move(task));
}

bool UserDataAuth::IsMounted(const std::string& username,
                             bool* is_ephemeral_out) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  bool is_mounted = false;
  bool is_ephemeral = false;
  if (username.empty()) {
    // No username is specified, so we consider "the cryptohome" to be mounted
    // if any existing cryptohome is mounted.
    for (const auto& mount_pair : mounts_) {
      if (mount_pair.second->IsMounted()) {
        is_mounted = true;
        is_ephemeral |= !mount_pair.second->IsNonEphemeralMounted();
      }
    }
  } else {
    // A username is specified, check the associated mount object.
    scoped_refptr<cryptohome::Mount> mount = GetMountForUser(username);

    if (mount.get()) {
      is_mounted = mount->IsMounted();
      is_ephemeral = is_mounted && !mount->IsNonEphemeralMounted();
    }
  }

  if (is_ephemeral_out) {
    *is_ephemeral_out = is_ephemeral;
  }

  return is_mounted;
}

scoped_refptr<cryptohome::Mount> UserDataAuth::GetMountForUser(
    const std::string& username) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  scoped_refptr<cryptohome::Mount> mount = nullptr;
  if (mounts_.count(username) == 1) {
    mount = mounts_[username];
  }
  return mount;
}

bool UserDataAuth::RemoveAllMounts(bool unmount) {
  AssertOnMountThread();

  bool success = true;
  for (auto it = mounts_.begin(); it != mounts_.end();) {
    scoped_refptr<cryptohome::Mount> mount = it->second;
    if (unmount && mount->IsMounted()) {
      if (mount->pkcs11_state() == cryptohome::Mount::kIsBeingInitialized) {
        // Reset the state.
        mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);
        // And also reset the global failure reported state.
        reported_pkcs11_init_fail_ = false;
      }
      success = success && mount->UnmountCryptohome();
    }
    mounts_.erase(it++);
  }
  return success;
}

bool UserDataAuth::FilterActiveMounts(
    std::multimap<const FilePath, const FilePath>* mounts,
    std::multimap<const FilePath, const FilePath>* active_mounts,
    bool force) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  bool skipped = false;
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
      for (const auto& mount_pair : mounts_) {
        if (mount_pair.second->OwnsMountPoint(match->second)) {
          keep = true;
          break;
        }
      }

      // Optionally, ignore mounts with open files.
      if (!force) {
        std::vector<ProcessInformation> processes;
        platform_->GetProcessesWithOpenFiles(match->second, &processes);
        if (processes.size()) {
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " has active holders.";
          keep = true;
          skipped = true;
        }
      }
    }
    // Delete anything that shouldn't be unmounted.
    if (keep) {
      active_mounts->insert(curr, match);
      mounts->erase(curr, match);
    }
  }
  return skipped;
}

void UserDataAuth::GetEphemeralLoopDevicesMounts(
    std::multimap<const FilePath, const FilePath>* mounts) {
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
  SecureBlob isolate =
      chaps::IsolateCredentialManager::GetDefaultIsolateCredential();
  std::vector<std::string> tokens;
  if (!chaps_client_->GetTokenList(isolate, &tokens))
    return false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] != chaps::kSystemTokenPath &&
        !PrefixPresent(exclude, tokens[i])) {
      // It's not a system token and is not under one of the excluded path.
      LOG(INFO) << "Unloading up PKCS #11 token: " << tokens[i];
      chaps_client_->UnloadToken(isolate, FilePath(tokens[i]));
    }
  }
  return true;
}

bool UserDataAuth::CleanUpStaleMounts(bool force) {
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

  // Active mounts that we don't intend to unmount.
  std::multimap<const FilePath, const FilePath> active_mounts;

  // Retrieve all the mounts that's currently mounted by the kernel and concerns
  // us
  platform_->GetMountsBySourcePrefix(homedirs_->shadow_root(), &shadow_mounts);
  GetEphemeralLoopDevicesMounts(&ephemeral_mounts);

  // Remove mounts that we've a record of or have open files on them
  bool skipped = FilterActiveMounts(&shadow_mounts, &active_mounts, force) ||
                 FilterActiveMounts(&ephemeral_mounts, &active_mounts, force);

  // Unload PKCS#11 tokens on any mount that we're going to unmount.
  std::vector<FilePath> excluded_mount_points;
  for (const auto& mount : active_mounts) {
    excluded_mount_points.push_back(mount.second);
  }
  UnloadPkcs11Tokens(excluded_mount_points);

  // Unmount anything left.
  for (const auto& match : shadow_mounts) {
    LOG(WARNING) << "Lazily unmounting stale shadow mount: "
                 << match.second.value() << " from " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
  }
  for (const auto& match : ephemeral_mounts) {
    LOG(WARNING) << "Lazily unmounting stale ephemeral mount: "
                 << match.second.value() << " from " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
    // Clean up destination directory for ephemeral mounts under ephemeral
    // cryptohome dir.
    if (base::StartsWith(match.first.value(), kLoopPrefix,
                         base::CompareCase::SENSITIVE) &&
        FilePath(kEphemeralCryptohomeDir).IsParent(match.second)) {
      platform_->DeleteFile(match.second, true /* recursive */);
    }
  }

  // Clean up all stale sparse files, this is comprised of two stages:
  // 1. Clean up stale loop devices.
  // 2. Clean up stale sparse files.
  // Note that some mounts are backed by loop devices, and loop devices are
  // backed by sparse files.

  std::vector<Platform::LoopDevice> loop_devices =
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
        ReportCryptohomeError(kEphemeralCleanUpFailed);
        PLOG(ERROR) << "Can't detach stale loop: " << device.device.value();
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
    if (!platform_->DeleteFile(file, false /* recursive */)) {
      ReportCryptohomeError(kEphemeralCleanUpFailed);
      PLOG(ERROR) << "Failed to clean up ephemeral sparse file: "
                  << file.value();
    }
  }

  // |force| and |skipped| cannot be true at the same time. If |force| is true,
  // then we'll not skip over any stale mount because there are open files, so
  // |skipped| must be false.
  DCHECK(!(force && skipped));

  return skipped;
}

bool UserDataAuth::PrefixPresent(const std::vector<FilePath>& prefixes,
                                 const std::string path) {
  return std::any_of(
      prefixes.begin(), prefixes.end(), [&path](const FilePath& prefix) {
        return base::StartsWith(path, prefix.value(),
                                base::CompareCase::INSENSITIVE_ASCII);
      });
}

bool UserDataAuth::Unmount() {
  bool unmount_ok = RemoveAllMounts(true);

  // If there are any unexpected mounts lingering from a crash/restart,
  // clean them up now.
  // Note that we do not care about the return value of CleanUpStaleMounts()
  // because it doesn't matter if any mount is skipped due to open files, and
  // additionally, since we've specified force=true, it'll not skip over mounts
  // with open files.
  CleanUpStaleMounts(true);

  return unmount_ok;
}

void UserDataAuth::InitializePkcs11(cryptohome::Mount* mount) {
  // We should not pass nullptr to this method.
  DCHECK(mount);

  if (!IsOnMountThread()) {
    // We are not on mount thread, but to be safe, we'll only access Mount
    // objects on mount thread, so let's post ourself there.
    PostTaskToMountThread(
        FROM_HERE,
        base::BindOnce(&UserDataAuth::InitializePkcs11, base::Unretained(this),
                       base::Unretained(mount)));
    return;
  }

  AssertOnMountThread();

  // Wait for ownership if there is a working TPM.
  if (tpm_ && tpm_->IsEnabled() && !tpm_->IsOwned()) {
    LOG(WARNING) << "TPM was not owned. TPM initialization call back will"
                 << " handle PKCS#11 initialization.";
    mount->set_pkcs11_state(cryptohome::Mount::kIsWaitingOnTPM);
    return;
  }

  bool still_mounted = false;

  // The mount have be mounted, that is, still tracked by cryptohome. Otherwise
  // there's no point in initializing PKCS#11 for it. The reason for this check
  // is because it might be possible for Unmount() to be called after mounting
  // and before getting here.
  for (const auto& mount_pair : mounts_) {
    if (mount_pair.second.get() == mount && mount->IsMounted()) {
      still_mounted = true;
      break;
    }
  }

  if (!still_mounted) {
    LOG(WARNING)
        << "PKCS#11 initialization requested but cryptohome is not mounted.";
    return;
  }

  mount->set_pkcs11_state(cryptohome::Mount::kIsBeingInitialized);

  // Note that the timer stops in the Mount class' method.
  ReportTimerStart(kPkcs11InitTimer);

  mount->InsertPkcs11Token();

  LOG(INFO) << "PKCS#11 initialization succeeded.";

  mount->set_pkcs11_state(cryptohome::Mount::kIsInitialized);
}

void UserDataAuth::ResumeAllPkcs11Initialization() {
  if (!IsOnMountThread()) {
    // We are not on mount thread, but to be safe, we'll only access Mount
    // objects on mount thread, so let's post ourself there.
    PostTaskToMountThread(
        FROM_HERE, base::BindOnce(&UserDataAuth::ResumeAllPkcs11Initialization,
                                  base::Unretained(this)));
    return;
  }

  for (const auto& mount_pair : mounts_) {
    cryptohome::Mount* mount = mount_pair.second.get();
    if (mount->pkcs11_state() == cryptohome::Mount::kIsWaitingOnTPM) {
      InitializePkcs11(mount);
    }
  }
}

void UserDataAuth::ResetAllTPMContext() {
  if (!IsOnMountThread()) {
    // We are not on mount thread, but to be safe, we'll only access Mount
    // objects on mount thread, so let's post ourself there.
    PostTaskToMountThread(FROM_HERE,
                          base::BindOnce(&UserDataAuth::ResetAllTPMContext,
                                         base::Unretained(this)));
    return;
  }

  for (const auto& mount_pair : mounts_) {
    if (mount_pair.second) {
      Crypto* crypto = mount_pair.second->crypto();
      if (crypto) {
        crypto->EnsureTpm(true);
      }
    }
  }
}

void UserDataAuth::OwnershipCallback(bool status, bool took_ownership) {
  if (took_ownership) {
    // Reset the TPM context of all mounts, that is, force a reload of
    // cryptohome keys, and make sure it is loaded and ready for every mount.
    PostTaskToMountThread(FROM_HERE,
                          base::BindOnce(&UserDataAuth::ResetAllTPMContext,
                                         base::Unretained(this)));

    // There might be some mounts that is half way through the PKCS#11
    // initialization, let's resume them.
    PostTaskToMountThread(
        FROM_HERE, base::BindOnce(&UserDataAuth::ResumeAllPkcs11Initialization,
                                  base::Unretained(this)));

    // Initialize the install-time locked attributes since we can't do it prior
    // to ownership.
    PostTaskToOriginThread(
        FROM_HERE, base::BindOnce(&UserDataAuth::InitializeInstallAttributes,
                                  base::Unretained(this)));

    // If we mounted before the TPM finished initialization, we must finalize
    // the install attributes now too, otherwise it takes a full re-login cycle
    // to finalize.
    PostTaskToMountThread(
        FROM_HERE,
        base::BindOnce(&UserDataAuth::FinalizeInstallAttributesIfMounted,
                       base::Unretained(this)));
  }
}

void UserDataAuth::SetEnterpriseOwned(bool enterprise_owned) {
  AssertOnMountThread();

  enterprise_owned_ = enterprise_owned;
  for (const auto& mount_pair : mounts_) {
    mount_pair.second->set_enterprise_owned(enterprise_owned);
  }
  homedirs_->set_enterprise_owned(enterprise_owned);
}

void UserDataAuth::DetectEnterpriseOwnership() {
  AssertOnOriginThread();

  static const std::string true_str = "true";
  brillo::Blob true_value(true_str.begin(), true_str.end());
  true_value.push_back(0);

  brillo::Blob value;
  if (install_attrs_->Get("enterprise.owned", &value) && value == true_value) {
    // Update any active mounts with the state, have to be done on mount thread.
    PostTaskToOriginThread(FROM_HERE,
                           base::BindOnce(&UserDataAuth::SetEnterpriseOwned,
                                          base::Unretained(this), true));
  }
  // Note: Right now there's no way to convert an enterprise owned machine to a
  // non-enterprise owned machine without clearing the TPM, so we don't try
  // calling SetEnterpriseOwned() with false.
}

void UserDataAuth::InitializeInstallAttributes() {
  AssertOnOriginThread();

  // The TPM owning instance may have changed since initialization.
  // InstallAttributes can handle a NULL or !IsEnabled Tpm object.
  install_attrs_->SetTpm(tpm_);
  install_attrs_->Init(tpm_init_);

  // Check if the machine is enterprise owned and report to mount_ then.
  DetectEnterpriseOwnership();
}

void UserDataAuth::FinalizeInstallAttributesIfMounted() {
  AssertOnMountThread();

  bool is_mounted = IsMounted();
  if (is_mounted &&
      install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
    bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
    if (!guest_mounted) {
      PostTaskToOriginThread(FROM_HERE,
                             base::BindOnce(
                                 [](UserDataAuth* userdataauth) {
                                   userdataauth->install_attrs_->Finalize();
                                 },
                                 base::Unretained(this)));
    }
  }
}

bool UserDataAuth::CreatePublicMountSaltIfNeeded() {
  if (!public_mount_salt_.empty())
    return true;
  FilePath saltfile(kPublicMountSaltFilePath);
  return crypto_->GetOrCreateSalt(saltfile, CRYPTOHOME_DEFAULT_SALT_LENGTH,
                                  false, &public_mount_salt_);
}

bool UserDataAuth::GetPublicMountPassKey(const std::string& public_mount_id,
                                         std::string* public_mount_passkey) {
  if (!CreatePublicMountSaltIfNeeded())
    return false;
  SecureBlob passkey;
  Crypto::PasswordToPasskey(public_mount_id.c_str(), public_mount_salt_,
                            &passkey);
  *public_mount_passkey = passkey.to_string();
  return true;
}

bool UserDataAuth::GetShouldMountAsEphemeral(
    const std::string& account_id,
    bool is_ephemeral_mount_requested,
    bool has_create_request,
    bool* is_ephemeral,
    user_data_auth::CryptohomeErrorCode* error) const {
  const bool is_or_will_be_owner = homedirs_->IsOrWillBeOwner(account_id);
  if (is_ephemeral_mount_requested && is_or_will_be_owner) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when the user "
                  "is not the owner.";
    *error = user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }
  *is_ephemeral =
      !is_or_will_be_owner &&
      (homedirs_->AreEphemeralUsersEnabled() || is_ephemeral_mount_requested);
  if (*is_ephemeral && !has_create_request) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when its "
                  "creation on-the-fly is allowed.";
    *error =
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
    return false;
  }
  return true;
}

scoped_refptr<cryptohome::Mount> UserDataAuth::GetOrCreateMountForUser(
    const std::string& username) {
  // This method touches the |mounts_| object so it needs to run on
  // |mount_thread_|
  AssertOnMountThread();

  scoped_refptr<cryptohome::Mount> m;
  if (mounts_.count(username) == 0U) {
    // We don't have a mount associated with |username|, let's create one with
    // the |mount_factory_|.
    m = mount_factory_->New();
    m->Init(platform_, crypto_, user_timestamp_cache_.get(),
            base::BindRepeating(&UserDataAuth::PreMountCallback,
                                base::Unretained(this)));
    m->set_enterprise_owned(enterprise_owned_);
    m->set_legacy_mount(legacy_mount_);
    mounts_[username] = m;
  } else {
    // A mount is already associated with |username|, so return it directly.
    m = mounts_[username];
  }
  return m;
}

void UserDataAuth::PreMountCallback() {
#if USE_TPM2
  // Lock NVRamBootLockbox
  auto nvram_boot_lockbox_client = BootLockboxClient::CreateBootLockboxClient();
  if (!nvram_boot_lockbox_client) {
    LOG(WARNING) << "Failed to create nvram_boot_lockbox_client";
    return;
  }

  if (!nvram_boot_lockbox_client->Finalize()) {
    LOG(WARNING) << "Failed to finalize nvram lockbox.";
  }
#endif  // USE_TMP2
}

bool UserDataAuth::CleanUpHiddenMounts() {
  AssertOnMountThread();

  bool ok = true;
  for (auto it = mounts_.begin(); it != mounts_.end();) {
    scoped_refptr<cryptohome::Mount> mount = it->second;
    if (mount->IsMounted() && mount->IsShadowOnly()) {
      ok = ok && mount->UnmountCryptohome();
      it = mounts_.erase(it);
    } else {
      ++it;
    }
  }
  return ok;
}

void UserDataAuth::GetChallengeCredentialsPcrRestrictions(
    const std::string& obfuscated_username,
    std::vector<std::map<uint32_t, brillo::Blob>>* pcr_restrictions) {
  {
    std::map<uint32_t, brillo::Blob> pcrs_1;
    for (const auto& pcr : crypto_->GetPcrMap(obfuscated_username,
                                              false /* use_extended_pcr */)) {
      pcrs_1[pcr.first] = brillo::BlobFromString(pcr.second);
    }
    pcr_restrictions->push_back(pcrs_1);
  }
  {
    std::map<uint32_t, brillo::Blob> pcrs_2;
    for (const auto& pcr :
         crypto_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */)) {
      pcrs_2[pcr.first] = brillo::BlobFromString(pcr.second);
    }
    pcr_restrictions->push_back(pcrs_2);
  }
}

bool UserDataAuth::RemoveMountForUser(const std::string& username) {
  AssertOnMountThread();

  if (mounts_.count(username) != 0) {
    return (1U == mounts_.erase(username));
  }
  return true;
}

void UserDataAuth::MountGuest(
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  if (mounts_.size() != 0) {
    LOG(WARNING) << "Guest mount requested with other mounts active.";
  }
  // Rather than make it safe to check the size, then clean up, just always
  // clean up.
  bool ok = RemoveAllMounts(true);
  // Create a ref-counted guest mount for async use and then throw it away.
  scoped_refptr<cryptohome::Mount> guest_mount =
      GetOrCreateMountForUser(guest_user_);
  user_data_auth::MountReply reply;
  if (!ok) {
    LOG(ERROR) << "Could not unmount cryptohomes for Guest use";
    if (!RemoveMountForUser(guest_user_)) {
      LOG(ERROR) << "Unexpectedly cannot drop unused Guest mount from map.";
    }
    reply.set_error(user_data_auth::CryptohomeErrorCode::
                        CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    std::move(on_done).Run(reply);
    return;
  }
  ReportTimerStart(kAsyncGuestMountTimer);

  if (!guest_mount->MountGuestCryptohome()) {
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  // TODO(b/137073669): Cleanup guest_mount if mount failed.
  std::move(on_done).Run(reply);
}

void UserDataAuth::DoMount(
    user_data_auth::MountRequest request,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  // DoMount current supports guest login/mount, normal plaintext password login
  // and challenge response login. For guest mount, a special process
  // (MountGuest()) is used. Meanwhile, for normal plaintext password login and
  // challenge response login, both will flow through this method. This method
  // generally does some parameter sanity checking, then pass the request onto
  // ContinueMountWithCredentials() for plaintext password login and
  // DoChallengeResponseMount() for challenge response login.
  // DoChallengeResponseMount() will contact a dbus service and transmit the
  // challenge, and once the response is received and checked with the TPM,
  // it'll pass the request to ContinueMountWithCredentials(), which is the same
  // as password login case, and in ContinueMountWithCredentials(), the mount is
  // actually mounted through system call.

  // Check for guest mount case.
  if (request.guest_mount()) {
    MountGuest(std::move(on_done));
    return;
  }

  user_data_auth::MountReply reply;

  // At present, we only enforce non-empty email addresses.
  // In the future, we may wish to canonicalize if we don't move
  // to requiring a IdP-unique identifier.
  const std::string& account_id = GetAccountId(request.account());

  // Check for empty account ID
  if (account_id.empty()) {
    LOG(ERROR) << "No email supplied";
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  if (request.public_mount()) {
    // Public mount have a set of passkey/password that is generated directly
    // from the username (and a local system salt.)
    std::string public_mount_passkey;
    if (!GetPublicMountPassKey(account_id, &public_mount_passkey)) {
      LOG(ERROR) << "Could not get public mount passkey.";
      reply.set_error(user_data_auth::CryptohomeErrorCode::
                          CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
      std::move(on_done).Run(reply);
      return;
    }

    // Set the secret as the key for cryptohome authorization/creation.
    request.mutable_authorization()->mutable_key()->set_secret(
        public_mount_passkey);
    if (request.has_create()) {
      request.mutable_create()->mutable_keys(0)->set_secret(
          public_mount_passkey);
    }
  }

  // We do not allow empty password, except for challenge response type login.
  if (request.authorization().key().secret().empty() &&
      request.authorization().key().data().type() !=
          KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    LOG(ERROR) << "No key secret supplied";
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  if (request.has_create()) {
    // copy_authorization_key in CreateRequest means that we'll copy the
    // authorization request's key and use it as if it's the key specified in
    // CreateRequest.
    if (request.create().copy_authorization_key()) {
      Key* auth_key = request.mutable_create()->add_keys();
      *auth_key = request.authorization().key();
      // Don't allow a key creation and mount if the key lacks
      // the privileges.
      if (!auth_key->data().privileges().mount()) {
        reply.set_error(user_data_auth::CryptohomeErrorCode::
                            CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED);
        std::move(on_done).Run(reply);
        return;
      }
    }

    // Sanity check for |request.create.keys|.
    int keys_size = request.create().keys_size();
    if (keys_size == 0) {
      LOG(ERROR) << "CreateRequest supplied with no keys";
      reply.set_error(user_data_auth::CryptohomeErrorCode::
                          CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      std::move(on_done).Run(reply);
      return;
    } else if (keys_size > 1) {
      LOG(INFO) << "MountEx: unimplemented CreateRequest with multiple keys";
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
      std::move(on_done).Run(reply);
      return;
    } else {
      const Key key = request.create().keys(0);
      // TODO(wad) Ensure the labels are all unique.
      if (!key.has_data() || key.data().label().empty() ||
          (key.secret().empty() &&
           key.data().type() != KeyData::KEY_TYPE_CHALLENGE_RESPONSE)) {
        LOG(ERROR) << "CreateRequest Keys are not fully specified";
        reply.set_error(user_data_auth::CryptohomeErrorCode::
                            CRYPTOHOME_ERROR_INVALID_ARGUMENT);
        std::move(on_done).Run(reply);
        return;
      }
      if (KeyHasWrappedAuthorizationSecrets(key)) {
        LOG(ERROR) << "KeyAuthorizationSecrets may not be wrapped";
        reply.set_error(user_data_auth::CryptohomeErrorCode::
                            CRYPTOHOME_ERROR_INVALID_ARGUMENT);
        std::move(on_done).Run(reply);
        return;
      }
    }
  }

  // Determine whether the mount should be ephemeral.
  bool is_ephemeral = false;
  user_data_auth::CryptohomeErrorCode mount_error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  if (!GetShouldMountAsEphemeral(account_id, request.require_ephemeral(),
                                 request.has_create(), &is_ephemeral,
                                 &mount_error)) {
    reply.set_error(mount_error);
    std::move(on_done).Run(reply);
    return;
  }

  // MountArgs is a set of parameters that we'll be passing around to
  // ContinueMountWithCredentials() and DoChallengeResponseMount().
  Mount::MountArgs mount_args;
  mount_args.create_if_missing = request.has_create();
  mount_args.is_ephemeral = is_ephemeral;
  mount_args.create_as_ecryptfs =
      force_ecryptfs_ ||
      (request.has_create() && request.create().force_ecryptfs());
  mount_args.to_migrate_from_ecryptfs = request.to_migrate_from_ecryptfs();
  // Force_ecryptfs_ wins.
  mount_args.force_dircrypto =
      !force_ecryptfs_ && request.force_dircrypto_if_available();
  mount_args.shadow_only = request.hidden_mount();

  // Process challenge-response credentials asynchronously.
  if (request.authorization().key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    DoChallengeResponseMount(request, mount_args, std::move(on_done));
    return;
  }

  auto credentials = std::make_unique<Credentials>(
      account_id.c_str(), SecureBlob(request.authorization().key().secret()));
  // Everything else can be the default.
  credentials->set_key_data(request.authorization().key().data());

  ContinueMountWithCredentials(request, std::move(credentials), mount_args,
                               std::move(on_done));
  return;
}

void UserDataAuth::DoChallengeResponseMount(
    const user_data_auth::MountRequest& request,
    const Mount::MountArgs& mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  DCHECK_EQ(request.authorization().key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;

  if (!tpm_) {
    LOG(ERROR) << "Cannot do challenge-response mount without TPM";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }
  if (!tpm_init_->IsTpmReady()) {
    LOG(ERROR)
        << "TPM must be initialized in order to do challenge-response mount";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  if (!challenge_credentials_helper_) {
    // Lazily create the helper object that manages generation/decryption of
    // credentials for challenge-protected vaults.
    Blob delegate_blob, delegate_secret;

    bool has_reset_lock_permissions = false;
    // TPM Delegate is required for TPM1.2. For TPM2.0, this is a no-op.
    if (!tpm_->GetDelegate(&delegate_blob, &delegate_secret,
                           &has_reset_lock_permissions)) {
      LOG(ERROR) << "Cannot do challenge-response mount without TPM delegate";
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
      std::move(on_done).Run(reply);
      return;
    }

    challenge_credentials_helper_ =
        std::make_unique<ChallengeCredentialsHelper>(tpm_, delegate_blob,
                                                     delegate_secret);
  }

  const std::string& account_id = GetAccountId(request.account());
  const std::string obfuscated_username =
      BuildObfuscatedUsername(account_id, system_salt_);
  const KeyData key_data = request.authorization().key().data();

  if (!request.authorization().has_key_delegate() ||
      !request.authorization().key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response mount without key delegate "
                  "information";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  // KeyChallengeServiceImpl is tasked with contacting the challenge response
  // DBus service that'll provide the response once we send the challenge.
  auto key_challenge_service = std::make_unique<KeyChallengeServiceImpl>(
      bus_, request.authorization().key_delegate().dbus_service_name());

  if (!homedirs_->Exists(obfuscated_username) &&
      !mount_args.create_if_missing) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    std::move(on_done).Run(reply);
    return;
  }

  std::unique_ptr<VaultKeyset> vault_keyset(homedirs_->GetVaultKeyset(
      obfuscated_username, request.authorization().key().data().label()));
  const bool use_existing_credentials =
      vault_keyset && !mount_args.is_ephemeral;
  // If the home directory already exist (and thus the corresponding encrypted
  // VaultKeyset exists) and the mount is not ephemeral, then we'll use the
  // ChallengeCredentialsHelper (which handles challenge response
  // authentication) to decrypt the VaultKeyset.
  if (use_existing_credentials) {
    // Home directory already exist and we are not doing ephemeral mount, so
    // we'll decrypt existing VaultKeyset.
    challenge_credentials_helper_->Decrypt(
        account_id, key_data,
        vault_keyset->serialized().signature_challenge_info(),
        std::move(key_challenge_service),
        base::BindOnce(
            &UserDataAuth::OnChallengeResponseMountCredentialsObtained,
            base::Unretained(this), request, mount_args, std::move(on_done)));
  } else {
    // We'll create a new VaultKeyset that accepts challenge response
    // authentication.
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "No existing challenge-response vault keyset found";
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
      std::move(on_done).Run(reply);
      return;
    }

    std::vector<std::map<uint32_t, brillo::Blob>> pcr_restrictions;
    GetChallengeCredentialsPcrRestrictions(obfuscated_username,
                                           &pcr_restrictions);
    challenge_credentials_helper_->GenerateNew(
        account_id, key_data, pcr_restrictions,
        std::move(key_challenge_service),
        base::BindOnce(
            &UserDataAuth::OnChallengeResponseMountCredentialsObtained,
            base::Unretained(this), request, mount_args, std::move(on_done)));
  }
}

void UserDataAuth::OnChallengeResponseMountCredentialsObtained(
    const user_data_auth::MountRequest& request,
    const Mount::MountArgs mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done,
    std::unique_ptr<Credentials> credentials) {
  // If we get here, that means the ChallengeCredentialsHelper have finished the
  // process of doing challenge response authentication, either successful or
  // otherwise.

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;

  DCHECK_EQ(request.authorization().key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  if (!credentials) {
    // Challenge response authentication have failed.
    LOG(ERROR) << "Could not mount due to failure to obtain challenge-response "
                  "credentials";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  DCHECK_EQ(credentials->key_data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  ContinueMountWithCredentials(request, std::move(credentials), mount_args,
                               std::move(on_done));
}

void UserDataAuth::ContinueMountWithCredentials(
    const user_data_auth::MountRequest& request,
    std::unique_ptr<Credentials> credentials,
    const Mount::MountArgs& mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  CleanUpHiddenMounts();

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;

  // This is safe even if cryptohomed restarts during a multi-mount
  // session and a new mount is added because cleanup is not forced.
  // An existing process will keep the mount alive.  On the next
  // Unmount() it'll be forcibly cleaned up.  In the case that
  // cryptohomed crashes and misses the Unmount call, the stale
  // mountpoints should still be cleaned up on the next daemon
  // interaction.
  //
  // As we introduce multiple mounts, we can consider API changes to
  // make it clearer what the UI expectations are (AddMount, etc).
  bool other_mounts_active = true;
  if (mounts_.size() == 0) {
    other_mounts_active = CleanUpStaleMounts(false);
    // This could run on every interaction to catch any unused mounts.
  }

  // If the home directory for our user doesn't exist and we aren't instructed
  // to create the home directory, and reply with the error.
  if (!request.has_create() &&
      !homedirs_->Exists(credentials->GetObfuscatedUsername(system_salt_))) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    std::move(on_done).Run(reply);
    return;
  }

  std::string account_id = GetAccountId(request.account());
  // Provide an authoritative filesystem-sanitized username.
  reply.set_sanitized_username(
      brillo::cryptohome::home::SanitizeUserName(account_id));

  // While it would be cleaner to implement the privilege enforcement
  // here, that can only be done if a label was supplied.  If a wildcard
  // was supplied, then we can only perform the enforcement after the
  // matching key is identified.
  //
  // See Mount::MountCryptohome for privilege checking.

  // Check if the guest user is mounted, if it is, we can't proceed.
  scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
  bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
  // TODO(wad,ellyjones) Change this behavior to return failure even
  // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
  if (guest_mounted && !guest_mount->UnmountCryptohome()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    std::move(on_done).Run(reply);
    return;
  }

  scoped_refptr<cryptohome::Mount> user_mount =
      GetOrCreateMountForUser(account_id);

  if (request.hidden_mount() && user_mount->IsMounted()) {
    LOG(ERROR) << "Hidden mount requested, but mount already exists.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    std::move(on_done).Run(reply);
    return;
  }

  // For public mount, don't proceed if there is any existing mount or stale
  // mount. Exceptionally, it is normal and ok to have a failed previous mount
  // attempt for the same user.
  const bool only_self_unmounted_attempt =
      mounts_.size() == 1 && !user_mount->IsMounted();
  if (request.public_mount() && other_mounts_active &&
      !only_self_unmounted_attempt) {
    LOG(ERROR) << "Public mount requested with other mounts active.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    std::move(on_done).Run(reply);
    return;
  }

  // Don't overlay an ephemeral mount over a file-backed one.
  if (mount_args.is_ephemeral && user_mount->IsNonEphemeralMounted()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_mount->UnmountCryptohome()) {
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
      std::move(on_done).Run(reply);
      return;
    }
  }

  // If a user's home directory is already mounted, then we'll just recheck its
  // credential with what's cached in memory. This is much faster than going to
  // the TPM.
  if (user_mount->IsMounted()) {
    LOG(INFO) << "Mount exists. Rechecking credentials.";
    // Attempt a short-circuited credential test.
    if (user_mount->AreSameUser(*credentials) &&
        user_mount->AreValid(*credentials)) {
      std::move(on_done).Run(reply);
      homedirs_->ResetLECredentials(*credentials);
      return;
    }
    // If the Mount has invalid credentials (repopulated from system state)
    // this will ensure a user can still sign-in with the right ones.
    // TODO(wad) Should we unmount on a failed re-mount attempt?
    if (!user_mount->AreValid(*credentials) &&
        !homedirs_->AreCredentialsValid(*credentials)) {
      reply.set_error(
          user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    } else {
      homedirs_->ResetLECredentials(*credentials);
    }
    std::move(on_done).Run(reply);
    return;
  }

  // Any non-guest mount attempt triggers InstallAttributes finalization.
  // The return value is ignored as it is possible we're pre-ownership.
  // The next login will assure finalization if possible.
  if (install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    install_attrs_->Finalize();
  }

  // As per the other timers, this really only tracks time spent in
  // MountCryptohome() not in the other areas prior.
  ReportTimerStart(kMountExTimer);

  MountError code = MOUNT_ERROR_NONE;
  // Does actual mounting here.
  bool status = user_mount->MountCryptohome(*credentials, mount_args, &code);

  // PKCS#11 always starts out uninitialized right after a fresh mount.
  user_mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);

  // Mark the timer as done.
  ReportTimerStop(kMountExTimer);

  if (!status) {
    reply.set_error(MountErrorToCryptohomeError(code));
  }
  if (code == MOUNT_ERROR_RECREATED) {
    // MOUNT_ERROR_RECREATED is not actually an error, so we'll not reply with
    // an error. Instead, we'll set the recreated flag to true.
    reply.set_recreated(true);
  }
  if (status) {
    homedirs_->ResetLECredentials(*credentials);
  }

  std::move(on_done).Run(reply);

  // Update user timestamp and kick off PKCS#11 initialization for non-hidden
  // mount.
  if (!request.hidden_mount()) {
    // Update user activity timestamp to be able to detect old users.
    // This action is not mandatory, so we perform it after
    // CryptohomeMount() returns, in background.
    user_mount->UpdateCurrentUserActivityTimestamp(0);
    // Time to push the task for PKCS#11 initialization.
    // TODO(wad) This call will PostTask back to the same thread. It is safe,
    //           but it seems pointless.
    InitializePkcs11(user_mount.get());
  }
}

user_data_auth::CryptohomeErrorCode UserDataAuth::AddKey(
    const user_data_auth::AddKeyRequest request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "AddKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "AddKeyRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  if (request.authorization_request().key().secret().empty()) {
    LOG(ERROR) << "No key secret in AddKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (request.key().secret().empty()) {
    LOG(ERROR) << "No new key in AddKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (request.key().data().label().empty()) {
    LOG(ERROR) << "No new key label in AddKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Ensure any new keys do not contain a wrapped authorization key.
  if (KeyHasWrappedAuthorizationSecrets(request.key())) {
    LOG(ERROR)
        << "KeyAuthorizationSecrets may not be wrapped in AddKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  const std::string& auth_key_secret =
      request.authorization_request().key().secret();
  Credentials credentials(account_id.c_str(), SecureBlob(auth_key_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  // An integer for AddKeyset to write the resulting index. This is discarded in
  // the end.
  int unused_keyset_index;

  const std::string& new_key_secret = request.key().secret();
  SecureBlob new_secret(new_key_secret);
  CryptohomeErrorCode result;
  result =
      homedirs_->AddKeyset(credentials, new_secret, &request.key().data(),
                           request.clobber_if_exists(), &unused_keyset_index);

  // Note that cryptohome::CryptohomeErrorCode and
  // user_data_auth::CryptohomeErrorCode are same in content, and it'll remain
  // so until the end of the refactor, so we can safely cast from one to
  // another. This is enforced in our unit test.
  return static_cast<user_data_auth::CryptohomeErrorCode>(result);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::CheckKey(
    const user_data_auth::CheckKeyRequest request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "CheckKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "CheckKeyRequest must have valid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  const std::string& auth_secret =
      request.authorization_request().key().secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in CheckKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id.c_str(), SecureBlob(auth_secret));
  credentials.set_key_data(request.authorization_request().key().data());

  bool found_valid_credentials = false;
  for (const auto& mount_pair : mounts_) {
    if (mount_pair.second->AreSameUser(credentials)) {
      found_valid_credentials = mount_pair.second->AreValid(credentials);
      break;
    }
  }

  if (found_valid_credentials) {
    // Entered the right creds, so reset LE credentials.
    homedirs_->ResetLECredentials(credentials);
    return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  } else {
    // Cover different keys for the same user with homedirs.
    if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
      return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
    }

    if (!homedirs_->AreCredentialsValid(credentials)) {
      // TODO(wad) Should this pass along KEY_NOT_FOUND too?
      return user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
    }

    homedirs_->ResetLECredentials(credentials);
    return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  }

  NOTREACHED();
}

user_data_auth::CryptohomeErrorCode UserDataAuth::RemoveKey(
    const user_data_auth::RemoveKeyRequest request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "RemoveKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "RemoveKeyRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  const std::string& auth_secret =
      request.authorization_request().key().secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in RemoveKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (request.key().data().label().empty()) {
    LOG(ERROR) << "No new key label in RemoveKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id.c_str(), SecureBlob(auth_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  CryptohomeErrorCode result;
  result = homedirs_->RemoveKeyset(credentials, request.key().data());

  // Note that cryptohome::CryptohomeErrorCode and
  // user_data_auth::CryptohomeErrorCode are same in content, and it'll remain
  // so until the end of the refactor, so we can safely cast from one to
  // another.
  return static_cast<user_data_auth::CryptohomeErrorCode>(result);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::ListKeys(
    const user_data_auth::ListKeysRequest& request,
    std::vector<std::string>* labels_out) {
  AssertOnMountThread();
  DCHECK(labels_out);

  if (!request.has_account_id()) {
    LOG(ERROR) << "ListKeysRequest must have account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  const std::string& account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "ListKeysRequest must have valid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  const std::string obfuscated_username =
      BuildObfuscatedUsername(account_id, system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  if (!homedirs_->GetVaultKeysetLabels(obfuscated_username, labels_out)) {
    return user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::GetKeyData(
    const user_data_auth::GetKeyDataRequest& request,
    cryptohome::KeyData* data_out,
    bool* found) {
  if (!request.has_account_id()) {
    // Note that authorization request is currently not required.
    LOG(ERROR) << "GetKeyDataRequest must have account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetKeyDataRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (!request.has_key()) {
    LOG(ERROR) << "No key attributes provided in GetKeyDataRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  const std::string obfuscated_username =
      BuildObfuscatedUsername(account_id, system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  // Requests only support using the key label at present.
  std::unique_ptr<VaultKeyset> vk(homedirs_->GetVaultKeyset(
      obfuscated_username, request.key().data().label()));
  if (vk) {
    *data_out = vk->serialized().key_data();

    // Clear any symmetric KeyAuthorizationSecrets even if they are wrapped.
    for (int a = 0; a < data_out->authorization_data_size(); ++a) {
      KeyAuthorizationData* auth_data = data_out->mutable_authorization_data(a);
      for (int s = 0; s < auth_data->secrets_size(); ++s) {
        auth_data->mutable_secrets(s)->clear_symmetric_key();
        auth_data->mutable_secrets(s)->set_wrapped(false);
      }
    }

    *found = true;
  } else {
    // No error is thrown if there is no match.
    *found = false;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::UpdateKey(
    const user_data_auth::UpdateKeyRequest& request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "UpdateKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "UpdateKeyRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  auto& auth_key = request.authorization_request().key();
  const std::string& auth_secret = auth_key.secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in UpdateKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Any undefined field in changes() will be left as it is.
  if (!request.has_changes()) {
    LOG(ERROR) << "No updates requested in UpdateKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (KeyHasWrappedAuthorizationSecrets(request.changes())) {
    LOG(ERROR)
        << "KeyAuthorizationSecrets may not be wrapped in UpdateKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id.c_str(), SecureBlob(auth_secret));
  credentials.set_key_data(auth_key.data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  cryptohome::CryptohomeErrorCode result = homedirs_->UpdateKeyset(
      credentials, &request.changes(), request.authorization_signature());

  // Note that cryptohome::CryptohomeErrorCode and
  // user_data_auth::CryptohomeErrorCode are same in content, and it'll remain
  // so until the end of the refactor, so we can safely cast from one to
  // another.
  return static_cast<user_data_auth::CryptohomeErrorCode>(result);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::MigrateKey(
    const user_data_auth::MigrateKeyRequest& request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "MigrateKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "MigrateKeyRequest must have valid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id.c_str(), SecureBlob(request.secret()));

  scoped_refptr<cryptohome::Mount> mount = GetMountForUser(account_id);
  if (!homedirs_->Migrate(
          credentials,
          SecureBlob(request.authorization_request().key().secret()), mount)) {
    return user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::Remove(
    const user_data_auth::RemoveRequest& request) {
  AssertOnMountThread();

  if (!request.has_identifier()) {
    LOG(ERROR) << "RemoveRequest must have identifier.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.identifier());
  if (account_id.empty()) {
    LOG(ERROR) << "RemoveRequest must have valid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (!homedirs_->Remove(request.identifier().account_id())) {
    return user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED;
  }
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::Rename(
    const user_data_auth::RenameRequest& request) {
  AssertOnMountThread();

  if (!request.has_id_from() || !request.has_id_to()) {
    LOG(ERROR) << "RenameRequest must have id_from and id_to.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string username_from = GetAccountId(request.id_from());
  std::string username_to = GetAccountId(request.id_to());

  scoped_refptr<cryptohome::Mount> mount = GetMountForUser(username_from);
  const bool is_mounted = mount.get() && mount->IsMounted();

  if (is_mounted) {
    LOG(ERROR) << "RenameCryptohome('" << username_from << "','" << username_to
               << "'): Unable to rename mounted cryptohome.";
    return user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY;
  } else if (!homedirs_) {
    LOG(ERROR) << "RenameCryptohome('" << username_from << "','" << username_to
               << "'): Homedirs not initialized.";
    return user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY;
  } else if (!homedirs_->Rename(username_from, username_to)) {
    return user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

void UserDataAuth::StartMigrateToDircrypto(
    const user_data_auth::StartMigrateToDircryptoRequest& request,
    base::Callback<void(const user_data_auth::DircryptoMigrationProgress&)>
        progress_callback) {
  AssertOnMountThread();

  MigrationType migration_type = request.minimal_migration()
                                     ? MigrationType::MINIMAL
                                     : MigrationType::FULL;

  // Note that total_bytes and current_bytes field in |progress| is discarded by
  // client whenever |progress.status| is not DIRCRYPTO_MIGRATION_IN_PROGRESS,
  // this is why they are left with the default value of 0 here. Please see
  // MigrationHelper::ProgressCallback for more details.
  user_data_auth::DircryptoMigrationProgress progress;

  scoped_refptr<cryptohome::Mount> mount =
      GetMountForUser(GetAccountId(request.account_id()));
  if (!mount.get()) {
    LOG(ERROR) << "StartMigrateToDircrypto: Failed to get mount.";
    progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
    progress_callback.Run(progress);
    return;
  }
  LOG(INFO) << "StartMigrateToDircrypto: Migrating to dircrypto.";
  if (!mount->MigrateToDircrypto(progress_callback, migration_type)) {
    LOG(ERROR) << "StartMigrateToDircrypto: Failed to migrate.";
    progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
    progress_callback.Run(progress);
    return;
  }
  LOG(INFO) << "StartMigrateToDircrypto: Migration done.";
  progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
  progress_callback.Run(progress);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::NeedsDircryptoMigration(
    const cryptohome::AccountIdentifier& account, bool* result) {
  const std::string obfuscated_username =
      BuildObfuscatedUsername(GetAccountId(account), system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    LOG(ERROR) << "Unknown user in NeedsDircryptoMigration.";
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  *result = !force_ecryptfs_ &&
            homedirs_->NeedsDircryptoMigration(obfuscated_username);
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

bool UserDataAuth::IsLowEntropyCredentialSupported() {
  return tpm_->GetLECredentialBackend() &&
         tpm_->GetLECredentialBackend()->IsSupported();
}

int64_t UserDataAuth::GetAccountDiskUsage(
    const cryptohome::AccountIdentifier& account) {
  // Note that if the given |account| is invalid or non-existent, then HomeDirs'
  // implementation of ComputeSize is specified to return 0.
  return homedirs_->ComputeSize(GetAccountId(account));
}

bool UserDataAuth::IsArcQuotaSupported() {
  return arc_disk_quota_->IsQuotaSupported();
}

int64_t UserDataAuth::GetCurrentSpaceForArcUid(uid_t android_uid) {
  return arc_disk_quota_->GetCurrentSpaceForUid(android_uid);
}

int64_t UserDataAuth::GetCurrentSpaceForArcGid(uid_t android_gid) {
  return arc_disk_quota_->GetCurrentSpaceForGid(android_gid);
}

bool UserDataAuth::Pkcs11IsTpmTokenReady() {
  AssertOnMountThread();
  // We touched the mounts_ object, so we need to be on mount thread.

  bool ready = true;
  for (const auto& mount_pair : mounts_) {
    cryptohome::Mount* mount = mount_pair.second.get();
    bool ok = (mount->pkcs11_state() == cryptohome::Mount::kIsInitialized);

    ready = ready && ok;
  }

  return ready;
}

user_data_auth::TpmTokenInfo UserDataAuth::Pkcs11GetTpmTokenInfo(
    const std::string& username) {
  user_data_auth::TpmTokenInfo result;
  std::string label, pin;
  CK_SLOT_ID slot;
  FilePath token_path;
  if (username.empty()) {
    // We want to get the system token.

    // Get the label and pin for system token.
    pkcs11_init_->GetTpmTokenInfo(&label, &pin);

    token_path = FilePath(chaps::kSystemTokenPath);
  } else {
    // We want to get the user token.

    // Get the label and pin for user token.
    pkcs11_init_->GetTpmTokenInfoForUser(username, &label, &pin);

    token_path = homedirs_->GetChapsTokenDir(username);
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
  // We are touching the |mounts_| object so we need to be on mount thread.

  for (const auto& mount_pair : mounts_) {
    mount_pair.second->RemovePkcs11Token();
  }
}

bool UserDataAuth::InstallAttributesGet(const std::string& name,
                                        std::vector<uint8_t>* data_out) {
  return install_attrs_->Get(name, data_out);
}

bool UserDataAuth::InstallAttributesSet(const std::string& name,
                                        const std::vector<uint8_t>& data) {
  return install_attrs_->Set(name, data);
}

bool UserDataAuth::InstallAttributesFinalize() {
  return install_attrs_->Finalize();
}

int UserDataAuth::InstallAttributesCount() {
  return install_attrs_->Count();
}

bool UserDataAuth::InstallAttributesIsSecure() {
  return install_attrs_->is_secure();
}

InstallAttributes::Status UserDataAuth::InstallAttributesGetStatus() {
  return install_attrs_->status();
}

user_data_auth::InstallAttributesState
UserDataAuth::InstallAttributesStatusToProtoEnum(
    InstallAttributes::Status status) {
  static const std::unordered_map<InstallAttributes::Status,
                                  user_data_auth::InstallAttributesState>
      state_map = {{InstallAttributes::Status::kUnknown,
                    user_data_auth::InstallAttributesState::UNKNOWN},
                   {InstallAttributes::Status::kTpmNotOwned,
                    user_data_auth::InstallAttributesState::TPM_NOT_OWNED},
                   {InstallAttributes::Status::kFirstInstall,
                    user_data_auth::InstallAttributesState::FIRST_INSTALL},
                   {InstallAttributes::Status::kValid,
                    user_data_auth::InstallAttributesState::VALID},
                   {InstallAttributes::Status::kInvalid,
                    user_data_auth::InstallAttributesState::INVALID}};
  if (state_map.count(status) != 0) {
    return state_map.at(status);
  }

  NOTREACHED();
  // Return is added so compiler doesn't complain.
  return user_data_auth::InstallAttributesState::INVALID;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::GetFirmwareManagementParameters(
    user_data_auth::FirmwareManagementParameters* fwmp) {
  if (!firmware_management_parameters_->Load()) {
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  uint32_t flags;
  if (firmware_management_parameters_->GetFlags(&flags)) {
    fwmp->set_flags(flags);
  } else {
    LOG(WARNING)
        << "Failed to GetFlags() for GetFirmwareManagementParameters().";
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  std::vector<uint8_t> hash;
  if (firmware_management_parameters_->GetDeveloperKeyHash(&hash)) {
    *fwmp->mutable_developer_key_hash() = {hash.begin(), hash.end()};
  } else {
    LOG(WARNING) << "Failed to GetDeveloperKeyHash() for "
                    "GetFirmwareManagementParameters().";
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::SetFirmwareManagementParameters(
    const user_data_auth::FirmwareManagementParameters& fwmp) {
  if (!firmware_management_parameters_->Create()) {
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  uint32_t flags = fwmp.flags();
  std::unique_ptr<std::vector<uint8_t>> hash;

  if (!fwmp.developer_key_hash().empty()) {
    hash.reset(new std::vector<uint8_t>(fwmp.developer_key_hash().begin(),
                                        fwmp.developer_key_hash().end()));
  }

  if (!firmware_management_parameters_->Store(flags, hash.get())) {
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

bool UserDataAuth::RemoveFirmwareManagementParameters() {
  return firmware_management_parameters_->Destroy();
}

const brillo::SecureBlob& UserDataAuth::GetSystemSalt() {
  DCHECK_NE(system_salt_.size(), 0)
      << "Cannot call GetSystemSalt before initialization";
  return system_salt_;
}

bool UserDataAuth::UpdateCurrentUserActivityTimestamp(int time_shift_sec) {
  AssertOnMountThread();
  // We are touching the mount object, so we'll need to be on mount thread.

  bool success = true;
  for (const auto& mount_pair : mounts_) {
    success &=
        mount_pair.second->UpdateCurrentUserActivityTimestamp(time_shift_sec);
  }

  return success;
}

}  // namespace cryptohome
