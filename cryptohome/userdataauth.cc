// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/notreached.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/cryptohome.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <chromeos/constants/cryptohome.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_session.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/bootlockbox/boot_lockbox_client.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/cleanup/disk_cleanup.h"
#include "cryptohome/cleanup/low_disk_space_handler.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptohome_rsa_key_loader.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/pkcs11/real_pkcs11_token_factory.h"
#include "cryptohome/signature_sealing/structures_proto.h"
#include "cryptohome/stateful_recovery.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/tpm.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session.h"
#include "cryptohome/userdataauth.h"

using base::FilePath;
using brillo::Blob;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;
using hwsec::StatusChain;
using hwsec::TPMErrorBase;

namespace cryptohome {

constexpr char kMountThreadName[] = "MountThread";
constexpr char kNotFirstBootFilePath[] = "/run/cryptohome/not_first_boot";
constexpr char kDeviceMapperDevicePrefix[] = "/dev/mapper/dmcrypt";

namespace {
// Some utility functions used by UserDataAuth.

// Get the Account ID for an AccountIdentifier proto.
const std::string& GetAccountId(const AccountIdentifier& id) {
  if (id.has_account_id()) {
    return id.account_id();
  }
  return id.email();
}

// Returns whether the Chrome OS image is a test one.
bool IsOsTestImage() {
  std::string chromeos_release_track;
  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                                         &chromeos_release_track)) {
    // Fall back to the safer assumption that we're not in a test image.
    return false;
  }
  return base::StartsWith(chromeos_release_track, "test",
                          base::CompareCase::SENSITIVE);
}

// Whether the key can be used for lightweight challenge-response authentication
// check against the given user session.
bool KeyMatchesForLightweightChallengeResponseCheck(
    const KeyData& key_data, const UserSession& session) {
  DCHECK_EQ(key_data.type(), KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK_EQ(key_data.challenge_response_key_size(), 1);
  if (session.key_data().type() != KeyData::KEY_TYPE_CHALLENGE_RESPONSE ||
      session.key_data().label().empty() ||
      session.key_data().label() != key_data.label())
    return false;
  if (session.key_data().challenge_response_key_size() != 1) {
    // Using multiple challenge-response keys at once is currently unsupported.
    return false;
  }
  if (session.key_data().challenge_response_key(0).public_key_spki_der() !=
      key_data.challenge_response_key(0).public_key_spki_der()) {
    LOG(WARNING) << "Public key mismatch for lightweight challenge-response "
                    "authentication check";
    return false;
  }
  return true;
}

CryptohomeVault::Options MountArgsToVaultOptions(
    const UserDataAuth::MountArgs& mount_args) {
  CryptohomeVault::Options vault_options;
  if (mount_args.force_dircrypto) {
    // If dircrypto is forced, it's an error to mount ecryptfs home unless
    // we are migrating from ecryptfs.
    vault_options.block_ecryptfs = true;
  } else if (mount_args.create_as_ecryptfs) {
    vault_options.force_type = EncryptedContainerType::kEcryptfs;
  }
  vault_options.migrate = mount_args.to_migrate_from_ecryptfs;
  return vault_options;
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
  for (auto match = mounts->begin(); match != mounts->end(); ++match) {
    // Group dmcrypt-<>-data and dmcrypt-<>-cache mounts. Strip out last
    // '-' from the path.
    size_t last_component_index = match->first.value().find_last_of("-");

    if (last_component_index == std::string::npos) {
      continue;
    }

    base::FilePath device_group(
        match->first.value().substr(0, last_component_index));
    grouped_mounts->insert({device_group, match->second});
  }
}

}  // namespace

UserDataAuth::UserDataAuth()
    : origin_thread_id_(base::PlatformThread::CurrentId()),
      mount_thread_(nullptr),
      system_salt_(),
      tpm_(nullptr),
      default_cryptohome_keys_manager_(nullptr),
      cryptohome_keys_manager_(nullptr),
      tpm_manager_util_(nullptr),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      default_crypto_(new Crypto(platform_)),
      crypto_(default_crypto_.get()),
      default_chaps_client_(new chaps::TokenManagerClient()),
      chaps_client_(default_chaps_client_.get()),
      default_pkcs11_init_(new Pkcs11Init()),
      pkcs11_init_(default_pkcs11_init_.get()),
      default_pkcs11_token_factory_(new RealPkcs11TokenFactory()),
      pkcs11_token_factory_(default_pkcs11_token_factory_.get()),
      firmware_management_parameters_(nullptr),
      default_fingerprint_manager_(),
      fingerprint_manager_(nullptr),
      upload_alerts_period_ms_(kUploadAlertsPeriodMS),
      ownership_callback_has_run_(false),
      default_install_attrs_(new cryptohome::InstallAttributes(NULL)),
      install_attrs_(default_install_attrs_.get()),
      enterprise_owned_(false),
      reported_pkcs11_init_fail_(false),
      default_user_activity_timestamp_manager_(
          new UserOldestActivityTimestampManager(platform_)),
      user_activity_timestamp_manager_(
          default_user_activity_timestamp_manager_.get()),
      default_homedirs_(nullptr),
      homedirs_(nullptr),
      default_keyset_management_(nullptr),
      keyset_management_(nullptr),
      default_auth_session_manager_(nullptr),
      auth_session_manager_(nullptr),
      default_low_disk_space_handler_(nullptr),
      low_disk_space_handler_(nullptr),
      disk_cleanup_threshold_(kFreeSpaceThresholdToTriggerCleanup),
      disk_cleanup_aggressive_threshold_(
          kFreeSpaceThresholdToTriggerAggressiveCleanup),
      disk_cleanup_critical_threshold_(
          kFreeSpaceThresholdToTriggerCriticalCleanup),
      disk_cleanup_target_free_space_(kTargetFreeSpaceAfterCleanup),
      default_mount_factory_(new cryptohome::MountFactory()),
      mount_factory_(default_mount_factory_.get()),
      public_mount_salt_(),
      guest_user_(brillo::cryptohome::home::kGuestUserName),
      force_ecryptfs_(true),
      fscrypt_v2_(false),
      legacy_mount_(true),
      bind_mount_downloads_(true),
      default_arc_disk_quota_(nullptr),
      arc_disk_quota_(nullptr) {}

UserDataAuth::~UserDataAuth() {
  if (low_disk_space_handler_) {
    low_disk_space_handler_->Stop();
  }
  if (mount_thread_) {
    mount_thread_->Stop();
  }
}

bool UserDataAuth::Initialize() {
  AssertOnOriginThread();

  // Note that we check to see if |origin_task_runner_| and |mount_task_runner_|
  // are available here because they may have been set to an overridden value
  // during unit testing before Initialize() is called.
  if (!origin_task_runner_) {
    origin_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  }
  if (!mount_task_runner_) {
    mount_thread_ = std::make_unique<MountThread>(kMountThreadName, this);
  }

  // Note that we check to see if |tpm_| is available here because it may have
  // been set to an overridden value during unit testing before Initialize() is
  // called.
  if (!tpm_) {
    tpm_ = Tpm::GetSingleton();
  }

  // Note that we check to see if |cryptohome_keys_manager_| is available here
  // because it may have been set to an overridden value during unit testing
  // before Initialize() is called.
  if (!cryptohome_keys_manager_) {
    default_cryptohome_keys_manager_.reset(
        new CryptohomeKeysManager(tpm_, platform_));
    cryptohome_keys_manager_ = default_cryptohome_keys_manager_.get();
  }

  // Initialize Firmware Management Parameters
  if (!firmware_management_parameters_) {
    default_firmware_management_params_ =
        FirmwareManagementParameters::CreateInstance(tpm_);
    firmware_management_parameters_ = default_firmware_management_params_.get();
  }

  if (!crypto_->Init(tpm_, cryptohome_keys_manager_)) {
    return false;
  }

  if (!InitializeFilesystemLayout(platform_, crypto_, &system_salt_)) {
    LOG(ERROR) << "Failed to initialize filesystem layout.";
    return false;
  }

  if (!keyset_management_) {
    default_keyset_management_ = std::make_unique<KeysetManagement>(
        platform_, crypto_, system_salt_,
        std::make_unique<VaultKeysetFactory>());
    keyset_management_ = default_keyset_management_.get();
  }

  if (!auth_factor_manager_) {
    default_auth_factor_manager_ =
        std::make_unique<AuthFactorManager>(platform_);
    auth_factor_manager_ = default_auth_factor_manager_.get();
  }

  if (!user_secret_stash_storage_) {
    default_user_secret_stash_storage_ =
        std::make_unique<UserSecretStashStorage>(platform_);
    user_secret_stash_storage_ = default_user_secret_stash_storage_.get();
  }

  if (!auth_session_manager_) {
    default_auth_session_manager_ = std::make_unique<AuthSessionManager>(
        keyset_management_, auth_factor_manager_, user_secret_stash_storage_);
    auth_session_manager_ = default_auth_session_manager_.get();
  }

  if (!homedirs_) {
    auto container_factory =
        std::make_unique<EncryptedContainerFactory>(platform_);
    container_factory->set_allow_fscrypt_v2(fscrypt_v2_);
    auto vault_factory = std::make_unique<CryptohomeVaultFactory>(
        platform_, std::move(container_factory));
    // This callback runs in HomeDirs::Remove on |this.homedirs_|. Since
    // |this.keyset_management_| won't be destroyed upon call of Remove(),
    // base::Unretained(keyset_management_) will be valid when the callback
    // runs.
    HomeDirs::RemoveCallback remove_callback =
        base::BindRepeating(&KeysetManagement::RemoveLECredentials,
                            base::Unretained(keyset_management_));
    default_homedirs_ = std::make_unique<HomeDirs>(
        platform_, std::make_unique<policy::PolicyProvider>(), remove_callback,
        std::move(vault_factory));
    homedirs_ = default_homedirs_.get();
  }

  auto homedirs = homedirs_->GetHomeDirs();
  for (const auto& dir : homedirs) {
    // TODO(b/205759690, dlunev): can be changed after a stepping stone release
    //  to `user_activity_timestamp_manager_->LoadTimestamp(dir.obfuscated);`
    base::Time legacy_timestamp =
        keyset_management_->GetKeysetBoundTimestamp(dir.obfuscated);
    user_activity_timestamp_manager_->LoadTimestampWithLegacy(dir.obfuscated,
                                                              legacy_timestamp);
    keyset_management_->CleanupPerIndexTimestampFiles(dir.obfuscated);
  }

  if (!low_disk_space_handler_) {
    default_low_disk_space_handler_ = std::make_unique<LowDiskSpaceHandler>(
        homedirs_, platform_, user_activity_timestamp_manager_);
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

  if (!arc_disk_quota_) {
    default_arc_disk_quota_ = std::make_unique<ArcDiskQuota>(
        homedirs_, platform_, base::FilePath(kArcDiskHome));
    arc_disk_quota_ = default_arc_disk_quota_.get();
  }
  // Initialize ARC Disk Quota Service.
  arc_disk_quota_->Initialize();

  if (!mount_task_runner_) {
    base::Thread::Options options;
    options.message_pump_type = base::MessagePumpType::IO;
    mount_thread_->StartWithOptions(std::move(options));
    mount_task_runner_ = mount_thread_->task_runner();
  }

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

  // We expect |tpm_| and |cryptohome_keys_manager_| to be available by this
  // point.
  DCHECK(tpm_ && cryptohome_keys_manager_);

  // Seed /dev/urandom
  SeedUrandom();

  low_disk_space_handler_->SetUpdateUserActivityTimestampCallback(
      base::BindRepeating(
          base::IgnoreResult(&UserDataAuth::UpdateCurrentUserActivityTimestamp),
          base::Unretained(this), 0));

  low_disk_space_handler_->SetLowDiskSpaceCallback(
      base::BindRepeating([](uint64_t) {}));

  if (!low_disk_space_handler_->Init(base::BindRepeating(
          &UserDataAuth::PostTaskToMountThread, base::Unretained(this))))
    return false;

  // Start scheduling periodic TPM alerts upload to UMA. Subsequent events are
  // scheduled by the callback itself.
  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::UploadAlertsDataCallback,
                                       base::Unretained(this)));

  // Do Stateful Recovery if requested.
  auto mountfn = base::BindRepeating(&UserDataAuth::StatefulRecoveryMount,
                                     base::Unretained(this));
  auto unmountfn = base::BindRepeating(&UserDataAuth::StatefulRecoveryUnmount,
                                       base::Unretained(this));
  auto isownerfn = base::BindRepeating(&UserDataAuth::StatefulRecoveryIsOwner,
                                       base::Unretained(this));
  StatefulRecovery recovery(platform_, mountfn, unmountfn, isownerfn);
  if (recovery.Requested()) {
    if (recovery.Recover()) {
      LOG(INFO) << "Stateful recovery was performed successfully.";
    } else {
      LOG(ERROR) << "Stateful recovery failed.";
    }
    recovery.PerformReboot();
  }
  return true;
}

bool UserDataAuth::StatefulRecoveryMount(const std::string& username,
                                         const std::string& passkey,
                                         FilePath* out_home_path) {
  AssertOnOriginThread();

  user_data_auth::MountRequest mount_req;
  mount_req.mutable_account()->set_account_id(username);
  mount_req.mutable_authorization()->mutable_key()->set_secret(passkey);

  bool mount_path_retrieved = false;
  // This will store the mount_reply when it finished.
  user_data_auth::MountReply mount_reply;
  // This will be used to let code outside of the callback know that we're
  // done.
  base::WaitableEvent done_event(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto on_done = base::BindOnce(
      [](UserDataAuth* uda, std::string username, FilePath* out_home_path,
         bool* mount_path_retrieved,
         user_data_auth::MountReply* mount_reply_ptr,
         base::WaitableEvent* done_event_ptr,
         const user_data_auth::MountReply& reply) {
        *mount_reply_ptr = reply;
        // After the mount is successful, we need to obtain the user
        // mount.
        scoped_refptr<UserSession> user_session = uda->GetUserSession(username);
        if (!user_session || !user_session->IsActive()) {
          LOG(ERROR) << "Failed to get mount in stateful recovery.";
          *mount_path_retrieved = false;
        } else {
          const std::string obfuscated_username =
              brillo::cryptohome::home::SanitizeUserName(username);
          *out_home_path = GetUserMountDirectory(obfuscated_username);
          *mount_path_retrieved = true;
        }
        done_event_ptr->Signal();
      },
      base::Unretained(this), username, base::Unretained(out_home_path),
      base::Unretained(&mount_path_retrieved), base::Unretained(&mount_reply),
      base::Unretained(&done_event));

  PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuth::DoMount, base::Unretained(this),
                                mount_req, std::move(on_done)));

  done_event.Wait();

  if (mount_reply.error()) {
    LOG(ERROR) << "Mount during stateful recovery failed: "
               << mount_reply.error();
    return false;
  }
  if (!mount_path_retrieved) {
    LOG(ERROR) << "Failed to get user home path in stateful recovery.";
    return false;
  }
  LOG(INFO) << "Mount succeeded during stateful recovery.";
  return true;
}

bool UserDataAuth::StatefulRecoveryUnmount() {
  AssertOnOriginThread();

  bool result;
  base::WaitableEvent done_event(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);

  PostTaskToMountThread(
      FROM_HERE, base::BindOnce(
                     [](UserDataAuth* uda, base::WaitableEvent* done_event_ptr,
                        bool* result_ptr) {
                       *result_ptr = uda->Unmount();
                       done_event_ptr->Signal();
                     },
                     base::Unretained(this), base::Unretained(&done_event),
                     base::Unretained(&result)));

  done_event.Wait();
  return result;
}

bool UserDataAuth::StatefulRecoveryIsOwner(const std::string& username) {
  AssertOnOriginThread();

  std::string owner;
  if (homedirs_->GetPlainOwner(&owner) && username.length() &&
      username == owner) {
    return true;
  }
  return false;
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

void UserDataAuth::ShutdownTask() {
  if (mount_thread_bus_) {
    mount_thread_bus_->ShutdownAndBlock();
    mount_thread_bus_.reset();
  }
}

bool UserDataAuth::PostDBusInitialize() {
  AssertOnOriginThread();
  CHECK(bus_);

  if (!tpm_manager_util_) {
    tpm_manager_util_ = tpm_manager::TpmManagerUtility::GetSingleton();
  }

  if (tpm_manager_util_) {
    tpm_manager_util_->AddOwnershipCallback(base::BindRepeating(
        &UserDataAuth::OnOwnershipTakenSignal, base::Unretained(this)));
  } else {
    LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
  }

  // Create a dbus connection on mount thread.
  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::CreateMountThreadDBus,
                                       base::Unretained(this)));

  // If the TPM is unowned or doesn't exist, it's safe for
  // this function to be called again. However, it shouldn't
  // be called across multiple threads in parallel.

  PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuth::InitializeInstallAttributes,
                                base::Unretained(this)));

  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::CreateFingerprintManager,
                                       base::Unretained(this)));

  return true;
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

void UserDataAuth::OnOwnershipTakenSignal() {
  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::OwnershipCallback,
                                       base::Unretained(this), true, true));
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
    if (parallel_task_count_ > 1) {
      ReportParallelTasks(parallel_task_count_);
    }

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

bool UserDataAuth::IsMounted(const std::string& username,
                             bool* is_ephemeral_out) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  bool is_mounted = false;
  bool is_ephemeral = false;
  if (username.empty()) {
    // No username is specified, so we consider "the cryptohome" to be mounted
    // if any existing cryptohome is mounted.
    for (const auto& session_pair : sessions_) {
      if (session_pair.second->IsActive()) {
        is_mounted = true;
        is_ephemeral |= session_pair.second->IsEphemeral();
      }
    }
  } else {
    // A username is specified, check the associated mount object.
    scoped_refptr<UserSession> session = GetUserSession(username);

    if (session.get()) {
      is_mounted = session->IsActive();
      is_ephemeral = is_mounted && session->IsEphemeral();
    }
  }

  if (is_ephemeral_out) {
    *is_ephemeral_out = is_ephemeral;
  }

  return is_mounted;
}

scoped_refptr<UserSession> UserDataAuth::GetUserSession(
    const std::string& username) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  scoped_refptr<UserSession> session = nullptr;
  if (sessions_.count(username) == 1) {
    session = sessions_[username];
  }
  return session;
}

bool UserDataAuth::RemoveAllMounts() {
  AssertOnMountThread();

  bool success = true;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    scoped_refptr<UserSession> session = it->second;
    if (session->IsActive() && !session->Unmount()) {
      success = false;
    }
    sessions_.erase(it++);
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
  std::set<const FilePath> children_to_preserve;

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
      for (const auto& session_pair : sessions_) {
        if (session_pair.second->OwnsMountPoint(match->second)) {
          keep = true;
          // If !include_busy_mount, other mount points not owned scanned after
          // should be preserved as well.
          if (include_busy_mount)
            break;
        }
      }

      // Ignore mounts pointing to children of used mounts.
      if (!include_busy_mount) {
        if (children_to_preserve.find(match->second) !=
            children_to_preserve.end()) {
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
        ExpireMountResult expire_mount_result =
            platform_->ExpireMount(match->second);
        if (expire_mount_result == ExpireMountResult::kBusy) {
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " has active holders.";
          keep = true;
          skipped = true;
        } else if (expire_mount_result == ExpireMountResult::kError) {
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
    if (base::StartsWith(match.first.value(), kLoopPrefix,
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
    if (!platform_->DeleteFile(file)) {
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

bool UserDataAuth::Unmount() {
  AssertOnMountThread();

  bool unmount_ok = RemoveAllMounts();

  // If there are any unexpected mounts lingering from a crash/restart,
  // clean them up now.
  // Note that we do not care about the return value of CleanUpStaleMounts()
  // because it doesn't matter if any mount is skipped due to open files, and
  // additionally, since we've specified force=true, it'll not skip over mounts
  // with open files.
  CleanUpStaleMounts(true);

  if (homedirs_->AreEphemeralUsersEnabled()) {
    homedirs_->RemoveNonOwnerCryptohomes();
  }

  return unmount_ok;
}

void UserDataAuth::InitializePkcs11(UserSession* session) {
  AssertOnMountThread();

  // We should not pass nullptr to this method.
  DCHECK(session);

  // Wait for ownership if there is a working TPM.
  if (tpm_ && tpm_->IsEnabled() && !tpm_->IsOwned()) {
    LOG(WARNING) << "TPM was not owned. TPM initialization call back will"
                 << " handle PKCS#11 initialization.";
    return;
  }

  bool still_mounted = false;

  // The mount has to be mounted, that is, still tracked by cryptohome.
  // Otherwise there's no point in initializing PKCS#11 for it. The reason for
  // this check is because it might be possible for Unmount() to be called after
  // mounting and before getting here.
  for (const auto& session_pair : sessions_) {
    if (session_pair.second.get() == session && session->IsActive()) {
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

void UserDataAuth::ResumeAllPkcs11Initialization() {
  AssertOnMountThread();

  for (auto& session_pair : sessions_) {
    scoped_refptr<UserSession> session = session_pair.second;
    if (!session->GetPkcs11Token() || !session->GetPkcs11Token()->IsReady()) {
      InitializePkcs11(session.get());
    }
  }
}

void UserDataAuth::Pkcs11RestoreTpmTokens() {
  AssertOnMountThread();

  // There is no token needs to resume if TPM isn't ready.
  if (tpm_ && tpm_->IsEnabled() && !tpm_->IsOwned()) {
    return;
  }

  for (auto& session_pair : sessions_) {
    scoped_refptr<UserSession> session = session_pair.second;
    InitializePkcs11(session.get());
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

  AssertOnMountThread();

  crypto_->EnsureTpm(true);
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

void UserDataAuth::SetLowDiskSpaceCallback(
    const base::RepeatingCallback<void(uint64_t)>& callback) {
  low_disk_space_handler_->SetLowDiskSpaceCallback(callback);
}

void UserDataAuth::OwnershipCallback(bool status, bool took_ownership) {
  AssertOnMountThread();

  // Note that this function should only be called once during the lifetime of
  // this process, extra calls will be dropped.
  if (ownership_callback_has_run_) {
    LOG(WARNING) << "Duplicated call to OwnershipCallback.";
    return;
  }
  ownership_callback_has_run_ = true;

  if (took_ownership) {
    // Reset the TPM context of all mounts, that is, force a reload of
    // cryptohome keys, and make sure it is loaded and ready for every mount.
    ResetAllTPMContext();

    // There might be some mounts that is half way through the PKCS#11
    // initialization, let's resume them.
    ResumeAllPkcs11Initialization();

    // Initialize the install-time locked attributes since we can't do it prior
    // to ownership.
    InitializeInstallAttributes();

    // If we mounted before the TPM finished initialization, we must finalize
    // the install attributes now too, otherwise it takes a full re-login cycle
    // to finalize.
    FinalizeInstallAttributesIfMounted();
  }
}

void UserDataAuth::SetEnterpriseOwned(bool enterprise_owned) {
  AssertOnMountThread();

  enterprise_owned_ = enterprise_owned;
  homedirs_->set_enterprise_owned(enterprise_owned);
}

void UserDataAuth::DetectEnterpriseOwnership() {
  AssertOnMountThread();

  static const std::string true_str = "true";
  brillo::Blob true_value(true_str.begin(), true_str.end());
  true_value.push_back(0);

  brillo::Blob value;
  if (install_attrs_->Get("enterprise.owned", &value) && value == true_value) {
    // Update any active mounts with the state, have to be done on mount thread.
    SetEnterpriseOwned(true);
  }
  // Note: Right now there's no way to convert an enterprise owned machine to a
  // non-enterprise owned machine without clearing the TPM, so we don't try
  // calling SetEnterpriseOwned() with false.
}

void UserDataAuth::InitializeInstallAttributes() {
  AssertOnMountThread();

  // Don't reinitialize when install attributes are valid.
  if (install_attrs_->status() == InstallAttributes::Status::kValid) {
    return;
  }

  // The TPM owning instance may have changed since initialization.
  // InstallAttributes can handle a NULL or !IsEnabled Tpm object.
  install_attrs_->SetTpm(tpm_);
  install_attrs_->Init(tpm_);

  // Check if the machine is enterprise owned and report to mount_ then.
  DetectEnterpriseOwnership();
}

void UserDataAuth::FinalizeInstallAttributesIfMounted() {
  AssertOnMountThread();

  bool is_mounted = IsMounted();
  if (is_mounted &&
      install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    scoped_refptr<UserSession> guest_session = GetUserSession(guest_user_);
    bool guest_mounted = guest_session.get() && guest_session->IsActive();
    if (!guest_mounted) {
      install_attrs_->Finalize();
    }
  }
}

bool UserDataAuth::GetShouldMountAsEphemeral(
    const std::string& account_id,
    bool is_ephemeral_mount_requested,
    bool has_create_request,
    bool* is_ephemeral,
    user_data_auth::CryptohomeErrorCode* error) const {
  AssertOnMountThread();
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

void UserDataAuth::EnsureBootLockboxFinalized() {
  AssertOnMountThread();

  // Lock NVRamBootLockbox
  auto nvram_boot_lockbox_client = BootLockboxClient::CreateBootLockboxClient();
  if (!nvram_boot_lockbox_client) {
    LOG(WARNING) << "Failed to create nvram_boot_lockbox_client";
    return;
  }

  if (!nvram_boot_lockbox_client->Finalize()) {
    LOG(WARNING) << "Failed to finalize nvram lockbox.";
  }
}

// TODO(b/172344610, dlunev): abstract user_session through factory/manager.
scoped_refptr<UserSession> UserDataAuth::GetOrCreateUserSession(
    const std::string& username) {
  // This method touches the |sessions_| object so it needs to run on
  // |mount_thread_|
  AssertOnMountThread();
  if (sessions_.count(username) == 0U) {
    // We don't have a mount associated with |username|, let's create one.
    EnsureBootLockboxFinalized();
    scoped_refptr<cryptohome::Mount> m = mount_factory_->New(
        platform_, homedirs_, legacy_mount_, bind_mount_downloads_,
        /*local_mounter=*/false);
    if (!m) {
      return nullptr;
    }
    sessions_[username] =
        new UserSession(homedirs_, low_disk_space_handler_->disk_cleanup(),
                        keyset_management_, user_activity_timestamp_manager_,
                        pkcs11_token_factory_, system_salt_, m);
  }
  return sessions_[username];
}

bool UserDataAuth::RemoveUserSession(const std::string& username) {
  AssertOnMountThread();

  if (sessions_.count(username) != 0) {
    return (1U == sessions_.erase(username));
  }
  return true;
}

void UserDataAuth::MountGuest(
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  if (sessions_.size() != 0) {
    LOG(WARNING) << "Guest mount requested with other sessions active.";
  }
  // Rather than make it safe to check the size, then clean up, just always
  // clean up.
  bool ok = RemoveAllMounts();
  user_data_auth::MountReply reply;
  // Provide an authoritative filesystem-sanitized username.
  reply.set_sanitized_username(
      SanitizeUserNameWithSalt(guest_user_, system_salt_));
  if (!ok) {
    LOG(ERROR) << "Could not unmount cryptohomes for Guest use";
    reply.set_error(user_data_auth::CryptohomeErrorCode::
                        CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    std::move(on_done).Run(reply);
    return;
  }
  ReportTimerStart(kMountGuestExTimer);

  // Create a ref-counted guest mount for async use and then throw it away.
  scoped_refptr<UserSession> guest_session =
      GetOrCreateUserSession(guest_user_);
  if (!guest_session || guest_session->MountGuest() != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Could not initialize guest session.";
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  if (reply.error() ==
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
    // We only report the guest mount time for successful cases.
    ReportTimerStop(kMountGuestExTimer);
  }

  // TODO(b/137073669): Cleanup guest_mount if mount failed.
  std::move(on_done).Run(reply);
}

void UserDataAuth::DoMount(
    user_data_auth::MountRequest request,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Received a mount request.";

  // DoMount current supports guest login/mount, normal plaintext password login
  // and challenge response login. For guest mount, a special process
  // (MountGuest()) is used. Meanwhile, for normal plaintext password login and
  // challenge response login, both will flow through this method. This method
  // generally does some parameter validity checking, then pass the request onto
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

  // AuthSession associated with this request's auth_session_id. Can be empty
  // in case auth_session_id is not supplied.
  AuthSession* auth_session = nullptr;

  if (!request.auth_session_id().empty()) {
    auth_session =
        auth_session_manager_->FindAuthSession(request.auth_session_id());
    if (!auth_session) {
      reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
      LOG(ERROR) << "Invalid AuthSession token provided.";
      std::move(on_done).Run(reply);
      return;
    }
    if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      LOG(ERROR) << "AuthSession is not authenticated";
      std::move(on_done).Run(reply);
      return;
    }
  }

  // Check for empty account ID
  if (account_id.empty() && !auth_session) {
    LOG(ERROR) << "No email supplied";
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  // Key generation is not needed if there is a valid AuthSession as part of the
  // request. Key generation is handled in AuthSession.
  if (request.public_mount() && !auth_session) {
    // Public mount have a set of passkey/password that is generated directly
    // from the username (and a local system salt.)
    brillo::SecureBlob public_mount_passkey =
        keyset_management_->GetPublicMountPassKey(account_id);
    if (public_mount_passkey.empty()) {
      LOG(ERROR) << "Could not get public mount passkey.";
      reply.set_error(user_data_auth::CryptohomeErrorCode::
                          CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
      std::move(on_done).Run(reply);
      return;
    }

    // Set the secret as the key for cryptohome authorization/creation.
    request.mutable_authorization()->mutable_key()->set_secret(
        public_mount_passkey.to_string());
    if (request.has_create()) {
      request.mutable_create()->mutable_keys(0)->set_secret(
          public_mount_passkey.to_string());
    }
  }

  // We do not allow empty password, except for challenge response type login.
  if (request.authorization().key().secret().empty() &&
      request.authorization().key().data().type() !=
          KeyData::KEY_TYPE_CHALLENGE_RESPONSE &&
      !auth_session) {
    LOG(ERROR) << "No key secret supplied";
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  if (request.has_create() && !auth_session) {
    // copy_authorization_key in CreateRequest means that we'll copy the
    // authorization request's key and use it as if it's the key specified in
    // CreateRequest.
    if (request.create().copy_authorization_key()) {
      Key* auth_key = request.mutable_create()->add_keys();
      *auth_key = request.authorization().key();
    }

    // Validity check for |request.create.keys|.
    int keys_size = request.create().keys_size();
    if (keys_size == 0) {
      LOG(ERROR) << "CreateRequest supplied with no keys";
      reply.set_error(user_data_auth::CryptohomeErrorCode::
                          CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      std::move(on_done).Run(reply);
      return;
    } else if (keys_size > 1) {
      LOG(ERROR) << "MountEx: unimplemented CreateRequest with multiple keys";
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
    }
  }

  // Determine whether the mount should be ephemeral.
  bool is_ephemeral = false;
  bool require_ephemeral =
      request.require_ephemeral() ||
      (auth_session ? auth_session->ephemeral_user() : false);
  user_data_auth::CryptohomeErrorCode mount_error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  if (!GetShouldMountAsEphemeral(account_id, require_ephemeral,
                                 (request.has_create() || auth_session),
                                 &is_ephemeral, &mount_error)) {
    reply.set_error(mount_error);
    std::move(on_done).Run(reply);
    return;
  }

  // MountArgs is a set of parameters that we'll be passing around to
  // ContinueMountWithCredentials() and DoChallengeResponseMount().
  UserDataAuth::MountArgs mount_args;

  // request.has_create() represents a CreateRequest, telling the API to
  // create a user with the credentials in CreateRequest. create_if_missing
  // creates a user mount should one not exist. In the legacy use case,
  // CreateRequest needs to requested in the Mount call API for user creation.
  // When AuthSessions are fully functional with mount call, we would not be
  // creating user directories in mount call, instead we'd use
  // CreateEphemeral. But for now, code paths such as ephemeral mounts
  // require create_if_missing to be set to true to continue mounting as
  // Ephemeral user directories are created here.
  // Therefore, if a valid and an authenticated AuthSession is passed we
  // can temporarily bypass create_if_missing as a first step to prevent
  // credentials from flowing to mount call. Later, this would be replaced by
  // CreateEphemeral, CreatePersistent calls.
  mount_args.create_if_missing = (request.has_create() || auth_session);
  mount_args.is_ephemeral = is_ephemeral;
  mount_args.create_as_ecryptfs =
      force_ecryptfs_ ||
      (request.has_create() && request.create().force_ecryptfs());
  mount_args.to_migrate_from_ecryptfs = request.to_migrate_from_ecryptfs();
  // Force_ecryptfs_ wins.
  mount_args.force_dircrypto =
      !force_ecryptfs_ && request.force_dircrypto_if_available();

  // Process challenge-response credentials asynchronously.
  if ((request.authorization().key().data().type() ==
       KeyData::KEY_TYPE_CHALLENGE_RESPONSE) &&
      !auth_session) {
    DoChallengeResponseMount(request, mount_args, std::move(on_done));
    return;
  }

  auto credentials = std::make_unique<Credentials>(
      account_id, SecureBlob(request.authorization().key().secret()));
  // Everything else can be the default.
  credentials->set_key_data(request.authorization().key().data());

  std::optional<base::UnguessableToken> token = std::nullopt;
  if (auth_session) {
    token = auth_session->token();
  }

  ContinueMountWithCredentials(request, std::move(credentials), token,
                               mount_args, std::move(on_done));
  LOG(INFO) << "Finished mount request process";
}

bool UserDataAuth::InitForChallengeResponseAuth(
    user_data_auth::CryptohomeErrorCode* error_code) {
  AssertOnMountThread();
  if (challenge_credentials_helper_) {
    // Already successfully initialized.
    return true;
  }

  if (!tpm_) {
    LOG(ERROR) << "Cannot do challenge-response authentication without TPM";
    *error_code = user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }

  if (!tpm_->IsEnabled() || !tpm_->IsOwned()) {
    LOG(ERROR) << "TPM must be initialized in order to do challenge-response "
                  "authentication";
    *error_code = user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }

  // Fail if the TPM is known to be vulnerable and we're not in a test image.
  bool is_srk_roca_vulnerable;
  if (StatusChain<TPMErrorBase> err =
          tpm_->IsSrkRocaVulnerable(&is_srk_roca_vulnerable)) {
    LOG(ERROR) << "Cannot do challenge-response mount: Failed to check for "
                  "ROCA vulnerability: "
               << err;
    *error_code = user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }
  if (is_srk_roca_vulnerable) {
    if (!IsOsTestImage()) {
      LOG(ERROR)
          << "Cannot do challenge-response mount: TPM is ROCA vulnerable";
      *error_code = user_data_auth::CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED;
      return false;
    }
    LOG(WARNING) << "TPM is ROCA vulnerable; ignoring this for "
                    "challenge-response mount due to running in test image";
  }

  if (!mount_thread_bus_) {
    LOG(ERROR) << "Cannot do challenge-response mount without system D-Bus bus";
    *error_code = user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }

  // Lazily create the helper object that manages generation/decryption of
  // credentials for challenge-protected vaults.

  Blob delegate_blob, delegate_secret;

  bool has_reset_lock_permissions = false;
  // TPM Delegate is required for TPM1.2. For TPM2.0, this is a no-op.
  if (!tpm_->GetDelegate(&delegate_blob, &delegate_secret,
                         &has_reset_lock_permissions)) {
    LOG(ERROR)
        << "Cannot do challenge-response authentication without TPM delegate";
    *error_code = user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }

  default_challenge_credentials_helper_ =
      std::make_unique<ChallengeCredentialsHelperImpl>(tpm_, delegate_blob,
                                                       delegate_secret);
  challenge_credentials_helper_ = default_challenge_credentials_helper_.get();

  return true;
}

void UserDataAuth::DoChallengeResponseMount(
    const user_data_auth::MountRequest& request,
    const UserDataAuth::MountArgs& mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();
  DCHECK_EQ(request.authorization().key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;

  user_data_auth::CryptohomeErrorCode error_code =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  if (!InitForChallengeResponseAuth(&error_code)) {
    reply.set_error(error_code);
    std::move(on_done).Run(reply);
    return;
  }

  const std::string& account_id = GetAccountId(request.account());
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);
  const KeyData key_data = request.authorization().key().data();

  if (!key_data.challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  if (key_data.challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  const ChallengePublicKeyInfo& public_key_info =
      key_data.challenge_response_key(0);

  if (!request.authorization().has_key_delegate() ||
      !request.authorization().key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response mount without key delegate "
                  "information";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  // KeyChallengeService is tasked with contacting the challenge response D-Bus
  // service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          mount_thread_bus_,
          request.authorization().key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  if (!homedirs_->Exists(obfuscated_username) &&
      !mount_args.create_if_missing) {
    LOG(ERROR) << "Cannot do challenge-response mount. Account not found.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    std::move(on_done).Run(reply);
    return;
  }

  std::unique_ptr<VaultKeyset> vault_keyset(keyset_management_->GetVaultKeyset(
      obfuscated_username, request.authorization().key().data().label()));
  const bool use_existing_credentials =
      vault_keyset && !mount_args.is_ephemeral;
  // If the home directory already exist (and thus the corresponding encrypted
  // VaultKeyset exists) and the mount is not ephemeral, then we'll use the
  // ChallengeCredentialsHelper (which handles challenge response
  // authentication) to decrypt the VaultKeyset.
  if (use_existing_credentials && vault_keyset->HasSignatureChallengeInfo()) {
    // Home directory already exist and we are not doing ephemeral mount, so
    // we'll decrypt existing VaultKeyset.

    // TODO(b/203994461): This should be handled inside the auth block.
    bool locked_to_single_user =
        platform_->FileExists(base::FilePath(kLockedToSingleUserFile));

    // Note: We don't need the |signature_challenge_info| when we are decrypting
    // the challenge credential, because the keyset managerment doesn't need to
    // read the |signature_challenge_info| from the credentials in this case.
    // This behavior would eventually be replaced by the asynchronous challenge
    // credential auth block, we can get rid of the |signature_challenge_info|
    // from the credentials after we move it into the auth block state.
    challenge_credentials_helper_->Decrypt(
        account_id, proto::FromProto(public_key_info),
        proto::FromProto(vault_keyset->GetSignatureChallengeInfo()),
        locked_to_single_user, std::move(key_challenge_service),
        base::BindOnce(
            &UserDataAuth::OnChallengeResponseMountCredentialsObtained,
            base::Unretained(this), request, mount_args, std::move(on_done),
            /*signature_challenge_info=*/nullptr));
  } else {
    // We'll create a new VaultKeyset that accepts challenge response
    // authentication.
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "No existing challenge-response vault keyset found";
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
      std::move(on_done).Run(reply);
      return;
    }

    std::map<uint32_t, brillo::Blob> default_pcr_map =
        tpm_->GetPcrMap(obfuscated_username, false /* use_extended_pcr */);
    std::map<uint32_t, brillo::Blob> extended_pcr_map =
        tpm_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */);

    challenge_credentials_helper_->GenerateNew(
        account_id, proto::FromProto(public_key_info),
        std::move(default_pcr_map), std::move(extended_pcr_map),
        std::move(key_challenge_service),
        base::BindOnce(
            &UserDataAuth::OnChallengeResponseMountCredentialsObtained,
            base::Unretained(this), request, mount_args, std::move(on_done)));
  }
}

void UserDataAuth::OnChallengeResponseMountCredentialsObtained(
    const user_data_auth::MountRequest& request,
    const UserDataAuth::MountArgs mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done,
    std::unique_ptr<structure::SignatureChallengeInfo> signature_challenge_info,
    std::unique_ptr<brillo::SecureBlob> passkey) {
  AssertOnMountThread();
  // If we get here, that means the ChallengeCredentialsHelper have finished the
  // process of doing challenge response authentication, either successful or
  // otherwise.

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;

  DCHECK_EQ(request.authorization().key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  if (!passkey) {
    // Challenge response authentication have failed.
    LOG(ERROR) << "Could not mount due to failure to obtain challenge-response "
                  "credentials";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  const std::string& account_id = GetAccountId(request.account());
  auto credentials = std::make_unique<Credentials>(account_id, *passkey);
  credentials->set_key_data(request.authorization().key().data());

  if (signature_challenge_info != nullptr) {
    credentials->set_challenge_credentials_keyset_info(
        proto::ToProto(*signature_challenge_info));
  }

  DCHECK_EQ(credentials->key_data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  ContinueMountWithCredentials(request, std::move(credentials), std::nullopt,
                               mount_args, std::move(on_done));
}

void UserDataAuth::ContinueMountWithCredentials(
    const user_data_auth::MountRequest& request,
    std::unique_ptr<Credentials> credentials,
    std::optional<base::UnguessableToken> token,
    const UserDataAuth::MountArgs& mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  AuthSession* auth_session =
      token.has_value() ? auth_session_manager_->FindAuthSession(token.value())
                        : nullptr;

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
  bool other_sessions_active = true;
  if (sessions_.size() == 0) {
    other_sessions_active = CleanUpStaleMounts(false);
    // This could run on every interaction to catch any unused mounts.
  }

  // If the home directory for our user doesn't exist and we aren't instructed
  // to create the home directory, and reply with the error.
  if (!request.has_create() &&
      !homedirs_->Exists(credentials->GetObfuscatedUsername(system_salt_)) &&
      !token.has_value()) {
    LOG(ERROR) << "Account not found when mounting with credentials.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    std::move(on_done).Run(reply);
    return;
  }

  std::string account_id =
      auth_session ? auth_session->username() : GetAccountId(request.account());
  // Provide an authoritative filesystem-sanitized username.
  reply.set_sanitized_username(
      brillo::cryptohome::home::SanitizeUserName(account_id));

  // Check if the guest user is mounted, if it is, we can't proceed.
  scoped_refptr<UserSession> guest_session = GetUserSession(guest_user_);
  bool guest_mounted = guest_session.get() && guest_session->IsActive();
  // TODO(wad,ellyjones) Change this behavior to return failure even
  // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
  if (guest_mounted && !guest_session->Unmount()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    std::move(on_done).Run(reply);
    return;
  }

  scoped_refptr<UserSession> user_session = GetOrCreateUserSession(account_id);

  if (!user_session) {
    LOG(ERROR) << "Could not initialize user session.";
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return;
  }

  // For public mount, don't proceed if there is any existing mount or stale
  // mount. Exceptionally, it is normal and ok to have a failed previous mount
  // attempt for the same user.
  const bool only_self_unmounted_attempt =
      sessions_.size() == 1 && !user_session->IsActive();
  if (request.public_mount() && other_sessions_active &&
      !only_self_unmounted_attempt) {
    LOG(ERROR) << "Public mount requested with other sessions active.";
    if (!request.auth_session_id().empty()) {
      std::string obfuscated =
          SanitizeUserNameWithSalt(account_id, system_salt_);
      if (!homedirs_->Remove(obfuscated)) {
        LOG(ERROR) << "Failed to remove vault for kiosk user.";
      }
    }
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    std::move(on_done).Run(reply);
    return;
  }

  // Don't overlay an ephemeral mount over a file-backed one.
  if (mount_args.is_ephemeral && user_session->IsActive() &&
      !user_session->IsEphemeral()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_session->Unmount()) {
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
      std::move(on_done).Run(reply);
      return;
    }
  }

  if (mount_args.is_ephemeral && !mount_args.create_if_missing) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when its "
                  "creation on-the-fly is allowed.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  // If a user's home directory is already mounted, then we'll just recheck its
  // credential with what's cached in memory. This is much faster than going to
  // the TPM.
  if (user_session->IsActive()) {
    // Attempt a short-circuited credential test.
    if (user_session->VerifyCredentials(*credentials)) {
      std::move(on_done).Run(reply);
      keyset_management_->ResetLECredentials(*credentials);
      return;
    }
    // If the Mount has invalid credentials (repopulated from system state)
    // this will ensure a user can still sign-in with the right ones.
    // TODO(wad) Should we unmount on a failed re-mount attempt?
    if (!user_session->VerifyCredentials(*credentials) &&
        !keyset_management_->AreCredentialsValid(*credentials)) {
      LOG(ERROR) << "Credentials are invalid";
      reply.set_error(
          user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    } else {
      keyset_management_->ResetLECredentials(*credentials);
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

  // Remove all existing cryptohomes, except for the owner's one, if the
  // ephemeral users policy is on.
  // Note that a fresh policy value is read here, which in theory can conflict
  // with the one used for calculation of |mount_args.is_ephemeral|. However,
  // this inconsistency (whose probability is anyway pretty low in practice)
  // should only lead to insignificant transient glitches, like an attempt to
  // mount a non existing anymore cryptohome.
  if (homedirs_->AreEphemeralUsersEnabled())
    homedirs_->RemoveNonOwnerCryptohomes();

  MountError code;
  if (auth_session) {
    code = AttemptUserMount(auth_session, mount_args, user_session);
  } else {
    code = AttemptUserMount(*credentials, mount_args, user_session);
  }
  // Does actual mounting here.
  if (code == MOUNT_ERROR_TPM_COMM_ERROR) {
    LOG(WARNING) << "TPM communication error. Retrying.";
    if (auth_session) {
      code = AttemptUserMount(auth_session, mount_args, user_session);
    } else {
      code = AttemptUserMount(*credentials, mount_args, user_session);
    }
  }

  if (code == MOUNT_ERROR_VAULT_UNRECOVERABLE) {
    LOG(ERROR) << "Unrecoverable vault, removing.";
    std::string obfuscated = credentials->GetObfuscatedUsername(system_salt_);
    if (!homedirs_->Remove(obfuscated)) {
      LOG(ERROR) << "Failed to remove unrecoverable vault.";
      code = MOUNT_ERROR_REMOVE_INVALID_USER_FAILED;
    }
  }

  // Mark the timer as done.
  ReportTimerStop(kMountExTimer);

  if (code != MOUNT_ERROR_NONE) {
    // Mount returned a non-OK status.
    LOG(ERROR) << "Failed to mount cryptohome, error = " << code;
    reply.set_error(MountErrorToCryptohomeError(code));
    ResetDictionaryAttackMitigation();
    std::move(on_done).Run(reply);
    return;
  }

  keyset_management_->ResetLECredentials(*credentials);
  std::move(on_done).Run(reply);

  InitializePkcs11(user_session.get());
}

std::unique_ptr<VaultKeyset> UserDataAuth::LoadVaultKeyset(
    const Credentials& credentials, bool is_new_user, MountError* error) {
  PopulateError(error, MountError::MOUNT_ERROR_NONE);

  if (is_new_user) {
    if (!keyset_management_->AddInitialKeyset(credentials)) {
      LOG(ERROR) << "Error adding initial keyset.";
      PopulateError(error, MountError::MOUNT_ERROR_KEY_FAILURE);
      return nullptr;
    }
  }
  std::unique_ptr<VaultKeyset> vk =
      keyset_management_->GetValidKeyset(credentials, error);
  if (!vk) {
    return nullptr;
  }
  // Reencrypt keyset with a TPM backed key if it is not the case, fill in
  // missing fields in the keyset, and resave.
  keyset_management_->ReSaveKeysetIfNeeded(credentials, vk.get());

  return vk;
}

MountError UserDataAuth::AttemptUserMount(
    const Credentials& credentials,
    const UserDataAuth::MountArgs& mount_args,
    scoped_refptr<UserSession> user_session) {
  if (user_session->IsActive()) {
    return MOUNT_ERROR_MOUNT_POINT_BUSY;
  }

  if (mount_args.is_ephemeral) {
    user_session->SetCredentials(credentials);
    return user_session->MountEphemeral(credentials.username());
  }

  MountError error = MOUNT_ERROR_NONE;
  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  bool created = false;
  if (!homedirs_->CryptohomeExists(obfuscated_username, &error)) {
    if (error != MOUNT_ERROR_NONE) {
      LOG(ERROR) << "Failed to check cryptohome existence for : "
                 << obfuscated_username << " error = " << error;
      return error;
    }
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "Asked to mount nonexistent user";
      return MOUNT_ERROR_USER_DOES_NOT_EXIST;
    }
    if (!homedirs_->Create(credentials.username())) {
      LOG(ERROR) << "Error creating cryptohome.";
      return MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
    }
    created = true;
  }

  std::unique_ptr<VaultKeyset> vk =
      LoadVaultKeyset(credentials, created, &error);
  if (error != MOUNT_ERROR_NONE) {
    return error;
  }
  if (vk == nullptr) {
    return MOUNT_ERROR_FATAL;
  }

  error = user_session->MountVault(credentials.username(),
                                   FileSystemKeyset(*vk.get()),
                                   MountArgsToVaultOptions(mount_args));
  if (error == MOUNT_ERROR_NONE) {
    // Store the credentials in the cache to use on session unlock.
    user_session->SetCredentials(credentials);
  }

  return error;
}

MountError UserDataAuth::AttemptUserMount(
    AuthSession* auth_session,
    const UserDataAuth::MountArgs& mount_args,
    scoped_refptr<UserSession> user_session) {
  if (user_session->IsActive()) {
    return MOUNT_ERROR_MOUNT_POINT_BUSY;
  }
  // Mount ephemerally using authsession
  if (mount_args.is_ephemeral) {
    // Store the credentials in the cache to use on session unlock.
    user_session->SetCredentials(auth_session);
    return user_session->MountEphemeral(auth_session->username());
  }

  // Cannot proceed with mount if the AuthSession is not authenticated yet.
  if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
    return MOUNT_ERROR_FATAL;
  }

  MountError error = user_session->MountVault(
      auth_session->username(), auth_session->file_system_keyset(),
      MountArgsToVaultOptions(mount_args));

  if (error == MOUNT_ERROR_NONE) {
    // Store the credentials in the cache to use on session unlock.
    user_session->SetCredentials(auth_session);
  }
  return error;
}

bool UserDataAuth::MigrateVaultKeyset(const Credentials& existing_credentials,
                                      const Credentials& new_credentials) {
  DCHECK_EQ(existing_credentials.username(), new_credentials.username());
  std::unique_ptr<VaultKeyset> vault_keyset =
      keyset_management_->GetValidKeyset(existing_credentials, nullptr);
  if (vault_keyset == nullptr) {
    return false;
  }

  if (!keyset_management_->Migrate(*vault_keyset.get(), new_credentials)) {
    return false;
  }
  return true;
}

CryptohomeErrorCode UserDataAuth::AddVaultKeyset(
    const Credentials& existing_credentials,
    const Credentials& new_credentials,
    bool clobber) {
  DCHECK_EQ(existing_credentials.username(), new_credentials.username());
  std::unique_ptr<VaultKeyset> vk =
      keyset_management_->GetValidKeyset(existing_credentials, nullptr);

  if (!vk) {
    // Differentiate between failure and non-existent.
    if (!existing_credentials.key_data().label().empty()) {
      vk = keyset_management_->GetVaultKeyset(
          existing_credentials.GetObfuscatedUsername(system_salt_),
          existing_credentials.key_data().label());
      if (!vk.get()) {
        LOG(WARNING) << "Key not found for AddKey operation.";
        return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
      }
    }
    LOG(WARNING) << "Invalid authentication provided for AddKey operation.";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  // If the newly added credential is an LE credential and reset seed is
  // missing in the vault keyset it needs to be added. We don't know whether
  // it is LE credential yet. So add reset_seed in anycase and resave.
  VaultKeyset* vault_keyset = vk.get();
  CryptohomeErrorCode crypto_error =
      keyset_management_->AddWrappedResetSeedIfMissing(vault_keyset,
                                                       existing_credentials);
  // Add the new key data to the user vault_keyset.
  if (crypto_error == CRYPTOHOME_ERROR_NOT_SET) {
    crypto_error =
        keyset_management_->AddKeyset(new_credentials, *vault_keyset, clobber);
  }
  return crypto_error;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::AddKey(
    const user_data_auth::AddKeyRequest& request) {
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

  const std::string& auth_key_secret =
      request.authorization_request().key().secret();
  Credentials credentials(account_id, SecureBlob(auth_key_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  const std::string& new_key_secret = request.key().secret();
  Credentials new_credentials(account_id, SecureBlob(new_key_secret));

  new_credentials.set_key_data(request.key().data());
  CryptohomeErrorCode result;
  result =
      AddVaultKeyset(credentials, new_credentials, request.clobber_if_exists());

  // Note that cryptohome::CryptohomeErrorCode and
  // user_data_auth::CryptohomeErrorCode are same in content, and it'll remain
  // so until the end of the refactor, so we can safely cast from one to
  // another. This is enforced in our unit test.
  return static_cast<user_data_auth::CryptohomeErrorCode>(result);
}

void UserDataAuth::CheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "CheckKeyRequest must have account_id and authorization_request.";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "CheckKeyRequest must have valid account_id.";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return;
  }

  // Process challenge-response credentials asynchronously.
  if (request.authorization_request().key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    DoChallengeResponseCheckKey(request, std::move(on_done));
    return;
  }

  // Process fingerprint credentials asynchronously.
  if (request.authorization_request().key().data().type() ==
      KeyData::KEY_TYPE_FINGERPRINT) {
    if (!fingerprint_manager_) {
      // Fingerprint manager failed to initialize, or the device may not
      // support fingerprint auth at all.
      std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                                 CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
      return;
    }
    if (!fingerprint_manager_->HasAuthSessionForUser(
            SanitizeUserNameWithSalt(account_id, system_salt_))) {
      std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                                 CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
      return;
    }
    fingerprint_manager_->SetAuthScanDoneCallback(base::BindRepeating(
        &UserDataAuth::CompleteFingerprintCheckKey, base::Unretained(this),
        base::Passed(std::move(on_done))));
    return;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  const std::string& auth_secret =
      request.authorization_request().key().secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in CheckKeyRequest.";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return;
  }

  Credentials credentials(account_id, SecureBlob(auth_secret));
  credentials.set_key_data(request.authorization_request().key().data());

  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  bool found_valid_credentials = false;
  for (const auto& session_pair : sessions_) {
    if (session_pair.second->VerifyCredentials(credentials)) {
      found_valid_credentials = true;
      break;
    }
  }

  if (found_valid_credentials) {
    // Entered the right creds, so reset LE credentials.
    keyset_management_->ResetLECredentials(credentials);
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    return;
  }

  // Cover different keys for the same user with homedirs.
  if (!homedirs_->Exists(obfuscated_username)) {
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    return;
  }

  if (!keyset_management_->AreCredentialsValid(credentials)) {
    // TODO(wad) Should this pass along KEY_NOT_FOUND too?
    std::move(on_done).Run(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    ResetDictionaryAttackMitigation();
    return;
  }

  keyset_management_->ResetLECredentials(credentials);
  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  return;
}

void UserDataAuth::CompleteFingerprintCheckKey(
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
    FingerprintScanStatus status) {
  AssertOnMountThread();
  if (status == FingerprintScanStatus::FAILED_RETRY_ALLOWED) {
    std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                               CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED);
    return;
  } else if (status == FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED) {
    std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                               CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
    return;
  }

  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

void UserDataAuth::DoChallengeResponseCheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  const auto& authorization = request.authorization_request();
  DCHECK_EQ(authorization.key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  user_data_auth::CryptohomeErrorCode error_code =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  if (!InitForChallengeResponseAuth(&error_code)) {
    std::move(on_done).Run(error_code);
    return;
  }

  if (!authorization.has_key_delegate() ||
      !authorization.key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response authentication without key "
                  "delegate information";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }
  if (!authorization.key().data().challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }
  if (authorization.key().data().challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  // Begin from attempting a lightweight check that doesn't use the vault keyset
  // or heavy TPM operations, and therefore is faster than the full check and
  // also works in case the mount is ephemeral.
  TryLightweightChallengeResponseCheckKey(request, std::move(on_done));
}

void UserDataAuth::TryLightweightChallengeResponseCheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  const auto& authorization = request.authorization_request();
  const auto& identifier = request.account_id();

  DCHECK_EQ(authorization.key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK(challenge_credentials_helper_);

  const std::string& account_id = GetAccountId(identifier);
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);

  std::optional<KeyData> found_session_key_data;
  for (const auto& session_pair : sessions_) {
    const scoped_refptr<UserSession>& session = session_pair.second;
    if (session->VerifyUser(obfuscated_username) &&
        KeyMatchesForLightweightChallengeResponseCheck(
            authorization.key().data(), *session)) {
      found_session_key_data = session->key_data();
      break;
    }
  }
  if (!found_session_key_data) {
    // No matching user session found, so fall back to the full check.
    OnLightweightChallengeResponseCheckKeyDone(request, std::move(on_done),
                                               /*success=*/false);
    return;
  }

  // KeyChallengeService is tasked with contacting the challenge response D-Bus
  // service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          mount_thread_bus_, authorization.key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    OnLightweightChallengeResponseCheckKeyDone(request, std::move(on_done),
                                               /*success=*/false);
    return;
  }

  if (!found_session_key_data->challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    OnLightweightChallengeResponseCheckKeyDone(request, std::move(on_done),
                                               /*success=*/false);
    return;
  }

  if (found_session_key_data->challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    OnLightweightChallengeResponseCheckKeyDone(request, std::move(on_done),
                                               /*success=*/false);
    return;
  }

  const ChallengePublicKeyInfo& public_key_info =
      found_session_key_data->challenge_response_key(0);

  // Attempt the lightweight check against the found user session.
  challenge_credentials_helper_->VerifyKey(
      account_id, proto::FromProto(public_key_info),
      std::move(key_challenge_service),
      base::BindOnce(&UserDataAuth::OnLightweightChallengeResponseCheckKeyDone,
                     base::Unretained(this), request, std::move(on_done)));
}

void UserDataAuth::OnLightweightChallengeResponseCheckKeyDone(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
    bool is_key_valid) {
  AssertOnMountThread();
  if (!is_key_valid) {
    DoFullChallengeResponseCheckKey(request, std::move(on_done));
    return;
  }

  // Note that the LE credentials are not reset here, since we don't have the
  // full credentials after the lightweight check.
  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

void UserDataAuth::DoFullChallengeResponseCheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  const auto& authorization = request.authorization_request();
  const auto& identifier = request.account_id();

  DCHECK_EQ(authorization.key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK(challenge_credentials_helper_);

  const std::string& account_id = GetAccountId(identifier);
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);

  // KeyChallengeService is tasked with contacting the challenge response D-Bus
  // service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          mount_thread_bus_, authorization.key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  if (!homedirs_->Exists(obfuscated_username)) {
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    return;
  }

  std::unique_ptr<VaultKeyset> vault_keyset(keyset_management_->GetVaultKeyset(
      obfuscated_username, authorization.key().data().label()));
  if (!vault_keyset) {
    LOG(ERROR) << "No existing challenge-response vault keyset found";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  if (!authorization.key().data().challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  if (authorization.key().data().challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  const ChallengePublicKeyInfo& public_key_info =
      authorization.key().data().challenge_response_key(0);

  // TODO(b/203994461): This should be handled inside the auth block.
  bool locked_to_single_user =
      platform_->FileExists(base::FilePath(kLockedToSingleUserFile));

  challenge_credentials_helper_->Decrypt(
      account_id, proto::FromProto(public_key_info),
      proto::FromProto(vault_keyset->GetSignatureChallengeInfo()),
      locked_to_single_user, std::move(key_challenge_service),
      base::BindOnce(&UserDataAuth::OnFullChallengeResponseCheckKeyDone,
                     base::Unretained(this), request, std::move(on_done)));
}

void UserDataAuth::OnFullChallengeResponseCheckKeyDone(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
    std::unique_ptr<brillo::SecureBlob> passkey) {
  AssertOnMountThread();
  if (!passkey) {
    LOG(ERROR) << "Key checking failed due to failure to obtain "
                  "challenge-response credentials";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  const auto& authorization = request.authorization_request();
  const auto& identifier = request.account_id();
  const std::string& account_id = GetAccountId(identifier);

  auto credentials = std::make_unique<Credentials>(account_id, *passkey);
  credentials->set_key_data(authorization.key().data());

  // Entered the right creds, so reset LE credentials.
  keyset_management_->ResetLECredentials(*credentials);

  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
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

  Credentials credentials(account_id, SecureBlob(auth_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  CryptohomeErrorCode result;
  result = keyset_management_->RemoveKeyset(credentials, request.key().data());

  // Note that cryptohome::CryptohomeErrorCode and
  // user_data_auth::CryptohomeErrorCode are same in content, and it'll remain
  // so until the end of the refactor, so we can safely cast from one to
  // another.
  return static_cast<user_data_auth::CryptohomeErrorCode>(result);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::MassRemoveKeys(
    const user_data_auth::MassRemoveKeysRequest request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR) << "MassRemoveKeysRequest must have account_id and "
                  "authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "MassRemoveKeysRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  const std::string& auth_secret =
      request.authorization_request().key().secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in MassRemoveKeysRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id, SecureBlob(auth_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  if (!keyset_management_->AreCredentialsValid(credentials)) {
    return user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  // get all labels under the username
  std::vector<std::string> labels;
  if (!keyset_management_->GetVaultKeysetLabels(obfuscated_username, &labels)) {
    return user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }

  // get all exempt labels from |request|
  std::unordered_set<std::string> exempt_labels;
  for (int i = 0; i < request.exempt_key_data_size(); i++) {
    exempt_labels.insert(request.exempt_key_data(i).label());
  }
  for (std::string label : labels) {
    if (exempt_labels.find(label) == exempt_labels.end()) {
      // non-exempt label, should be removed
      std::unique_ptr<VaultKeyset> remove_vk(
          keyset_management_->GetVaultKeyset(obfuscated_username, label));
      if (!keyset_management_->ForceRemoveKeyset(obfuscated_username,
                                                 remove_vk->GetLegacyIndex())) {
        LOG(ERROR) << "MassRemoveKeys: failed to remove keyset " << label;
        return user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
      }
    }
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
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
      SanitizeUserNameWithSalt(account_id, system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  if (!keyset_management_->GetVaultKeysetLabels(obfuscated_username,
                                                labels_out)) {
    return user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::GetKeyData(
    const user_data_auth::GetKeyDataRequest& request,
    cryptohome::KeyData* data_out,
    bool* found) {
  AssertOnMountThread();

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
      SanitizeUserNameWithSalt(account_id, system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  // Requests only support using the key label at present.
  std::unique_ptr<VaultKeyset> vk(keyset_management_->GetVaultKeyset(
      obfuscated_username, request.key().data().label()));
  *found = (vk != nullptr);
  if (*found) {
    *data_out = vk->GetKeyData();
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
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

  Credentials credentials(account_id, SecureBlob(request.secret()));

  Credentials old_credentials(
      account_id, SecureBlob(request.authorization_request().key().secret()));
  if (!MigrateVaultKeyset(old_credentials, credentials)) {
    ResetDictionaryAttackMitigation();
    return user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
  if (session.get()) {
    if (!session->SetCredentials(credentials)) {
      LOG(WARNING) << "Failed to set new creds";
    }
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::Remove(
    const user_data_auth::RemoveRequest& request) {
  AssertOnMountThread();

  if (!request.has_identifier() && request.auth_session_id().empty()) {
    LOG(ERROR) << "RemoveRequest must have identifier or an AuthSession Id";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  AuthSession* auth_session = nullptr;
  if (!request.auth_session_id().empty()) {
    auth_session =
        auth_session_manager_->FindAuthSession(request.auth_session_id());
    if (!auth_session) {
      return user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN;
    }
  }

  std::string account_id = auth_session ? auth_session->username()
                                        : GetAccountId(request.identifier());
  if (account_id.empty()) {
    LOG(ERROR) << "RemoveRequest must have valid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string obfuscated = SanitizeUserNameWithSalt(account_id, system_salt_);
  if (!homedirs_->Remove(obfuscated)) {
    return user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED;
  }

  // Since the user is now removed, any further operations require a fresh
  // AuthSession.
  if (auth_session) {
    auth_session_manager_->RemoveAuthSession(request.auth_session_id());
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

void UserDataAuth::StartMigrateToDircrypto(
    const user_data_auth::StartMigrateToDircryptoRequest& request,
    base::RepeatingCallback<void(
        const user_data_auth::DircryptoMigrationProgress&)> progress_callback) {
  AssertOnMountThread();

  MigrationType migration_type = request.minimal_migration()
                                     ? MigrationType::MINIMAL
                                     : MigrationType::FULL;

  // Note that total_bytes and current_bytes field in |progress| is discarded by
  // client whenever |progress.status| is not DIRCRYPTO_MIGRATION_IN_PROGRESS,
  // this is why they are left with the default value of 0 here. Please see
  // MigrationHelper::ProgressCallback for more details.
  user_data_auth::DircryptoMigrationProgress progress;

  scoped_refptr<UserSession> session =
      GetUserSession(GetAccountId(request.account_id()));
  if (!session.get()) {
    LOG(ERROR) << "StartMigrateToDircrypto: Failed to get session.";
    progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
    progress_callback.Run(progress);
    return;
  }
  LOG(INFO) << "StartMigrateToDircrypto: Migrating to dircrypto.";
  if (!session->MigrateVault(progress_callback, migration_type)) {
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
  AssertOnMountThread();
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(GetAccountId(account), system_salt_);
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
  return tpm_->GetLECredentialBackend() &&
         tpm_->GetLECredentialBackend()->IsSupported();
}

int64_t UserDataAuth::GetAccountDiskUsage(
    const cryptohome::AccountIdentifier& account) {
  AssertOnMountThread();
  // Note that if the given |account| is invalid or non-existent, then HomeDirs'
  // implementation of ComputeDiskUsage is specified to return 0.
  return homedirs_->ComputeDiskUsage(GetAccountId(account));
}

bool UserDataAuth::IsArcQuotaSupported() {
  AssertOnOriginThread();
  return arc_disk_quota_->IsQuotaSupported();
}

int64_t UserDataAuth::GetCurrentSpaceForArcUid(uid_t android_uid) {
  AssertOnOriginThread();
  return arc_disk_quota_->GetCurrentSpaceForUid(android_uid);
}

int64_t UserDataAuth::GetCurrentSpaceForArcGid(uid_t android_gid) {
  AssertOnOriginThread();
  return arc_disk_quota_->GetCurrentSpaceForGid(android_gid);
}

int64_t UserDataAuth::GetCurrentSpaceForArcProjectId(int project_id) {
  AssertOnOriginThread();
  return arc_disk_quota_->GetCurrentSpaceForProjectId(project_id);
}

bool UserDataAuth::SetProjectId(
    int project_id,
    user_data_auth::SetProjectIdAllowedPathType parent_path,
    const FilePath& child_path,
    const cryptohome::AccountIdentifier& account) {
  AssertOnOriginThread();
  const std::string& account_id = GetAccountId(account);
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);
  return arc_disk_quota_->SetProjectId(
      project_id, static_cast<SetProjectIdAllowedPathType>(parent_path),
      child_path, obfuscated_username);
}

bool UserDataAuth::SetMediaRWDataFileProjectId(int project_id,
                                               int fd,
                                               int* out_error) {
  AssertOnOriginThread();
  return arc_disk_quota_->SetMediaRWDataFileProjectId(project_id, fd,
                                                      out_error);
}

bool UserDataAuth::Pkcs11IsTpmTokenReady() {
  AssertOnMountThread();
  // We touched the sessions_ object, so we need to be on mount thread.

  for (const auto& session_pair : sessions_) {
    UserSession* session = session_pair.second.get();
    if (!session->GetPkcs11Token() || !session->GetPkcs11Token()->IsReady()) {
      return false;
    }
  }

  return true;
}

user_data_auth::TpmTokenInfo UserDataAuth::Pkcs11GetTpmTokenInfo(
    const std::string& username) {
  AssertOnOriginThread();
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
  // We are touching the |sessions_| object so we need to be on mount thread.

  for (const auto& session_pair : sessions_) {
    if (session_pair.second->GetPkcs11Token()) {
      session_pair.second->GetPkcs11Token()->Remove();
    }
  }
}

bool UserDataAuth::InstallAttributesGet(const std::string& name,
                                        std::vector<uint8_t>* data_out) {
  AssertOnMountThread();
  return install_attrs_->Get(name, data_out);
}

bool UserDataAuth::InstallAttributesSet(const std::string& name,
                                        const std::vector<uint8_t>& data) {
  AssertOnMountThread();
  return install_attrs_->Set(name, data);
}

bool UserDataAuth::InstallAttributesFinalize() {
  AssertOnMountThread();
  bool result = install_attrs_->Finalize();
  DetectEnterpriseOwnership();
  return result;
}

int UserDataAuth::InstallAttributesCount() {
  AssertOnMountThread();
  return install_attrs_->Count();
}

bool UserDataAuth::InstallAttributesIsSecure() {
  AssertOnMountThread();
  return install_attrs_->is_secure();
}

InstallAttributes::Status UserDataAuth::InstallAttributesGetStatus() {
  AssertOnMountThread();
  return install_attrs_->status();
}

// static
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

void UserDataAuth::OnFingerprintStartAuthSessionResp(
    base::OnceCallback<
        void(const user_data_auth::StartFingerprintAuthSessionReply&)> on_done,
    bool success) {
  AssertOnMountThread();
  VLOG(1) << "Start fingerprint auth session result: " << success;
  user_data_auth::StartFingerprintAuthSessionReply reply;
  if (!success) {
    reply.set_error(user_data_auth::CryptohomeErrorCode::
                        CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
  }
  std::move(on_done).Run(reply);
}

void UserDataAuth::StartFingerprintAuthSession(
    const user_data_auth::StartFingerprintAuthSessionRequest& request,
    base::OnceCallback<void(
        const user_data_auth::StartFingerprintAuthSessionReply&)> on_done) {
  AssertOnMountThread();
  user_data_auth::StartFingerprintAuthSessionReply reply;

  if (!request.has_account_id()) {
    LOG(ERROR) << "StartFingerprintAuthSessionRequest must have account_id";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR)
        << "StartFingerprintAuthSessionRequest must have vaid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    std::move(on_done).Run(reply);
    return;
  }

  fingerprint_manager_->StartAuthSessionAsyncForUser(
      obfuscated_username,
      base::BindOnce(&UserDataAuth::OnFingerprintStartAuthSessionResp,
                     base::Unretained(this), std::move(on_done)));
}

void UserDataAuth::EndFingerprintAuthSession() {
  AssertOnMountThread();
  fingerprint_manager_->EndAuthSession();
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

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetWebAuthnSecretRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
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

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetWebAuthnSecretHashRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
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

user_data_auth::GetHibernateSecretReply UserDataAuth::GetHibernateSecret(
    const user_data_auth::GetHibernateSecretRequest& request) {
  AssertOnMountThread();
  user_data_auth::GetHibernateSecretReply reply;

  if (!request.has_account_id()) {
    LOG(ERROR) << "GetHibernateSecretRequest must have account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetHibernateSecretRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
  std::unique_ptr<brillo::SecureBlob> secret;
  if (session) {
    secret = session->GetHibernateSecret();
  }
  if (!secret) {
    LOG(ERROR) << "Failed to get hibernate secret hash.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    return reply;
  }

  reply.set_hibernate_secret(secret->to_string());
  return reply;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::GetFirmwareManagementParameters(
    user_data_auth::FirmwareManagementParameters* fwmp) {
  AssertOnOriginThread();
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
  AssertOnOriginThread();

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
  AssertOnOriginThread();
  return firmware_management_parameters_->Destroy();
}

const brillo::SecureBlob& UserDataAuth::GetSystemSalt() {
  AssertOnOriginThread();
  DCHECK_NE(system_salt_.size(), 0)
      << "Cannot call GetSystemSalt before initialization";
  return system_salt_;
}

bool UserDataAuth::UpdateCurrentUserActivityTimestamp(int time_shift_sec) {
  AssertOnMountThread();
  // We are touching the sessions object, so we'll need to be on mount thread.

  bool success = true;
  for (const auto& session_pair : sessions_) {
    const std::string obfuscated_username =
        SanitizeUserNameWithSalt(session_pair.first, system_salt_);
    success &= user_activity_timestamp_manager_->UpdateTimestamp(
        obfuscated_username, base::TimeDelta::FromSeconds(time_shift_sec));
  }

  return success;
}

bool UserDataAuth::GetRsuDeviceId(std::string* rsu_device_id) {
  AssertOnOriginThread();
  return tpm_->GetRsuDeviceId(rsu_device_id);
}

bool UserDataAuth::RequiresPowerwash() {
  AssertOnOriginThread();
  const bool is_powerwash_required = !crypto_->CanUnsealWithUserAuth();
  return is_powerwash_required;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::LockToSingleUserMountUntilReboot(
    const cryptohome::AccountIdentifier& account_id) {
  AssertOnOriginThread();
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(GetAccountId(account_id), system_salt_);

  homedirs_->SetLockedToSingleUser();
  brillo::Blob pcr_value;

  if (!tpm_->ReadPCR(kTpmSingleUserPCR, &pcr_value)) {
    LOG(ERROR) << "Failed to read PCR for LockToSingleUserMountUntilReboot()";
    return user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR;
  }

  if (pcr_value != brillo::Blob(pcr_value.size(), 0)) {
    return user_data_auth::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED;
  }

  brillo::Blob extention_blob(obfuscated_username.begin(),
                              obfuscated_username.end());

  if (tpm_->GetVersion() == cryptohome::Tpm::TPM_1_2) {
    extention_blob = Sha1(extention_blob);
  }

  if (!tpm_->ExtendPCR(kTpmSingleUserPCR, extention_blob)) {
    return user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

bool UserDataAuth::OwnerUserExists() {
  AssertOnOriginThread();
  std::string owner;
  return homedirs_->GetPlainOwner(&owner);
}

std::string UserDataAuth::GetStatusString() {
  AssertOnMountThread();

  base::Value mounts(base::Value::Type::LIST);
  for (const auto& session_pair : sessions_) {
    mounts.Append(session_pair.second->GetStatus());
  }
  auto attrs = install_attrs_->GetStatus();

  Tpm::TpmStatusInfo tpm_status_info;
  CryptohomeKeyLoader* rsa_key_loader =
      cryptohome_keys_manager_->GetKeyLoader(CryptohomeKeyType::kRSA);
  if (rsa_key_loader && rsa_key_loader->HasCryptohomeKey()) {
    tpm_->GetStatus(rsa_key_loader->GetCryptohomeKey(), &tpm_status_info);
  } else {
    tpm_->GetStatus(std::nullopt, &tpm_status_info);
  }

  base::Value tpm(base::Value::Type::DICTIONARY);
  tpm.SetBoolKey("can_connect", tpm_status_info.can_connect);
  tpm.SetBoolKey("can_load_srk", tpm_status_info.can_load_srk);
  tpm.SetBoolKey("can_load_srk_pubkey",
                 tpm_status_info.can_load_srk_public_key);
  tpm.SetBoolKey("srk_vulnerable_roca", tpm_status_info.srk_vulnerable_roca);
  tpm.SetBoolKey("has_cryptohome_key", tpm_status_info.has_cryptohome_key);
  tpm.SetBoolKey("can_encrypt", tpm_status_info.can_encrypt);
  tpm.SetBoolKey("can_decrypt", tpm_status_info.can_decrypt);
  tpm.SetBoolKey("has_context", tpm_status_info.this_instance_has_context);
  tpm.SetBoolKey("has_key_handle",
                 tpm_status_info.this_instance_has_key_handle);
  tpm.SetIntKey("last_error", tpm_status_info.last_tpm_error);

  tpm.SetBoolKey("enabled", tpm_->IsEnabled());
  tpm.SetBoolKey("owned", tpm_->IsOwned());

  base::Value dv(base::Value::Type::DICTIONARY);
  dv.SetKey("mounts", std::move(mounts));
  dv.SetKey("installattrs", std::move(attrs));
  dv.SetKey("tpm", std::move(tpm));
  std::string json;
  base::JSONWriter::WriteWithOptions(dv, base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                     &json);
  return json;
}

void UserDataAuth::ResetDictionaryAttackMitigation() {
  AssertOnMountThread();

  // The delegate information is not used.
  brillo::Blob unused_blob;
  if (!tpm_->ResetDictionaryAttackMitigation(unused_blob, unused_blob)) {
    LOG(WARNING) << "Failed to reset DA";
  }
}

void UserDataAuth::UploadAlertsDataCallback() {
  AssertOnMountThread();

  Tpm::AlertsData alerts;

  CHECK(tpm_);
  if (StatusChain<TPMErrorBase> err = tpm_->GetAlertsData(&alerts)) {
    // TODO(b/141294469): Change the code to retry even when it fails.
    LOG(INFO) << "The TPM chip does not support GetAlertsData. Stop "
                 "UploadAlertsData task: "
              << err;
  } else {
    ReportAlertsData(alerts);

    PostTaskToMountThread(
        FROM_HERE,
        base::BindOnce(&UserDataAuth::UploadAlertsDataCallback,
                       base::Unretained(this)),
        base::TimeDelta::FromMilliseconds(upload_alerts_period_ms_));
  }
}

void UserDataAuth::SeedUrandom() {
  AssertOnOriginThread();

  brillo::Blob random;
  if (StatusChain<TPMErrorBase> err =
          tpm_->GetRandomDataBlob(kDefaultRandomSeedLength, &random)) {
    LOG(ERROR) << "Could not get random data from the TPM " << err;
  }
  if (!platform_->WriteFile(FilePath(kDefaultEntropySourcePath), random)) {
    LOG(ERROR) << "Error writing data to " << kDefaultEntropySourcePath;
  }
}

bool UserDataAuth::StartAuthSession(
    user_data_auth::StartAuthSessionRequest request,
    base::OnceCallback<void(const user_data_auth::StartAuthSessionReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::StartAuthSessionReply reply;

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      request.account_id().account_id(), request.flags());
  if (!auth_session) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    std::move(on_done).Run(reply);
    return false;
  }

  reply.set_auth_session_id(auth_session->serialized_token());
  reply.set_user_exists(auth_session->user_exists());
  google::protobuf::Map<std::string, cryptohome::KeyData> proto_key_map(
      auth_session->key_label_data().begin(),
      auth_session->key_label_data().end());
  *(reply.mutable_key_label_data()) = proto_key_map;
  std::move(on_done).Run(reply);

  return true;
}

bool UserDataAuth::AddCredentials(
    user_data_auth::AddCredentialsRequest request,
    base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::AddCredentialsReply reply;

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  if (!auth_session) {
    reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    std::move(on_done).Run(reply);
    return false;
  }

  // Additional check if the user wants to add new credentials for an existing
  // user.
  if (request.add_more_credentials() && !auth_session->user_exists()) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED);
    std::move(on_done).Run(reply);
    return false;
  }

  // Add credentials using data in AuthorizationRequest and
  // auth_session_token.
  reply.set_error(auth_session->AddCredentials(request));
  std::move(on_done).Run(reply);
  return true;
}

void UserDataAuth::UpdateCredential(
    user_data_auth::UpdateCredentialRequest request,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
  user_data_auth::UpdateCredentialReply reply;
  AuthSession* auth_session =
      GetAuthenticatedAuthSession(request.auth_session_id(), &error);
  if (!auth_session) {
    reply.set_error(error);
    std::move(on_done).Run(reply);
    return;
  }
  // Update credentials using data in AuthorizationRequest and
  // auth_session_token.
  reply.set_error(auth_session->UpdateCredential(request));
  std::move(on_done).Run(reply);
  return;
}

bool UserDataAuth::AuthenticateAuthSession(
    user_data_auth::AuthenticateAuthSessionRequest request,
    base::OnceCallback<
        void(const user_data_auth::AuthenticateAuthSessionReply&)> on_done) {
  AssertOnMountThread();

  user_data_auth::AuthenticateAuthSessionReply reply;

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  if (!auth_session) {
    reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    std::move(on_done).Run(reply);
    return false;
  }

  // Perform authentication using data in AuthorizationRequest and
  // auth_session_token.
  // TODO(crbug.com/1157622) : Complete the API with actual authentication.
  reply.set_error(auth_session->Authenticate(request.authorization()));
  reply.set_authenticated(auth_session->GetStatus() ==
                          AuthStatus::kAuthStatusAuthenticated);
  std::move(on_done).Run(reply);
  return false;
}

bool UserDataAuth::InvalidateAuthSession(
    user_data_auth::InvalidateAuthSessionRequest request,
    base::OnceCallback<void(const user_data_auth::InvalidateAuthSessionReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::InvalidateAuthSessionReply reply;
  auth_session_manager_->RemoveAuthSession(request.auth_session_id());
  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::move(on_done).Run(reply);
  return true;
}

bool UserDataAuth::ExtendAuthSession(
    user_data_auth::ExtendAuthSessionRequest request,
    base::OnceCallback<void(const user_data_auth::ExtendAuthSessionReply&)>
        on_done) {
  AssertOnMountThread();

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  user_data_auth::ExtendAuthSessionReply reply;
  if (!auth_session) {
    // Token lookup failed.
    reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    std::move(on_done).Run(reply);
    return false;
  }

  // Extend specified AuthSession.
  auto timer_extension =
      base::TimeDelta::FromSeconds(request.extension_duration());
  reply.set_error(auth_session->ExtendTimer(timer_extension));
  std::move(on_done).Run(reply);
  return true;
}

AuthSession* UserDataAuth::GetAuthenticatedAuthSession(
    const std::string& auth_session_id,
    user_data_auth::CryptohomeErrorCode* error) {
  AssertOnMountThread();

  // Check if the token refers to a valid AuthSession.
  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(auth_session_id);
  if (!auth_session) {
    *error = user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN;
    LOG(ERROR) << "AuthSession not found.";
    return nullptr;
  }

  // Check if the AuthSession is properly authenticated.
  if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
    *error = user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
    LOG(ERROR) << "AuthSession is not authenticated.";
    return nullptr;
  }

  return auth_session;
}

std::string UserDataAuth::SanitizedUserNameForSession(
    const std::string& auth_session_id) {
  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(auth_session_id);
  if (!auth_session) {
    return "";
  }
  return SanitizeUserName(auth_session->username());
}

scoped_refptr<UserSession> UserDataAuth::GetMountableUserSession(
    AuthSession* auth_session, user_data_auth::CryptohomeErrorCode* error) {
  AssertOnMountThread();

  const std::string& obfuscated_username =
      SanitizeUserName(auth_session->username());

  // Check no guest is mounted.
  scoped_refptr<UserSession> guest_session = GetUserSession(guest_user_);
  if (guest_session && guest_session->IsActive()) {
    LOG(ERROR) << "Can not mount non-anonymous while guest session is active.";
    *error = user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY;
    return nullptr;
  }

  // Check the user is not already mounted.
  scoped_refptr<UserSession> session =
      GetOrCreateUserSession(auth_session->username());
  if (session->IsActive()) {
    LOG(ERROR) << "User is already mounted: " << obfuscated_username;
    *error = user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY;
    return nullptr;
  }

  return session;
}

void UserDataAuth::PreMountHook(const std::string& obfuscated_username) {
  AssertOnMountThread();

  LOG(INFO) << "Started mounting for: " << obfuscated_username;

  // Any non-guest mount attempt triggers InstallAttributes finalization.
  // The return value is ignored as it is possible we're pre-ownership.
  // The next login will assure finalization if possible.
  if (install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    install_attrs_->Finalize();
  }
  // Remove all existing cryptohomes, except for the owner's one, if the
  // ephemeral users policy is on.
  // Note that a fresh policy value is read here, which in theory can conflict
  // with the one used for calculation of |mount_args.is_ephemeral|. However,
  // this inconsistency (whose probability is anyway pretty low in practice)
  // should only lead to insignificant transient glitches, like an attempt to
  // mount a non existing anymore cryptohome.
  if (homedirs_->AreEphemeralUsersEnabled()) {
    homedirs_->RemoveNonOwnerCryptohomes();
  }
}

void UserDataAuth::PostMountHook(scoped_refptr<UserSession> user_session,
                                 MountError mount_error) {
  AssertOnMountThread();

  if (mount_error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Finished mounting with status code: " << mount_error;
    return;
  }
  LOG(INFO) << "Mount succeeded.";
  InitializePkcs11(user_session.get());
}

EncryptedContainerType UserDataAuth::DbusEncryptionTypeToContainerType(
    user_data_auth::VaultEncryptionType type) {
  switch (type) {
    case user_data_auth::VaultEncryptionType::CRYPTOHOME_VAULT_ENCRYPTION_ANY:
      return EncryptedContainerType::kUnknown;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS:
      return EncryptedContainerType::kEcryptfs;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_FSCRYPT:
      return EncryptedContainerType::kFscrypt;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_DMCRYPT:
      return EncryptedContainerType::kDmcrypt;
    default:
      // Default cuz proto3 enum sentinels, that's why -_-
      return EncryptedContainerType::kUnknown;
  }
}

void UserDataAuth::PrepareGuestVault(
    user_data_auth::PrepareGuestVaultRequest request,
    base::OnceCallback<void(const user_data_auth::PrepareGuestVaultReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing guest vault";
  user_data_auth::PrepareGuestVaultReply reply;
  reply.set_error(PrepareGuestVaultImpl());
  reply.set_sanitized_username(SanitizeUserName(guest_user_));
  std::move(on_done).Run(reply);
  return;
}

void UserDataAuth::PrepareEphemeralVault(
    user_data_auth::PrepareEphemeralVaultRequest request,
    base::OnceCallback<void(const user_data_auth::PrepareEphemeralVaultReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing ephemeral vault";
  user_data_auth::PrepareEphemeralVaultReply reply;
  reply.set_error(PrepareEphemeralVaultImpl(request.auth_session_id()));
  reply.set_sanitized_username(
      SanitizedUserNameForSession(request.auth_session_id()));
  std::move(on_done).Run(reply);
}

void UserDataAuth::PreparePersistentVault(
    user_data_auth::PreparePersistentVaultRequest request,
    base::OnceCallback<void(const user_data_auth::PreparePersistentVaultReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing persistent vault";
  CryptohomeVault::Options options = {
      .force_type =
          DbusEncryptionTypeToContainerType(request.encryption_type()),
      .block_ecryptfs = request.block_ecryptfs(),
  };
  user_data_auth::PreparePersistentVaultReply reply;
  reply.set_error(
      PreparePersistentVaultImpl(request.auth_session_id(), options));
  reply.set_sanitized_username(
      SanitizedUserNameForSession(request.auth_session_id()));
  std::move(on_done).Run(reply);
}

void UserDataAuth::PrepareVaultForMigration(
    user_data_auth::PrepareVaultForMigrationRequest request,
    base::OnceCallback<
        void(const user_data_auth::PrepareVaultForMigrationReply&)> on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing vault for migration";
  CryptohomeVault::Options options = {
      .migrate = true,
  };
  user_data_auth::PrepareVaultForMigrationReply reply;
  reply.set_error(
      PreparePersistentVaultImpl(request.auth_session_id(), options));
  reply.set_sanitized_username(
      SanitizedUserNameForSession(request.auth_session_id()));
  std::move(on_done).Run(reply);
}  // namespace cryptohome

void UserDataAuth::CreatePersistentUser(
    user_data_auth::CreatePersistentUserRequest request,
    base::OnceCallback<void(const user_data_auth::CreatePersistentUserReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Creating persistent user";
  user_data_auth::CreatePersistentUserReply reply;
  reply.set_error(CreatePersistentUserImpl(request.auth_session_id()));
  reply.set_sanitized_username(
      SanitizedUserNameForSession(request.auth_session_id()));
  std::move(on_done).Run(reply);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::PrepareGuestVaultImpl() {
  AssertOnMountThread();

  if (sessions_.size() != 0) {
    LOG(ERROR) << "Can not mount guest while other sessions are active.";
    return user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL;
  }

  scoped_refptr<UserSession> session = GetOrCreateUserSession(guest_user_);

  LOG(INFO) << "Started mounting for guest";
  ReportTimerStart(kMountGuestExTimer);
  MountError mount_error = session->MountGuest();
  ReportTimerStop(kMountGuestExTimer);
  if (mount_error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Finished mounting with status code: " << mount_error;
  } else {
    LOG(INFO) << "Mount succeeded.";
  }
  return MountErrorToCryptohomeError(mount_error);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::PrepareEphemeralVaultImpl(
    const std::string& auth_session_id) {
  AssertOnMountThread();

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
  AuthSession* auth_session =
      GetAuthenticatedAuthSession(auth_session_id, &error);
  if (!auth_session) {
    return error;
  }
  scoped_refptr<UserSession> session =
      GetMountableUserSession(auth_session, &error);
  if (!session.get()) {
    return error;
  }

  const std::string& obfuscated_username =
      SanitizeUserName(auth_session->username());
  PreMountHook(obfuscated_username);
  ReportTimerStart(kMountExTimer);
  MountError mount_error = session->MountEphemeral(auth_session->username());
  ReportTimerStop(kMountExTimer);
  PostMountHook(session, mount_error);
  return MountErrorToCryptohomeError(mount_error);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::PreparePersistentVaultImpl(
    const std::string& auth_session_id,
    const CryptohomeVault::Options& vault_options) {
  AssertOnMountThread();

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
  AuthSession* auth_session =
      GetAuthenticatedAuthSession(auth_session_id, &error);
  if (!auth_session) {
    return error;
  }

  const std::string& obfuscated_username =
      SanitizeUserName(auth_session->username());
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CryptohomeErrorCode::
        CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  scoped_refptr<UserSession> session =
      GetMountableUserSession(auth_session, &error);
  if (!session.get()) {
    return error;
  }

  PreMountHook(obfuscated_username);
  ReportTimerStart(kMountExTimer);
  MountError mount_error =
      session->MountVault(auth_session->username(),
                          auth_session->file_system_keyset(), vault_options);
  if (mount_error == MOUNT_ERROR_TPM_COMM_ERROR) {
    LOG(WARNING) << "TPM communication error. Retrying.";
    mount_error =
        session->MountVault(auth_session->username(),
                            auth_session->file_system_keyset(), vault_options);
  }
  ReportTimerStop(kMountExTimer);
  PostMountHook(session, mount_error);
  return MountErrorToCryptohomeError(mount_error);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::CreatePersistentUserImpl(
    const std::string& auth_session_id) {
  AssertOnMountThread();

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(auth_session_id);
  if (!auth_session) {
    LOG(ERROR) << "AuthSession not found.";
    return user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN;
  }

  const std::string& obfuscated_username =
      SanitizeUserName(auth_session->username());
  MountError mount_error = MOUNT_ERROR_NONE;

  // This checks presence of the actual encrypted vault. We fail if Create is
  // called while actual persistent vault is present.
  if (homedirs_->CryptohomeExists(obfuscated_username, &mount_error)) {
    LOG(ERROR) << "User already exists: " << obfuscated_username;
    // TODO(b/208898186, dlunev): replace with a more appropriate error
    return user_data_auth::CryptohomeErrorCode::
        CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY;
  }

  if (mount_error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Failed to query vault existance for: " << obfuscated_username
               << ", code: " << mount_error;
    return MountErrorToCryptohomeError(mount_error);
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
      !homedirs_->Create(auth_session->username())) {
    LOG(ERROR) << "Failed to create shadow directory for: "
               << obfuscated_username;
    return user_data_auth::CryptohomeErrorCode::
        CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }

  // Let the auth session perform any finalization operations for a newly
  // created user.
  return auth_session->OnUserCreated();
}

bool UserDataAuth::AddAuthFactor(
    user_data_auth::AddAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();
  // TODO(b/3319388): Implement AddAuthFactor.
  user_data_auth::AddAuthFactorReply reply;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
  AuthSession* auth_session =
      GetAuthenticatedAuthSession(request.auth_session_id(), &error);
  if (!auth_session) {
    reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    std::move(on_done).Run(reply);
    return false;
  }
  auth_session->AddAuthFactor(request);

  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
  std::move(on_done).Run(reply);
  return true;
}

bool UserDataAuth::AuthenticateAuthFactor(
    user_data_auth::AuthenticateAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();
  // TODO(b/208358041): Implement AuthenticateAUthFactor.
  user_data_auth::AuthenticateAuthFactorReply reply;
  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
  std::move(on_done).Run(reply);
  return true;
}

bool UserDataAuth::UpdateAuthFactor(
    user_data_auth::UpdateAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::UpdateAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();
  // TODO(b/208357704): Implement UpdateAuthFactor.
  user_data_auth::UpdateAuthFactorReply reply;
  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
  std::move(on_done).Run(reply);
  return true;
}

bool UserDataAuth::RemoveAuthFactor(
    user_data_auth::RemoveAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::RemoveAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();
  // TODO(b/208357933): Implement RemoveAuthFactor.
  user_data_auth::RemoveAuthFactorReply reply;
  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
  std::move(on_done).Run(reply);
  return true;
}

}  // namespace cryptohome
