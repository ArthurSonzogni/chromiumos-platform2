// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/service.h"
#if USE_TPM2
#include "cryptohome/service_distributed.h"
#endif
#include "cryptohome/service_monolithic.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/sys_string_conversions.h>
#include <base/time/time.h>
#include <base/values.h>
#include <brillo/cryptohome.h>
#include <brillo/glib/dbus.h>
#include <brillo/secure_blob.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <map>
#include <string>
#include <vector>

#include "cryptohome/boot_attributes.h"
#include "cryptohome/boot_lockbox.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_event_source.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/dbus_transition.h"
#include "cryptohome/firmware_management_parameters.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/interface.h"
#include "cryptohome/mount.h"
#include "cryptohome/platform.h"
#include "cryptohome/stateful_recovery.h"
#include "cryptohome/tpm.h"
#include "cryptohome/username_passkey.h"

#include "key.pb.h"  // NOLINT(build/include)
#include "rpc.pb.h"  // NOLINT(build/include)
#include "vault_keyset.pb.h"  // NOLINT(build/include)

using base::FilePath;
using brillo::SecureBlob;

// Forcibly namespace the dbus-bindings generated server bindings instead of
// modifying the files afterward.
namespace cryptohome {
namespace gobject {
#include "bindings/cryptohome.dbusserver.h"  // NOLINT(build/include_alpha)
}  // namespace gobject
}  // namespace cryptohome

namespace cryptohome {

namespace {

const std::string& GetAccountId(const AccountIdentifier& id) {
  if (id.has_account_id()) {
    return id.account_id();
  }

  return id.email();
}

}  // anonymous namespace

const char kSaltFile[] = "salt";
const char kPublicMountSaltFilePath[] = "/var/lib/public_mount_salt";
const char kChapsSystemToken[] = "/var/lib/chaps";
const int kAutoCleanupPeriodMS = 1000 * 60 * 60;  // 1 hour
const int kUpdateUserActivityPeriod = 24;  // divider of the former
const int kLowDiskNotificationPeriodMS = 1000 * 60 * 1;  // 1 minute
const int64_t kNotifyDiskSpaceThreshold = 1 << 30;  // 1GB
const int kDefaultRandomSeedLength = 64;
const char kMountThreadName[] = "MountThread";
const char kTpmInitStatusEventType[] = "TpmInitStatus";
const char kDircryptoMigrationProgressEventType[] =
                                               "DircryptoMigrationProgress";

// The default entropy source to seed with random data from the TPM on startup.
const FilePath kDefaultEntropySource("/dev/urandom");

// Location of the path to store basic device enrollment information that
// will persist across powerwashes.
const FilePath kPreservedEnrollmentStatePath(
    "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb");
const mode_t kPreservedEnrollmentStatePermissions = 0600;

#if USE_TPM2
const bool kUseInternalAttestationModeByDefault = false;
const char kAttestationMode[] = "attestation_mode";
#endif

const char kAutoInitializeTpmSwitch[] = "auto_initialize_tpm";

class TpmInitStatus : public CryptohomeEventBase {
 public:
  TpmInitStatus()
      : took_ownership_(false),
        status_(false) { }
  virtual ~TpmInitStatus() { }

  virtual const char* GetEventName() const {
    return kTpmInitStatusEventType;
  }

  void set_took_ownership(bool value) {
    took_ownership_ = value;
  }

  bool get_took_ownership() {
    return took_ownership_;
  }

  void set_status(bool value) {
    status_ = value;
  }

  bool get_status() {
    return status_;
  }

 private:
  bool took_ownership_;
  bool status_;
};

class DircryptoMigrationProgress : public CryptohomeEventBase {
 public:
  DircryptoMigrationProgress(DircryptoMigrationStatus status,
                             uint64_t current_bytes,
                             uint64_t total_bytes)
      : status_(status),
        current_bytes_(current_bytes),
        total_bytes_(total_bytes) { }
  ~DircryptoMigrationProgress() override = default;

  const char* GetEventName() const override {
    return kDircryptoMigrationProgressEventType;
  }

  DircryptoMigrationStatus status() const { return status_; }
  uint64_t current_bytes() const { return current_bytes_; }
  uint64_t total_bytes() const { return total_bytes_; }

 private:
  DircryptoMigrationStatus status_;
  uint64_t current_bytes_;
  uint64_t total_bytes_;

  DISALLOW_COPY_AND_ASSIGN(DircryptoMigrationProgress);
};

Service::Service()
    : use_tpm_(true),
      loop_(NULL),
      cryptohome_(NULL),
      system_salt_(),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      default_crypto_(new Crypto(platform_)),
      crypto_(default_crypto_.get()),
      tpm_(nullptr),
      tpm_init_(nullptr),
      default_pkcs11_init_(new Pkcs11Init()),
      pkcs11_init_(default_pkcs11_init_.get()),
      initialize_tpm_(true),
      mount_thread_(kMountThreadName),
      async_complete_signal_(-1),
      async_data_complete_signal_(-1),
      tpm_init_signal_(-1),
      low_disk_space_signal_(-1),
      dircrypto_migration_progress_signal_(-1),
      event_source_(),
      event_source_sink_(this),
      auto_cleanup_period_(kAutoCleanupPeriodMS),
      default_install_attrs_(new cryptohome::InstallAttributes(NULL)),
      install_attrs_(default_install_attrs_.get()),
      update_user_activity_period_(kUpdateUserActivityPeriod - 1),
      reported_pkcs11_init_fail_(false),
      enterprise_owned_(false),
      mounts_lock_(),
      user_timestamp_cache_(new UserOldestActivityTimestampCache()),
      default_mount_factory_(new cryptohome::MountFactory()),
      mount_factory_(default_mount_factory_.get()),
      default_reply_factory_(new cryptohome::DBusReplyFactory),
      reply_factory_(default_reply_factory_.get()),
      default_homedirs_(new cryptohome::HomeDirs()),
      homedirs_(default_homedirs_.get()),
      guest_user_(brillo::cryptohome::home::kGuestUserName),
      force_ecryptfs_(true),
      legacy_mount_(true),
      public_mount_salt_(),
      default_chaps_client_(new chaps::TokenManagerClient()),
      chaps_client_(default_chaps_client_.get()),
      boot_lockbox_(nullptr),
      boot_attributes_(nullptr),
      firmware_management_parameters_(nullptr),
      low_disk_notification_period_ms_(kLowDiskNotificationPeriodMS) {
}

Service::~Service() {
  mount_thread_.Stop();
  if (loop_) {
    g_main_loop_unref(loop_);
  }
  if (cryptohome_) {
    g_object_unref(cryptohome_);
  }
}

void Service::StopTasks() {
  LOG(INFO) << "Stopping cryptohome task processing.";
  if (loop_) {
    g_main_loop_quit(loop_);
  }
  // It is safe to call Stop() multiple times
  mount_thread_.Stop();
}

Service* Service::CreateDefault(const std::string& abe_data) {
#if USE_TPM2
  bool use_monolithic = kUseInternalAttestationModeByDefault;
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();

  if (cmd_line->HasSwitch(kAttestationMode)) {
    std::string name = cmd_line->GetSwitchValueASCII(kAttestationMode);
    if (name == "internal")
      use_monolithic = true;
    else if (name == "dbus")
      use_monolithic = false;
  }
  if (use_monolithic)
    return new ServiceMonolithic(abe_data);
  else
    return new ServiceDistributed();
#else
  return new ServiceMonolithic(abe_data);
#endif
}

bool Service::GetExistingMounts(
    std::multimap<const FilePath, const FilePath>* mounts) {
  bool found = platform_->GetMountsBySourcePrefix(homedirs_->shadow_root(),
                                                  mounts);
  found |= platform_->GetMountsBySourcePrefix(FilePath(kEphemeralDir),
                                              mounts);
  found |= platform_->GetMountsBySourcePrefix(FilePath(kGuestMountPath),
                                              mounts);
  return found;
}

static bool PrefixPresent(const std::vector<FilePath>& prefixes,
                          const std::string path) {
  for (const auto& prefix : prefixes)
    if (base::StartsWith(path, prefix.value(),
                         base::CompareCase::INSENSITIVE_ASCII))
      return true;
  return false;
}

bool Service::UnloadPkcs11Tokens(const std::vector<FilePath>& exclude) {
  SecureBlob isolate =
      chaps::IsolateCredentialManager::GetDefaultIsolateCredential();
  std::vector<std::string> tokens;
  if (!chaps_client_->GetTokenList(isolate, &tokens))
    return false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] != kChapsSystemToken && !PrefixPresent(exclude, tokens[i])) {
      LOG(INFO) << "Cleaning up PKCS #11 token: " << tokens[i];
      chaps_client_->UnloadToken(isolate, FilePath(tokens[i]));
    }
  }
  return true;
}

CryptohomeErrorCode Service::MountErrorToCryptohomeError(
    const MountError code) const {
  switch (code) {
  case MOUNT_ERROR_FATAL:
    return CRYPTOHOME_ERROR_MOUNT_FATAL;
  case MOUNT_ERROR_KEY_FAILURE:
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  case MOUNT_ERROR_MOUNT_POINT_BUSY:
    return CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY;
  case MOUNT_ERROR_TPM_COMM_ERROR:
    return CRYPTOHOME_ERROR_TPM_COMM_ERROR;
  case MOUNT_ERROR_TPM_DEFEND_LOCK:
    return CRYPTOHOME_ERROR_TPM_DEFEND_LOCK;
  case MOUNT_ERROR_USER_DOES_NOT_EXIST:
    return CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  case MOUNT_ERROR_TPM_NEEDS_REBOOT:
    return CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT;
  case MOUNT_ERROR_OLD_ENCRYPTION:
    return CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION;
  case MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE:
    return CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE;
  case MOUNT_ERROR_RECREATED:
  default:
    return CRYPTOHOME_ERROR_NOT_SET;
  }
}

void Service::SendReply(DBusGMethodInvocation* context,
                        const BaseReply& reply) {
  // DBusReply will take ownership of the |reply_str|.
  std::unique_ptr<std::string> reply_str(new std::string);
  reply.SerializeToString(reply_str.get());
  event_source_.AddEvent(reply_factory_->NewReply(context,
                                                  reply_str.release()));
}

void Service::SendDBusErrorReply(DBusGMethodInvocation* context,
                                 GQuark domain,
                                 gint code,
                                 const gchar* message) {
    GError* error = g_error_new_literal(domain, code, message);
    DBusErrorReply* reply_cb = reply_factory_->NewErrorReply(context, error);
    event_source_.AddEvent(reply_cb);
}

bool Service::CleanUpStaleMounts(bool force) {
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
  bool skipped = false;
  std::multimap<const FilePath, const FilePath> matches;
  std::vector<FilePath> exclude;
  if (!GetExistingMounts(&matches)) {
    // If there's no existing mounts, go ahead and unload all chaps tokens by
    // passing an empty exclude list.
    UnloadPkcs11Tokens(exclude);
    return skipped;
  }

  for (auto match = matches.begin(); match != matches.end(); ) {
    auto curr = match;
    bool keep = false;
    // Walk each set of sources as one group since multimaps are key ordered.
    for (; match != matches.end() && match->first == curr->first; ++match) {
      // Ignore known mounts.
      mounts_lock_.Acquire();
      for (const auto& mount_pair : mounts_) {
        if (mount_pair.second->OwnsMountPoint(match->second)) {
          keep = true;
          break;
        }
      }
      mounts_lock_.Release();
      // Optionally, ignore mounts with open files.
      if (!force) {
        std::vector<ProcessInformation> processes;
        platform_->GetProcessesWithOpenFiles(match->second, &processes);
        if (processes.size()) {
          LOG(WARNING) << "Stale mount " << match->second.value()
                       << " from " << match->first.value()
                       << " has active holders.";
          keep = true;
          skipped = true;
        }
      }
    }

    // Delete anything that shouldn't be unmounted.
    if (keep) {
      for (auto it = curr; it != match; ++it)
        exclude.push_back(it->second);
      matches.erase(curr, match);
    }
  }
  UnloadPkcs11Tokens(exclude);
  // Unmount anything left.
  for (const auto& match : matches) {
    LOG(WARNING) << "Lazily unmounting stale mount: " << match.second.value()
                 << " from " << match.first.value();
    platform_->Unmount(match.second, true, NULL);
  }
  return skipped;
}

bool Service::CleanUpHiddenMounts() {
  bool ok = true;
  mounts_lock_.Acquire();
  for (auto it = mounts_.begin(); it != mounts_.end();) {
    scoped_refptr<cryptohome::Mount> mount = it->second;
    if (mount->IsMounted() && mount->IsShadowOnly()) {
      ok = ok && mount->UnmountCryptohome();
      it = mounts_.erase(it);
    } else {
      ++it;
    }
  }
  mounts_lock_.Release();
  return ok;
}

bool Service::Initialize() {
  bool result = true;
  if (!tpm_ && use_tpm_) {
    tpm_ = Tpm::GetSingleton();
  }
  if (!tpm_init_ && initialize_tpm_) {
    default_tpm_init_.reset(new TpmInit(tpm_, platform_));
    tpm_init_ = default_tpm_init_.get();
  }
  if (!boot_lockbox_) {
    default_boot_lockbox_.reset(new BootLockbox(tpm_, platform_, crypto_));
    boot_lockbox_ = default_boot_lockbox_.get();
  }
  if (!boot_attributes_) {
    default_boot_attributes_.reset(
        new BootAttributes(boot_lockbox_, platform_));
    boot_attributes_ = default_boot_attributes_.get();
  }
  if (!firmware_management_parameters_) {
    default_firmware_management_params_.reset(
        new FirmwareManagementParameters(tpm_));
    firmware_management_parameters_ = default_firmware_management_params_.get();
  }
  crypto_->set_use_tpm(use_tpm_);
  if (!crypto_->Init(tpm_init_))
    return false;
  if (!homedirs_->Init(platform_, crypto_, user_timestamp_cache_.get()))
    return false;

  // If the TPM is unowned or doesn't exist, it's safe for
  // this function to be called again. However, it shouldn't
  // be called across multiple threads in parallel.
  InitializeInstallAttributes(false);

  // Clean up any unreferenced mountpoints at startup.
  CleanUpStaleMounts(false);

  AttestationInitialize();

  // Install the type-info for the service with dbus.
  dbus_g_object_type_install_info(gobject::cryptohome_get_type(),
                                  &gobject::dbus_glib_cryptohome_object_info);
  if (!Reset()) {
    result = false;
  }

  async_complete_signal_ = g_signal_lookup("async_call_status",
                                           gobject::cryptohome_get_type());
  if (!async_complete_signal_) {
    async_complete_signal_ = g_signal_new("async_call_status",
                                          gobject::cryptohome_get_type(),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL,
                                          NULL,
                                          nullptr,
                                          G_TYPE_NONE,
                                          3,
                                          G_TYPE_INT,
                                          G_TYPE_BOOLEAN,
                                          G_TYPE_INT);
  }

  async_data_complete_signal_ = g_signal_lookup("async_call_status_with_data",
                                                gobject::cryptohome_get_type());
  if (!async_data_complete_signal_) {
    async_data_complete_signal_ = g_signal_new(
        "async_call_status_with_data",
        gobject::cryptohome_get_type(),
        G_SIGNAL_RUN_LAST,
        0,
        NULL,
        NULL,
        nullptr,
        G_TYPE_NONE,
        3,
        G_TYPE_INT,
        G_TYPE_BOOLEAN,
        DBUS_TYPE_G_UCHAR_ARRAY);
  }

  tpm_init_signal_ = g_signal_lookup("tpm_init_status",
                                     gobject::cryptohome_get_type());
  if (!tpm_init_signal_) {
    tpm_init_signal_ = g_signal_new("tpm_init_status",
                                    gobject::cryptohome_get_type(),
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL,
                                    NULL,
                                    nullptr,
                                    G_TYPE_NONE,
                                    3,
                                    G_TYPE_BOOLEAN,
                                    G_TYPE_BOOLEAN,
                                    G_TYPE_BOOLEAN);
  }

  low_disk_space_signal_ = g_signal_lookup("low_disk_space",
                                           gobject::cryptohome_get_type());
  if (!low_disk_space_signal_) {
    low_disk_space_signal_ = g_signal_new("low_disk_space",
                                          gobject::cryptohome_get_type(),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL,
                                          NULL,
                                          nullptr,
                                          G_TYPE_NONE,
                                          1,
                                          G_TYPE_UINT64);
  }

  dircrypto_migration_progress_signal_ = g_signal_lookup(
      "dircrypto_migration_progress",
      gobject::cryptohome_get_type());
  if (!dircrypto_migration_progress_signal_) {
    dircrypto_migration_progress_signal_ = g_signal_new(
        "dircrypto_migration_progress",
        gobject::cryptohome_get_type(),
        G_SIGNAL_RUN_LAST,
        0,
        NULL,
        NULL,
        nullptr,
        G_TYPE_NONE,
        3,
        G_TYPE_INT,
        G_TYPE_UINT64,
        G_TYPE_UINT64);
  }

  mount_thread_.Start();

  // TODO(wad) Determine if this should only be called if
  //           tpm->IsEnabled() is true.
  if (tpm_ && initialize_tpm_) {
    tpm_init_->Init(
        base::Bind(&Service::OwnershipCallback, base::Unretained(this)));
    if (!SeedUrandom()) {
      LOG(ERROR) << "FAILED TO SEED /dev/urandom AT START";
    }
    AttestationInitializeTpm();
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kAutoInitializeTpmSwitch)) {
      tpm_init_->AsyncTakeOwnership();
    }
  }

  // Start scheduling periodic cleanup events. Subsequent events are scheduled
  // by the callback itself.
  mount_thread_.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&Service::AutoCleanupCallback, base::Unretained(this)));

  // Start scheduling periodic check for low-disk space.  Subsequent events are
  // scheduled by teh callback itself.
  mount_thread_.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&Service::LowDiskCallback, base::Unretained(this)));

  // TODO(keescook,ellyjones) Make this mock-able.
  StatefulRecovery recovery(platform_, this);
  if (recovery.Requested()) {
    if (recovery.Recover())
      LOG(INFO) << "A stateful recovery was performed successfully.";
    recovery.PerformReboot();
  }

  boot_attributes_->Load();

  return result;
}

bool Service::IsOwner(const std::string &userid) {
  std::string owner;
  if (homedirs_->GetPlainOwner(&owner) && userid.length() && userid == owner)
    return true;
  return false;
}

void Service::InitializeInstallAttributes(bool first_time) {
  // Wait for ownership if there is a working TPM.
  if (tpm_ && tpm_->IsEnabled() && !tpm_->IsOwned())
    return;

  // The TPM owning instance may have changed since initialization.
  // InstallAttributes can handle a NULL or !IsEnabled Tpm object.
  install_attrs_->SetTpm(tpm_);

  if (first_time && !install_attrs_->PrepareSystem()) {
    // TODO(wad) persist this failure to allow recovery or force
    //           powerwash/reset.
    LOG(ERROR) << "Unable to prepare system for install attributes.";
  }

  // Init can fail without making the interface inconsistent so we're okay here.
  install_attrs_->Init(tpm_init_);

  // Check if the machine is enterprise owned and report to mount_ then.
  DetectEnterpriseOwnership();
}

void Service::InitializePkcs11(cryptohome::Mount* mount) {
  if (!mount) {
    LOG(ERROR) << "InitializePkcs11 called with NULL mount!";
    return;
  }
  // Wait for ownership if there is a working TPM.
  if (tpm_ && tpm_->IsEnabled() && !tpm_->IsOwned()) {
    LOG(WARNING) << "TPM was not owned. TPM initialization call back will"
                 << " handle PKCS#11 initialization.";
    mount->set_pkcs11_state(cryptohome::Mount::kIsWaitingOnTPM);
    return;
  }

  // Ok, so the TPM is owned. Time to request asynchronous initialization of
  // PKCS#11.
  // Make sure cryptohome is mounted, otherwise all of this is for naught.
  if (!mount->IsMounted()) {
    LOG(WARNING) << "PKCS#11 initialization requested but cryptohome is"
                 << " not mounted.";
    return;
  }

  // Reset PKCS#11 initialization status. A successful completion of
  // MountTaskPkcs11_Init would set it in the service thread via NotifyEvent().
  ReportTimerStart(kPkcs11InitTimer);
  mount->set_pkcs11_state(cryptohome::Mount::kIsBeingInitialized);
  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(mount, &event_source_);
  scoped_refptr<MountTaskPkcs11Init> pkcs11_init_task =
      new MountTaskPkcs11Init(bridge, mount);
  LOG(INFO) << "Putting a Pkcs11_Initialize on the mount thread.";
  pkcs11_tasks_[pkcs11_init_task->sequence_id()] = pkcs11_init_task.get();
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskPkcs11Init::Run, pkcs11_init_task.get()));
}

bool Service::SeedUrandom() {
  SecureBlob random;
  if (!tpm_->GetRandomData(kDefaultRandomSeedLength, &random)) {
    LOG(ERROR) << "Could not get random data from the TPM";
    return false;
  }
  if (!platform_->WriteFile(kDefaultEntropySource, random)) {
    LOG(ERROR) << "Error writing data to " << kDefaultEntropySource.value();
    return false;
  }
  return true;
}

bool Service::Reset() {
  if (cryptohome_)
    g_object_unref(cryptohome_);
  cryptohome_ = reinterpret_cast<gobject::Cryptohome*>(
      g_object_new(gobject::cryptohome_get_type(), NULL));
  // Allow references to this instance.
  cryptohome_->service = this;

  if (loop_) {
    ::g_main_loop_unref(loop_);
  }
  loop_ = g_main_loop_new(NULL, false);
  if (!loop_) {
    LOG(ERROR) << "Failed to create main loop";
    return false;
  }

  // Install the local event source for handling async results
  event_source_.Reset(event_source_sink_, g_main_loop_get_context(loop_));
  return true;
}

void Service::NotifyEvent(CryptohomeEventBase* event) {
  if (!strcmp(event->GetEventName(), kMountTaskResultEventType)) {
    MountTaskResult* result = static_cast<MountTaskResult*>(event);
    if (!result->return_data()) {
      g_signal_emit(cryptohome_,
                    async_complete_signal_,
                    0,
                    result->sequence_id(),
                    result->return_status(),
                    result->return_code());
      // TODO(wad) are there any non-mount uses of this type?
      if (!result->return_status()) {
        RemoveMount(result->mount().get());
      }
    } else {
      brillo::glib::ScopedArray tmp_array(g_array_new(FALSE, FALSE, 1));
      g_array_append_vals(tmp_array.get(),
                          result->return_data()->data(),
                          result->return_data()->size());
      g_signal_emit(cryptohome_,
                    async_data_complete_signal_,
                    0,
                    result->sequence_id(),
                    result->return_status(),
                    tmp_array.get());
      brillo::SecureMemset(tmp_array.get()->data, 0, tmp_array.get()->len);
    }
    if (result->pkcs11_init()) {
      LOG(INFO) << "An asynchronous mount request with sequence id: "
                << result->sequence_id()
                << " finished; doing PKCS11 init...";
      // We only report and init PKCS#11 for successful mounts.
      if (result->return_status()) {
        if (!result->return_code()) {
          ReportTimerStop(kAsyncMountTimer);
        }
        // A return code of MOUNT_RECREATED will still need PKCS#11 init.
        InitializePkcs11(result->mount().get());
      }
    } else if (result->guest()) {
      if (!result->return_status()) {
        DLOG(INFO) << "Dropping MountMap entry for failed Guest mount.";
        RemoveMountForUser(guest_user_);
      }
      if (result->return_status() && !result->return_code()) {
        ReportTimerStop(kAsyncGuestMountTimer);
      }
    }
  } else if (!strcmp(event->GetEventName(), kTpmInitStatusEventType)) {
    TpmInitStatus* result = static_cast<TpmInitStatus*>(event);
    g_signal_emit(cryptohome_, tpm_init_signal_, 0, tpm_init_->IsTpmReady(),
                  tpm_init_->IsTpmEnabled(), result->get_took_ownership());
    // TODO(wad) should we package up a InstallAttributes status here too?
  } else if (!strcmp(event->GetEventName(), kPkcs11InitResultEventType)) {
    LOG(INFO) << "A Pkcs11_Init event got finished.";
    MountTaskResult* result = static_cast<MountTaskResult*>(event);
    // Drop the reference since the work is done.
    pkcs11_tasks_.erase(result->sequence_id());
    if (result->return_status()) {
      ReportTimerStop(kPkcs11InitTimer);
      LOG(INFO) << "PKCS#11 initialization succeeded.";
      result->mount()->set_pkcs11_state(cryptohome::Mount::kIsInitialized);
      return;
    }
    LOG(ERROR) << "PKCS#11 initialization failed.";
    result->mount()->set_pkcs11_state(cryptohome::Mount::kIsFailed);
  } else if (!strcmp(event->GetEventName(), kDBusErrorReplyEventType)) {
    DBusErrorReply* result = static_cast<DBusErrorReply*>(event);
    result->Run();
  } else if (!strcmp(event->GetEventName(), kDBusReplyEventType)) {
    DBusReply* result = static_cast<DBusReply*>(event);
    result->Run();
  } else if (!strcmp(event->GetEventName(),
                     kDircryptoMigrationProgressEventType)) {
    auto* progress = static_cast<DircryptoMigrationProgress*>(event);
    g_signal_emit(cryptohome_, dircrypto_migration_progress_signal_,
                  0 /* signal detail (not used) */,
                  static_cast<int32_t>(progress->status()),
                  progress->current_bytes(), progress->total_bytes());
  }
}

void Service::OwnershipCallback(bool status, bool took_ownership) {
  if (took_ownership) {
    ReportTimerStop(kTpmTakeOwnershipTimer);
    // When TPM initialization finishes, we need to tell every Mount to
    // reinitialize its TPM context, since the TPM is now useable, and we might
    // need to kick off their PKCS11 initialization if they were blocked before.
    mounts_lock_.Acquire();
    for (const auto& mount_pair : mounts_) {
      scoped_refptr<MountTaskResetTpmContext> mount_task =
          new MountTaskResetTpmContext(NULL, mount_pair.second.get());
      mount_thread_.message_loop()->PostTask(FROM_HERE,
          base::Bind(&MountTaskResetTpmContext::Run, mount_task.get()));
    }
    mounts_lock_.Release();
  }
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::ConfigureOwnedTpm, base::Unretained(this),
                 status, took_ownership));
}

void Service::ConfigureOwnedTpm(bool status, bool took_ownership) {
  LOG(INFO) << "Configuring TPM, ownership taken: " << took_ownership << ".";
  if (took_ownership) {
    // Check if we have pending pkcs11 init tasks due to tpm ownership
    // not being done earlier. Trigger initialization if so.
    mounts_lock_.Acquire();
    for (const auto& mount_pair : mounts_) {
      cryptohome::Mount* mount = mount_pair.second.get();
      if (mount->pkcs11_state() == cryptohome::Mount::kIsWaitingOnTPM) {
        InitializePkcs11(mount);
      }
    }
    mounts_lock_.Release();
    // Initialize the install-time locked attributes since we
    // can't do it prior to ownership.
    InitializeInstallAttributes(true);
  }
  // The event source will free this object
  TpmInitStatus* tpm_init_status = new TpmInitStatus();
  tpm_init_status->set_status(status);
  tpm_init_status->set_took_ownership(took_ownership);
  event_source_.AddEvent(tpm_init_status);

  // Do attestation work after AddEvent because it may take long.
  AttestationInitializeTpmComplete();

  // If we mounted before the TPM finished initialization, we must
  // finalize the install attributes now too, otherwise it takes a
  // full re-login cycle to finalize.
  gboolean mounted = FALSE;
  bool is_mounted = (IsMounted(&mounted, NULL) && mounted);
  if (is_mounted && took_ownership && install_attrs_->is_first_install()) {
    install_attrs_->Finalize();
  }
}

gboolean Service::CheckKey(gchar *userid,
                           gchar *key,
                           gboolean *OUT_result,
                           GError **error) {
  UsernamePasskey credentials(userid, SecureBlob(key, key + strlen(key)));
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    if (mount_pair.second->AreSameUser(credentials)) {
      *OUT_result = mount_pair.second->AreValid(credentials);
      mounts_lock_.Release();
      return TRUE;
    }
  }
  mounts_lock_.Release();

  MountTaskResult result;
  base::WaitableEvent event(true, false);
  scoped_refptr<MountTaskTestCredentials> mount_task =
      new MountTaskTestCredentials(NULL, NULL, homedirs_, credentials);
  mount_task->set_result(&result);
  mount_task->set_complete_event(&event);
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskTestCredentials::Run, mount_task.get()));
  event.Wait();
  *OUT_result = result.return_status();
  return TRUE;
}

gboolean Service::AsyncCheckKey(gchar *userid,
                                gchar *key,
                                gint *OUT_async_id,
                                GError **error) {
  UsernamePasskey credentials(userid, SecureBlob(key, key + strlen(key)));
  // Freed by the message loop
  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(NULL, &event_source_);
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    // Fast path - because we can check credentials on a Mount very fast, we can
    // afford to check them synchronously here and post the result
    // asynchronously.
    if (mount_pair.second->AreSameUser(credentials)) {
      bool ok = mount_pair.second->AreValid(credentials);
      *OUT_async_id = PostAsyncCallResult(bridge, MOUNT_ERROR_NONE, ok);
      mounts_lock_.Release();
      return TRUE;
    }
  }
  mounts_lock_.Release();

  // Slow path - ask the HomeDirs to check credentials.
  scoped_refptr<MountTaskTestCredentials> mount_task
      = new MountTaskTestCredentials(bridge, NULL, homedirs_, credentials);
  *OUT_async_id = mount_task->sequence_id();
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskTestCredentials::Run, mount_task.get()));
  return TRUE;
}

void Service::DoCheckKeyEx(AccountIdentifier* identifier,
                           AuthorizationRequest* authorization,
                           CheckKeyRequest* check_key_request,
                           DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !check_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  UsernamePasskey credentials(GetAccountId(*identifier).c_str(),
                              SecureBlob(authorization->key().secret().begin(),
                                         authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  BaseReply reply;
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    if (mount_pair.second->AreSameUser(credentials)) {
      if (!mount_pair.second->AreValid(credentials)) {
        // Fallthrough to HomeDirs to cover different keys for the same user.
        break;
      }
      mounts_lock_.Release();
      SendReply(context, reply);
      return;
    }
  }
  mounts_lock_.Release();

  if (!homedirs_->Exists(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  } else if (!homedirs_->AreCredentialsValid(credentials)) {
    // TODO(wad) Should this pass along KEY_NOT_FOUND too?
    reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }
  SendReply(context, reply);
}

gboolean Service::CheckKeyEx(GArray* account_id,
                             GArray* authorization_request,
                             GArray* check_key_request,
                             DBusGMethodInvocation *context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<CheckKeyRequest> request(new CheckKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(check_key_request->data, check_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoCheckKeyEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoRemoveKeyEx(AccountIdentifier* identifier,
                            AuthorizationRequest* authorization,
                            RemoveKeyRequest* remove_key_request,
                            DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !remove_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  if (remove_key_request->key().data().label().empty()) {
    SendInvalidArgsReply(context, "No label provided for target key");
    return;
  }

  BaseReply reply;
  UsernamePasskey credentials(GetAccountId(*identifier).c_str(),
                              SecureBlob(authorization->key().secret().begin(),
                                         authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  if (!homedirs_->Exists(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  reply.set_error(homedirs_->RemoveKeyset(credentials,
                                          remove_key_request->key().data()));
  if (reply.error() == CRYPTOHOME_ERROR_NOT_SET) {
    // Don't set the error if there wasn't one.
    reply.clear_error();
  }
  SendReply(context, reply);
}

gboolean Service::RemoveKeyEx(GArray* account_id,
                             GArray* authorization_request,
                             GArray* remove_key_request,
                             DBusGMethodInvocation *context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<RemoveKeyRequest> request(new RemoveKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(remove_key_request->data,
                               remove_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoRemoveKeyEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoListKeysEx(AccountIdentifier* identifier,
                            AuthorizationRequest* authorization,
                            ListKeysRequest* list_keys_request,
                            DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !list_keys_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }
  BaseReply reply;
  UsernamePasskey credentials(GetAccountId(*identifier).c_str(), SecureBlob());
  if (!homedirs_->Exists(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }
  std::vector<std::string> labels;
  if (!homedirs_->GetVaultKeysetLabels(credentials, &labels)) {
    reply.set_error(CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }
  ListKeysReply* list_keys_reply = reply.MutableExtension(ListKeysReply::reply);

  for (const auto& label : labels)
    list_keys_reply->add_labels(label);

  SendReply(context, reply);
}

gboolean Service::ListKeysEx(GArray* account_id,
                             GArray* authorization_request,
                             GArray* list_keys_request,
                             DBusGMethodInvocation *context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<ListKeysRequest> request(new ListKeysRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(list_keys_request->data,
                               list_keys_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoListKeysEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetKeyDataEx(AccountIdentifier* identifier,
                             AuthorizationRequest* authorization,
                             GetKeyDataRequest* get_key_data_request,
                             DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !get_key_data_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  if (!get_key_data_request->has_key()) {
    SendInvalidArgsReply(context, "No key attributes provided");
    return;
  }

  BaseReply reply;
  UsernamePasskey credentials(GetAccountId(*identifier).c_str(), SecureBlob());
  if (!homedirs_->Exists(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  GetKeyDataReply* sub_reply = reply.MutableExtension(GetKeyDataReply::reply);
  credentials.set_key_data(get_key_data_request->key().data());
  // Requests only support using the key label at present.
  std::unique_ptr<VaultKeyset> vk(homedirs_->GetVaultKeyset(credentials));
  if (vk) {
    KeyData* new_kd = sub_reply->add_key_data();
    *new_kd = vk->serialized().key_data();
    // Clear any symmetric KeyAuthorizationSecrets even if they are wrapped.
    for (int a = 0; a < new_kd->authorization_data_size(); ++a) {
      KeyAuthorizationData *auth_data = new_kd->mutable_authorization_data(a);
      for (int s = 0; s < auth_data->secrets_size(); ++s) {
        auth_data->mutable_secrets(s)->clear_symmetric_key();
        auth_data->mutable_secrets(s)->set_wrapped(false);
      }
    }
  }
  // No error is thrown if there is no match.
  reply.clear_error();
  SendReply(context, reply);
}

gboolean Service::GetKeyDataEx(GArray* account_id,
                               GArray* authorization_request,
                               GArray* get_key_data_request,
                               DBusGMethodInvocation *context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<GetKeyDataRequest> request(new GetKeyDataRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    identifier.reset(NULL);
  }
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len)) {
    authorization.reset(NULL);
  }
  if (!request->ParseFromArray(get_key_data_request->data,
                               get_key_data_request->len)) {
    request.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoGetKeyDataEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()),
                 base::Unretained(context)));
  return TRUE;
}

gboolean Service::MigrateKey(gchar *userid,
                             gchar *from_key,
                             gchar *to_key,
                             gboolean *OUT_result,
                             GError **error) {
  UsernamePasskey credentials(userid,
                              SecureBlob(to_key, to_key + strlen(to_key)));

  MountTaskResult result;
  base::WaitableEvent event(true, false);
  scoped_refptr<MountTaskMigratePasskey> mount_task =
      new MountTaskMigratePasskey(NULL, homedirs_, credentials, from_key);
  mount_task->set_result(&result);
  mount_task->set_complete_event(&event);
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskMigratePasskey::Run, mount_task.get()));
  event.Wait();
  *OUT_result = result.return_status();
  return TRUE;
}

gboolean Service::AsyncMigrateKey(gchar *userid,
                                  gchar *from_key,
                                  gchar *to_key,
                                  gint *OUT_async_id,
                                  GError **error) {
  UsernamePasskey credentials(userid,
                              SecureBlob(to_key, to_key + strlen(to_key)));

  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<MountTaskMigratePasskey> mount_task =
      new MountTaskMigratePasskey(bridge, homedirs_, credentials, from_key);
  *OUT_async_id = mount_task->sequence_id();
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskMigratePasskey::Run, mount_task.get()));
  return TRUE;
}

gboolean Service::AddKey(gchar *userid,
                         gchar *key,
                         gchar *new_key,
                         gint *OUT_key_id,
                         gboolean *OUT_result,
                         GError **error) {
  UsernamePasskey credentials(userid, SecureBlob(key, key + strlen(key)));

  MountTaskResult result;
  base::WaitableEvent event(true, false);
  scoped_refptr<MountTaskAddPasskey> mount_task =
      new MountTaskAddPasskey(NULL, homedirs_, credentials, new_key);
  mount_task->set_result(&result);
  mount_task->set_complete_event(&event);
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskAddPasskey::Run, mount_task.get()));
  event.Wait();
  *OUT_key_id = result.return_code();
  *OUT_result = result.return_status();
  return TRUE;
}

gboolean Service::AsyncAddKey(gchar *userid,
                              gchar *key,
                              gchar *new_key,
                              gint *OUT_async_id,
                              GError **error) {
  UsernamePasskey credentials(userid, SecureBlob(key, key + strlen(key)));

  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<MountTaskAddPasskey> mount_task =
      new MountTaskAddPasskey(bridge, homedirs_, credentials, new_key);
  *OUT_async_id = mount_task->sequence_id();
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskAddPasskey::Run, mount_task.get()));
  return TRUE;
}

void Service::DoAddKeyEx(AccountIdentifier* identifier,
                         AuthorizationRequest* authorization,
                         AddKeyRequest* add_key_request,
                         DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !add_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  // Setup a reply for use during error handling.
  BaseReply reply;

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  if (!add_key_request->has_key() || add_key_request->key().secret().empty()) {
    SendInvalidArgsReply(context, "No new key supplied");
    return;
  }

  if (add_key_request->key().data().label().empty()) {
    SendInvalidArgsReply(context, "No new key label supplied");
    return;
  }

  // Ensure any new keys do not contain a wrapped authorization key.
  for (int ad = 0;
       ad < add_key_request->key().data().authorization_data_size();
       ++ad) {
    const KeyAuthorizationData auth_data =
      add_key_request->key().data().authorization_data(ad);
    for (int s = 0; s < auth_data.secrets_size(); ++s) {
      if (auth_data.secrets(s).wrapped()) {
        // If wrapping becomes richer in the future, this may change.
        SendInvalidArgsReply(context,
                             "KeyAuthorizationSecrets may not be wrapped");
        return;
      }
    }
  }

  UsernamePasskey credentials(GetAccountId(*identifier).c_str(),
                              SecureBlob(authorization->key().secret().begin(),
                                         authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  if (!homedirs_->Exists(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  int index = -1;
  SecureBlob new_secret(add_key_request->key().secret().begin(),
                        add_key_request->key().secret().end());
  reply.set_error(homedirs_->AddKeyset(credentials,
                                       new_secret,
                                       &add_key_request->key().data(),
                                       add_key_request->clobber_if_exists(),
                                       &index));
  if (reply.error() == CRYPTOHOME_ERROR_NOT_SET) {
    // Don't set the error if there wasn't one.
    reply.clear_error();
  }
  SendReply(context, reply);
}

gboolean Service::AddKeyEx(GArray* account_id,
                           GArray* authorization_request,
                           GArray* add_key_request,
                           DBusGMethodInvocation *context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<AddKeyRequest> request(new AddKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(add_key_request->data, add_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoAddKeyEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoUpdateKeyEx(AccountIdentifier* identifier,
                            AuthorizationRequest* authorization,
                            UpdateKeyRequest* update_key_request,
                            DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !update_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  // Setup a reply for use during error handling.
  BaseReply reply;

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  // Any undefined field in changes() will be left as it is.
  if (!update_key_request->has_changes()) {
    SendInvalidArgsReply(context, "No updates requested");
    return;
  }

  for (int ad = 0;
       ad < update_key_request->changes().data().authorization_data_size();
       ++ad) {
    const KeyAuthorizationData auth_data =
      update_key_request->changes().data().authorization_data(ad);
    for (int s = 0; s < auth_data.secrets_size(); ++s) {
      if (auth_data.secrets(s).wrapped()) {
        // If wrapping becomes richer in the future, this may change.
        SendInvalidArgsReply(context,
                             "KeyAuthorizationSecrets may not be wrapped");
        return;
      }
    }
  }

  UsernamePasskey credentials(GetAccountId(*identifier).c_str(),
                              SecureBlob(authorization->key().secret().begin(),
                                         authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  if (!homedirs_->Exists(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  reply.set_error(homedirs_->UpdateKeyset(
                      credentials,
                      &update_key_request->changes(),
                      update_key_request->authorization_signature()));
  if (reply.error() == CRYPTOHOME_ERROR_NOT_SET) {
    // Don't set the error if there wasn't one.
    reply.clear_error();
  }
  SendReply(context, reply);
}

gboolean Service::UpdateKeyEx(GArray* account_id,
                              GArray* authorization_request,
                              GArray* update_key_request,
                              DBusGMethodInvocation *context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<UpdateKeyRequest> request(new UpdateKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(update_key_request->data,
                               update_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoUpdateKeyEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()),
                 base::Unretained(context)));
  return TRUE;
}

gboolean Service::Remove(gchar *userid,
                         gboolean *OUT_result,
                         GError **error) {
  UsernamePasskey credentials(userid, brillo::Blob());
  scoped_refptr<cryptohome::Mount> user_mount = GetMountForUser(userid);
  if (user_mount.get() && user_mount->IsMounted()) {
    *OUT_result = FALSE;
    return TRUE;
  }

  MountTaskResult result;
  base::WaitableEvent event(true, false);
  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<MountTaskRemove> mount_task =
      new MountTaskRemove(bridge, NULL, credentials, homedirs_);
  mount_task->set_result(&result);
  mount_task->set_complete_event(&event);
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskRemove::Run, mount_task.get()));
  event.Wait();
  *OUT_result = result.return_status();
  return TRUE;
}

gboolean Service::AsyncRemove(gchar *userid,
                              gint *OUT_async_id,
                              GError **error) {
  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<cryptohome::Mount> user_mount = GetMountForUser(userid);
  if (user_mount.get() && user_mount->IsMounted()) {
    scoped_refptr<MountTaskNop> mount_task = new MountTaskNop(bridge);
    mount_task->result()->set_return_status(false);
    *OUT_async_id = mount_task->sequence_id();
    mount_thread_.message_loop()->PostTask(FROM_HERE,
        base::Bind(&MountTaskNop::Run, mount_task.get()));
  } else {
    UsernamePasskey credentials(userid, brillo::Blob());
    scoped_refptr<MountTaskRemove> mount_task =
        new MountTaskRemove(bridge, NULL, credentials, homedirs_);
    *OUT_async_id = mount_task->sequence_id();
    mount_thread_.message_loop()->PostTask(FROM_HERE,
        base::Bind(&MountTaskRemove::Run, mount_task.get()));
  }
  return TRUE;
}

gboolean Service::RenameCryptohome(const GArray* account_id_from,
                                   const GArray* account_id_to,
                                   DBusGMethodInvocation* response) {
  std::unique_ptr<AccountIdentifier> id_from(new AccountIdentifier);
  std::unique_ptr<AccountIdentifier> id_to(new AccountIdentifier);
  if (!id_from->ParseFromArray(account_id_from->data, account_id_from->len)) {
    id_from.reset(NULL);
  }

  if (!id_to->ParseFromArray(account_id_to->data, account_id_to->len)) {
    id_to.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&Service::DoRenameCryptohome, base::Unretained(this),
                 base::Owned(id_from.release()), base::Owned(id_to.release()),
                 base::Unretained(response)));

  return TRUE;
}

void Service::DoRenameCryptohome(AccountIdentifier* id_from,
                                 AccountIdentifier* id_to,
                                 DBusGMethodInvocation* context) {
  if (!id_from || !id_to) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  scoped_refptr<cryptohome::Mount> mount =
      GetMountForUser(GetAccountId(*id_from));
  const bool is_mounted = mount.get() && mount->IsMounted();
  BaseReply reply;

  if (is_mounted) {
    LOG(ERROR) << "RenameCryptohome('" << GetAccountId(*id_from) << "','"
               << GetAccountId(*id_to)
               << "'): Unable to rename mounted cryptohome.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  } else if (!homedirs_) {
    LOG(ERROR) << "RenameCryptohome('" << GetAccountId(*id_from) << "','"
               << GetAccountId(*id_to) << "'): Homedirs not initialized.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  } else if (!homedirs_->Rename(GetAccountId(*id_from), GetAccountId(*id_to))) {
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  SendReply(context, reply);
}

gboolean Service::GetAccountDiskUsage(const GArray* account_id,
                                      DBusGMethodInvocation* response) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    identifier.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&Service::DoGetAccountDiskUsage, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Unretained(response)));
  return TRUE;
}

void Service::DoGetAccountDiskUsage(AccountIdentifier* identifier,
                                    DBusGMethodInvocation* context) {
  if (!identifier) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  BaseReply reply;
  reply.MutableExtension(GetAccountDiskUsageReply::reply)->set_size(
      homedirs_->ComputeSize(GetAccountId(*identifier)));

  SendReply(context, reply);
}

gboolean Service::GetSystemSalt(GArray **OUT_salt, GError **error) {
  if (!CreateSystemSaltIfNeeded())
    return FALSE;
  *OUT_salt = g_array_new(false, false, 1);
  g_array_append_vals(*OUT_salt, system_salt_.data(), system_salt_.size());
  return TRUE;
}

gboolean Service::GetSanitizedUsername(gchar *username,
                                       gchar **OUT_sanitized,
                                       GError **error) {
  // UsernamePasskey::GetObfuscatedUsername() returns an uppercase hex encoding,
  // while SanitizeUserName() returns a lowercase hex encoding. They should
  // return the same value, but login_manager is already relying on
  // SanitizeUserName() and that's the value that chrome should see.
  std::string sanitized =
      brillo::cryptohome::home::SanitizeUserName(username);
  if (sanitized.empty())
    return FALSE;
  *OUT_sanitized = g_strndup(sanitized.data(), sanitized.size());
  return TRUE;
}

gboolean Service::IsMounted(gboolean *OUT_is_mounted, GError **error) {
  // We consider "the cryptohome" to be mounted if any existing cryptohome is
  // mounted.
  *OUT_is_mounted = FALSE;
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    if (mount_pair.second->IsMounted()) {
      *OUT_is_mounted = TRUE;
      break;
    }
  }
  mounts_lock_.Release();
  return TRUE;
}

gboolean Service::IsMountedForUser(gchar *userid,
                                   gboolean *OUT_is_mounted,
                                   gboolean *OUT_is_ephemeral_mount,
                                   GError **error) {
  scoped_refptr<cryptohome::Mount> mount = GetMountForUser(userid);
  *OUT_is_mounted = false;
  *OUT_is_ephemeral_mount = false;
  if (!mount.get())
    return TRUE;
  if (mount->IsNonEphemeralMounted()) {
    *OUT_is_mounted = true;
    *OUT_is_ephemeral_mount = false;
  } else if (mount->IsMounted()) {
    *OUT_is_mounted = true;
    *OUT_is_ephemeral_mount = true;
  }
  return TRUE;
}

gboolean Service::Mount(const gchar *userid,
                        const gchar *key,
                        gboolean create_if_missing,
                        gboolean ensure_ephemeral,
                        gint *OUT_error_code,
                        gboolean *OUT_result,
                        GError **error) {
  CleanUpHiddenMounts();

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
  if (mounts_.size() == 0)
    // This could run on every interaction to catch any unused mounts.
    CleanUpStaleMounts(false);

  UsernamePasskey credentials(userid, SecureBlob(key, key + strlen(key)));

  scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
  bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
  if (guest_mounted && !guest_mount->UnmountCryptohome()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    *OUT_error_code = MOUNT_ERROR_MOUNT_POINT_BUSY;
    *OUT_result = FALSE;
    return TRUE;
  }

  // If a cryptohome is mounted for the user already, reuse that mount unless
  // the |ensure_ephemeral| flag prevents it: When |ensure_ephemeral| is
  // |true|, a cryptohome backed by tmpfs is required. If the currently
  // mounted cryptohome is backed by a vault, it must be unmounted and
  // remounted with a tmpfs backend.
  scoped_refptr<cryptohome::Mount> user_mount = GetOrCreateMountForUser(userid);
  if (ensure_ephemeral && user_mount->IsNonEphemeralMounted()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_mount->UnmountCryptohome()) {
      // The MountMap entry is kept since the Unmount failed.
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      *OUT_error_code = MOUNT_ERROR_MOUNT_POINT_BUSY;
      *OUT_result = FALSE;
      return TRUE;
    }
  }

  // TODO(wad) A case we haven't handled is mount-over of a non-ephemeral user.

  // This is the case where there were 2 mount requests for a given user
  // without any intervening unmount requests. This should only be able to
  // happen if Chrome acts pathologically and re-requests a Mount.  If,
  // for instance, cryptohomed crashed, the MountMap would not contain the
  // entry.
  // TODO(wad) Can we get rid of this code path?

  if (user_mount->IsMounted()) {
    // TODO(wad) This tests against the stored credentials, not the TPM.
    // If mounts are "repopulated", then a trip through the TPM would be needed.
    LOG(INFO) << "Mount exists. Rechecking credentials.";
    if (!user_mount->AreSameUser(credentials) ||
        !user_mount->AreValid(credentials)) {
      // Need to take a trip through the TPM.
      if (!homedirs_->AreCredentialsValid(credentials)) {
        LOG(ERROR) << "Failed to reauthenticate against the existing mount!";
        // TODO(wad) Should we teardown all the mounts if this happens?
        // RemoveAllMounts();
        *OUT_error_code = MOUNT_ERROR_KEY_FAILURE;
        *OUT_result = FALSE;
        return TRUE;
      }
    }

    // As far as PKCS#11 initialization goes, we treat this as a brand new
    // mount request. InitializePkcs11() will detect and re-initialize if
    // necessary except if the mount point is ephemeral as there is no PKCS#11
    // data.
    InitializePkcs11(user_mount.get());
    *OUT_error_code = MOUNT_ERROR_NONE;
    *OUT_result = TRUE;
    return TRUE;
  }

  // Any non-guest mount attempt triggers InstallAttributes finalization.
  // The return value is ignored as it is possible we're pre-ownership.
  // The next login will assure finalization if possible.
  if (install_attrs_->is_first_install())
    install_attrs_->Finalize();

  ReportTimerStart(kSyncMountTimer);
  MountTaskResult result;
  base::WaitableEvent event(true, false);
  Mount::MountArgs mount_args;
  mount_args.create_if_missing = create_if_missing;
  mount_args.ensure_ephemeral = ensure_ephemeral;
  mount_args.create_as_ecryptfs = force_ecryptfs_;
  // TODO(kinaba): Currently Mount is not used for type of accounts that
  // we need to force dircrypto. Add an option when it becomes necessary.
  mount_args.force_dircrypto = false;
  scoped_refptr<MountTaskMount> mount_task = new MountTaskMount(
                                                            NULL,
                                                            user_mount.get(),
                                                            credentials,
                                                            mount_args);
  mount_task->set_result(&result);
  mount_task->set_complete_event(&event);

  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskMount::Run, mount_task.get()));
  event.Wait();
  // We only report successful mounts.
  if (result.return_status() && !result.return_code())
    ReportTimerStop(kSyncMountTimer);

  user_mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);
  if (result.return_status()) {
    InitializePkcs11(result.mount().get());
  } else {
    RemoveMount(result.mount().get());
  }

  *OUT_error_code = result.return_code();
  *OUT_result = result.return_status();
  return TRUE;
}

void Service::DoMountEx(AccountIdentifier* identifier,
                        AuthorizationRequest* authorization,
                        MountRequest* request,
                        DBusGMethodInvocation *context) {
  if (!identifier || !authorization || !request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  CleanUpHiddenMounts();

  // Setup a reply for use during error handling.
  BaseReply reply;

  // Needed to pass along |recreated|
  MountReply* mount_reply = reply.MutableExtension(MountReply::reply);
  mount_reply->set_recreated(false);

  // See ::Mount for detailed commentary.
  bool other_mounts_active = true;
  if (mounts_.size() == 0)
    other_mounts_active = CleanUpStaleMounts(false);

  // At present, we only enforce non-empty email addresses.
  // In the future, we may wish to canonicalize if we don't move
  // to requiring a IdP-unique identifier.
  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  if (request->public_mount()) {
    std::string public_mount_passkey;
    if (!GetPublicMountPassKey(GetAccountId(*identifier),
                               &public_mount_passkey)) {
      LOG(ERROR) << "Could not get public mount passkey.";
      reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
      SendReply(context, reply);
      return;
    }

    // Set the secret as the key for cryptohome authorization/creation.
    authorization->mutable_key()->set_secret(public_mount_passkey);
    if (request->has_create()) {
      request->mutable_create()->mutable_keys(0)->set_secret(
          public_mount_passkey);
    }
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  if (request->has_create()) {
    if (request->create().copy_authorization_key()) {
      Key *auth_key = request->mutable_create()->add_keys();
      *auth_key = authorization->key();
      // Don't allow a key creation and mount if the key lacks
      // the privileges.
      if (!auth_key->data().privileges().mount()) {
        reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED);
        SendReply(context, reply);
        return;
      }
    }
    int keys_size = request->create().keys_size();
    if (keys_size == 0) {
      SendInvalidArgsReply(context, "CreateRequest supplied with no keys");
      return;
    } else if (keys_size > 1) {
      LOG(INFO) << "MountEx: unimplemented CreateRequest with multiple keys";
      reply.set_error(CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
      SendReply(context, reply);
      return;
    } else {
      const Key key = request->create().keys(0);
      // TODO(wad) Ensure the labels are all unique.
      if (key.secret().empty() || !key.has_data() ||
          key.data().label().empty()) {
        SendInvalidArgsReply(context,
                             "CreateRequest Keys are not fully specified");
        return;
      }
      // TODO(wad): Refactor out this check and other incoming Key validations
      //            in a helper.  crbug.com/353644
      for (int ad = 0; ad < key.data().authorization_data_size(); ++ad) {
        const KeyAuthorizationData auth_data =
          key.data().authorization_data(ad);
        for (int s = 0; s < auth_data.secrets_size(); ++s) {
          if (auth_data.secrets(s).wrapped()) {
          // If wrapping becomes richer in the future, this may change.
          SendInvalidArgsReply(context,
                               "KeyAuthorizationSecrets may not be wrapped");
          return;
          }
        }
      }
    }
  }

  UsernamePasskey credentials(GetAccountId(*identifier).c_str(),
                              SecureBlob(authorization->key().secret().begin(),
                                         authorization->key().secret().end()));
  // Everything else can be the default.
  credentials.set_key_data(authorization->key().data());

  if (!request->has_create() && !homedirs_->Exists(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  // Provide an authoritative filesystem-sanitized username.
  mount_reply->set_sanitized_username(
      brillo::cryptohome::home::SanitizeUserName(GetAccountId(*identifier)));

  // While it would be cleaner to implement the privilege enforcement
  // here, that can only be done if a label was supplied.  If a wildcard
  // was supplied, then we can only perform the enforcement after the
  // matching key is identified.
  //
  // See Mount::MountCryptohome for privilege checking.

  scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
  bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
  // TODO(wad,ellyjones) Change this behavior to return failure even
  // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
  if (guest_mounted && !guest_mount->UnmountCryptohome()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    SendReply(context, reply);
    return;
  }

  scoped_refptr<cryptohome::Mount> user_mount =
      GetOrCreateMountForUser(GetAccountId(*identifier));

  if (request->hidden_mount() && user_mount->IsMounted()) {
    LOG(ERROR) << "Hidden mount requested, but mount already exists.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    SendReply(context, reply);
    return;
  }

  // For public mount, don't proceed if there is any existing mount or stale
  // mount. Exceptionally, it is normal and ok to have a failed previous mount
  // attempt for the same user.
  const bool only_self_unmounted_attempt =
      mounts_.size() == 1 && !user_mount->IsMounted();
  if (request->public_mount() && other_mounts_active &&
      !only_self_unmounted_attempt) {
    LOG(ERROR) << "Public mount requested with other mounts active.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    SendReply(context, reply);
    return;
  }

  // Don't overlay an ephemeral mount over a file-backed one.
  if (request->require_ephemeral() && user_mount->IsNonEphemeralMounted()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_mount->UnmountCryptohome()) {
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
      SendReply(context, reply);
      return;
    }
  }

  if (user_mount->IsMounted()) {
    LOG(INFO) << "Mount exists. Rechecking credentials.";
    // Attempt a short-circuited credential test.
    if (user_mount->AreSameUser(credentials) &&
        user_mount->AreValid(credentials)) {
      SendReply(context, reply);
      return;
    }
    // If the Mount has invalid credentials (repopulated from system state)
    // this will ensure a user can still sign-in with the right ones.
    // TODO(wad) Should we unmount on a failed re-mount attempt?
    if (!user_mount->AreValid(credentials) &&
        !homedirs_->AreCredentialsValid(credentials)) {
      reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    }
    SendReply(context, reply);
    return;
  }

  // See Mount for a relevant comment.
  if (install_attrs_->is_first_install()) {
    install_attrs_->Finalize();
  }

  // As per the other timers, this really only tracks time spent in
  // MountCryptohome() not in the other areas prior.
  ReportTimerStart(kMountExTimer);
  MountError code = MOUNT_ERROR_NONE;
  Mount::MountArgs mount_args;
  mount_args.create_if_missing = request->has_create();
  mount_args.ensure_ephemeral = request->require_ephemeral();
  mount_args.create_as_ecryptfs = force_ecryptfs_ ||
      (request->has_create() && request->create().force_ecryptfs());
  mount_args.to_migrate_from_ecryptfs = request->to_migrate_from_ecryptfs();
  // Force_ecryptfs_ wins.
  mount_args.force_dircrypto =
      !force_ecryptfs_ && request->force_dircrypto_if_available();
  mount_args.shadow_only = request->hidden_mount();
  bool status = user_mount->MountCryptohome(credentials,
                                            mount_args,
                                            &code);
  user_mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);

  // Mark the timer as done.
  ReportTimerStop(kMountExTimer);
  if (!status) {
    reply.set_error(MountErrorToCryptohomeError(code));
  }
  if (code == MOUNT_ERROR_RECREATED) {
    mount_reply->set_recreated(true);
  }

  SendReply(context, reply);

  if (!request->hidden_mount()) {
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

gboolean Service::MountEx(const GArray *account_id,
                          const GArray *authorization_request,
                          const GArray *mount_request,
                          DBusGMethodInvocation *context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<MountRequest> request(new MountRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(mount_request->data, mount_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoMountEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()),
                 base::Unretained(context)));
  return TRUE;
}

void Service::SendLegacyAsyncReply(MountTaskMount* mount_task,
                                   MountError return_code,
                                   bool return_status) {
    MountTaskResult *result = new MountTaskResult(*mount_task->result());
    result->set_mount(mount_task->mount());
    result->set_return_code(return_code);
    result->set_return_status(return_status);
    event_source_.AddEvent(result);
    return;
}

void Service::SendDircryptoMigrationProgressSignal(
    DircryptoMigrationStatus status,
    uint64_t current_bytes,
    uint64_t total_bytes) {
  event_source_.AddEvent(new DircryptoMigrationProgress(
      status, current_bytes, total_bytes));
}

// This function implements the _old_ style Mounts.  It should be removed
// once MountEx is used everywhere.
// Pass in the MountTaskMount so the async_id stays consistent.
void Service::DoAsyncMount(const std::string& userid,
                           SecureBlob *key,
                           bool public_mount,
                           MountTaskMount* mount_task) {
  CleanUpHiddenMounts();

  // Clean up stale mounts if this is the only mount.
  if (mounts_.size() != 0 || CleanUpStaleMounts(false))  {
    // Don't proceed if there is any existing mount or stale mount.
    if (public_mount) {
      LOG(ERROR) << "Public mount requested with other mounts active.";
      PostAsyncCallResultForUser(userid, mount_task,
                                 MOUNT_ERROR_MOUNT_POINT_BUSY, false);
      return;
    }
  }

  if (public_mount) {
    std::string public_mount_passkey;
    if (!GetPublicMountPassKey(userid, &public_mount_passkey)) {
      LOG(ERROR) << "Could not get public mount passkey.";
      PostAsyncCallResultForUser(userid, mount_task,
                                 MOUNT_ERROR_KEY_FAILURE, false);
      return;
    }
    SecureBlob public_key(public_mount_passkey);
    key->swap(public_key);
    // Override the mount_task credentials with the public key.
    UsernamePasskey credentials(userid.c_str(), *key);
    mount_task->set_credentials(credentials);
  }

  scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
  mount_task->set_mount(guest_mount);
  bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
  // TODO(wad,ellyjones) Change this behavior to return failure even
  // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
  if (guest_mounted && !guest_mount->UnmountCryptohome()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    SendLegacyAsyncReply(mount_task, MOUNT_ERROR_MOUNT_POINT_BUSY, false);
    return;
  }

  scoped_refptr<cryptohome::Mount> user_mount =
      GetOrCreateMountForUser(userid.c_str());
  // Any work from here will use the user_mount.
  mount_task->set_mount(user_mount);

  // Don't overlay an ephemeral mount over a file-backed one.
  const Mount::MountArgs mount_args = mount_task->mount_args();
  if (mount_args.ensure_ephemeral && user_mount->IsNonEphemeralMounted()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_mount->UnmountCryptohome()) {
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      SendLegacyAsyncReply(mount_task, MOUNT_ERROR_MOUNT_POINT_BUSY, false);
      return;
    }
  }

  UsernamePasskey credentials(userid.c_str(), *key);
  if (user_mount->IsMounted()) {
    LOG(INFO) << "Mount exists. Rechecking credentials.";
    // Attempt a short-circuited credential test.
    if (user_mount->AreSameUser(credentials) &&
        user_mount->AreValid(credentials)) {
      SendLegacyAsyncReply(mount_task, MOUNT_ERROR_NONE, true);
      return;
    }

    // If the Mount has invalid credentials (repopulated from system state)
    // this will ensure a user can still sign-in with the right ones.
    // TODO(wad) Should we unmount on a failed re-mount attempt?
    bool return_status = homedirs_->AreCredentialsValid(credentials);
    SendLegacyAsyncReply(mount_task, MOUNT_ERROR_NONE, return_status);

    // See comment in Service::Mount() above on why this is needed here.
    InitializePkcs11(user_mount.get());
    return;
  }

  // See Mount for a relevant comment.
  if (install_attrs_->is_first_install()) {
    MountTaskInstallAttrsFinalize *finalize =
        new MountTaskInstallAttrsFinalize(NULL, install_attrs_);
    mount_thread_.message_loop()->PostTask(FROM_HERE,
        base::Bind(&MountTaskInstallAttrsFinalize::Run, finalize));
  }

  ReportTimerStart(kAsyncMountTimer);
  mount_task->result()->set_pkcs11_init(true);
  user_mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);
  mount_task->Run();
  MountTaskResult *result = new MountTaskResult(*mount_task->result());
  event_source_.AddEvent(result);
  return;
}

gboolean Service::AsyncMount(const gchar *userid,
                             const gchar *key,
                             gboolean create_if_missing,
                             gboolean ensure_ephemeral,
                             DBusGMethodInvocation *context) {
  Mount::MountArgs mount_args;
  mount_args.create_if_missing = create_if_missing;
  mount_args.ensure_ephemeral = ensure_ephemeral;
  mount_args.create_as_ecryptfs = force_ecryptfs_;
  // TODO(kinaba): Currently AsyncMount is not used for type of accounts that
  // we need to force dircrypto. Add an option when it becomes necessary.
  mount_args.force_dircrypto = false;
  std::unique_ptr<SecureBlob> key_blob(new SecureBlob(key, key + strlen(key)));
  UsernamePasskey credentials(userid, *key_blob);
  scoped_refptr<MountTaskMount> mount_task = new MountTaskMount(
                                                            NULL,
                                                            NULL,
                                                            credentials,
                                                            mount_args);

  // Send the async_id before we do any real work.
  dbus_g_method_return(context, mount_task->sequence_id());

  LOG(INFO) << "Asynced Mount() requested. Tracking request sequence id "
            << mount_task->sequence_id()
            << " for later PKCS#11 initialization.";

  // Just pass the task and the args.
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoAsyncMount, base::Unretained(this),
                 std::string(userid),
                 base::Owned(key_blob.release()),
                 false,
                 base::RetainedRef(mount_task)));

  return TRUE;
}

gboolean Service::MountGuest(gint *OUT_error_code,
                             gboolean *OUT_result,
                             GError **error) {
  if (mounts_.size() != 0)
    LOG(WARNING) << "Guest mount requested with other mounts active.";
  // Rather than make it safe to check the size, then clean up, just always
  // clean up.
  if (!RemoveAllMounts(true)) {
    LOG(ERROR) << "Could not unmount cryptohomes for Guest use";
    *OUT_error_code = MOUNT_ERROR_MOUNT_POINT_BUSY;
    *OUT_result = FALSE;
    return TRUE;
  }

  scoped_refptr<cryptohome::Mount> guest_mount =
    GetOrCreateMountForUser(guest_user_);
  ReportTimerStart(kSyncGuestMountTimer);
  MountTaskResult result;
  base::WaitableEvent event(true, false);
  scoped_refptr<MountTaskMountGuest> mount_task
      = new MountTaskMountGuest(NULL, guest_mount.get());
  mount_task->set_result(&result);
  mount_task->set_complete_event(&event);
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskMountGuest::Run, mount_task.get()));
  event.Wait();
  // We only report successful mounts.
  if (result.return_status() && !result.return_code())
    ReportTimerStop(kSyncGuestMountTimer);
  *OUT_error_code = result.return_code();
  *OUT_result = result.return_status();
  return TRUE;
}

gboolean Service::AsyncMountGuest(gint *OUT_async_id,
                                  GError **error) {
  if (mounts_.size() != 0)
    LOG(WARNING) << "Guest mount requested with other mounts active.";
  // Rather than make it safe to check the size, then clean up, just always
  // clean up.
  bool ok = RemoveAllMounts(true);
  // Create a ref-counted guest mount for async use and then throw it away.
  scoped_refptr<cryptohome::Mount> guest_mount =
    GetOrCreateMountForUser(guest_user_);
  if (!ok) {
    LOG(ERROR) << "Could not unmount cryptohomes for Guest use";
    MountTaskObserverBridge* bridge =
        new MountTaskObserverBridge(guest_mount.get(), &event_source_);
    // Drop it from the map now that the MountTask has a ref.
    if (!RemoveMountForUser(guest_user_)) {
      LOG(ERROR) << "Unexpectedly cannot drop unused Guest mount from map.";
    }
    *OUT_async_id = PostAsyncCallResult(bridge,
                                        MOUNT_ERROR_MOUNT_POINT_BUSY,
                                        false);
    return TRUE;
  }

  ReportTimerStart(kAsyncGuestMountTimer);
  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(guest_mount.get(), &event_source_);
  scoped_refptr<MountTaskMountGuest> mount_task
      = new MountTaskMountGuest(bridge, guest_mount.get());
  mount_task->result()->set_guest(true);
  *OUT_async_id = mount_task->sequence_id();
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskMountGuest::Run, mount_task.get()));
  return TRUE;
}

gboolean Service::MountPublic(const gchar* public_mount_id,
                              gboolean create_if_missing,
                              gboolean ensure_ephemeral,
                              gint* OUT_error_code,
                              gboolean* OUT_result,
                              GError** error) {
  CleanUpHiddenMounts();

  // Don't proceed if there is any existing mount or stale mount.
  if (mounts_.size() != 0 || CleanUpStaleMounts(false))  {
    LOG(ERROR) << "Public mount requested with other mounts active.";
    *OUT_error_code = MOUNT_ERROR_MOUNT_POINT_BUSY;
    *OUT_result = FALSE;
    return TRUE;
  }

  std::string public_mount_passkey;
  if (!GetPublicMountPassKey(public_mount_id, &public_mount_passkey)) {
    LOG(ERROR) << "Could not get public mount passkey.";
    *OUT_error_code = MOUNT_ERROR_KEY_FAILURE;
    *OUT_result = FALSE;
    return FALSE;
  }

  return Mount(public_mount_id,
               public_mount_passkey.c_str(),
               create_if_missing,
               ensure_ephemeral,
               OUT_error_code,
               OUT_result,
               error);
}

gboolean Service::AsyncMountPublic(const gchar* public_mount_id,
                                   gboolean create_if_missing,
                                   gboolean ensure_ephemeral,
                                   DBusGMethodInvocation *context) {
  Mount::MountArgs mount_args;
  mount_args.create_if_missing = create_if_missing;
  mount_args.ensure_ephemeral = ensure_ephemeral;
  mount_args.create_as_ecryptfs = force_ecryptfs_;
  // TODO(kinaba): Currently AsyncMount is not used for type of accounts that
  // we need to force dircrypto. Add an option when it becomes necessary.
  mount_args.force_dircrypto = false;
  std::unique_ptr<SecureBlob> key_blob(new SecureBlob());
  UsernamePasskey credentials(public_mount_id, *key_blob);
  scoped_refptr<MountTaskMount> mount_task = new MountTaskMount(
                                                            NULL,
                                                            NULL,
                                                            credentials,
                                                            mount_args);

  // Send the async_id before we do any real work.
  dbus_g_method_return(context, mount_task->sequence_id());


  // This should really call DoAsyncMount
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoAsyncMount, base::Unretained(this),
                 std::string(public_mount_id),
                 base::Owned(key_blob.release()),
                 true,
                 base::RetainedRef(mount_task)));
  return TRUE;
}

// Unmount all mounted cryptohomes.
gboolean Service::Unmount(gboolean *OUT_result, GError **error) {
  *OUT_result = RemoveAllMounts(true);
  // If there are any unexpected mounts lingering from a crash/restart,
  // clean them up now.
  CleanUpStaleMounts(true);
  return TRUE;
}

gboolean Service::UnmountForUser(const gchar *userid, gboolean *OUT_result,
                                 GError **error) {
  // NOTE: it's not clear we ever want to allow a per-user unmount.
  return Unmount(OUT_result, error);
}

gboolean Service::DoAutomaticFreeDiskSpaceControl(gboolean *OUT_result,
                                                  GError **error) {
  MountTaskResult result;
  base::WaitableEvent event(true, false);
  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<MountTaskAutomaticFreeDiskSpace> mount_task =
      new MountTaskAutomaticFreeDiskSpace(bridge, homedirs_);
  mount_task->set_result(&result);
  mount_task->set_complete_event(&event);
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskAutomaticFreeDiskSpace::Run, mount_task.get()));
  event.Wait();
  *OUT_result = result.return_status();
  return TRUE;
}

gboolean Service::AsyncDoAutomaticFreeDiskSpaceControl(gint *OUT_async_id,
                                                       GError **error) {
  MountTaskObserverBridge* bridge =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<MountTaskAutomaticFreeDiskSpace> mount_task =
      new MountTaskAutomaticFreeDiskSpace(bridge, homedirs_);
  *OUT_async_id = mount_task->sequence_id();
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskAutomaticFreeDiskSpace::Run, mount_task.get()));
  return TRUE;
}

gboolean Service::UpdateCurrentUserActivityTimestamp(gint time_shift_sec,
                                                     GError **error) {
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    mount_pair.second->UpdateCurrentUserActivityTimestamp(time_shift_sec);
  }
  mounts_lock_.Release();
  return TRUE;
}

gboolean Service::TpmIsReady(gboolean* OUT_ready, GError** error) {
  *OUT_ready = tpm_init_->IsTpmReady();
  return TRUE;
}

gboolean Service::TpmIsEnabled(gboolean* OUT_enabled, GError** error) {
  *OUT_enabled = tpm_init_->IsTpmEnabled();
  return TRUE;
}

gboolean Service::TpmGetPassword(gchar** OUT_password, GError** error) {
  SecureBlob password;
  if (!tpm_init_->GetTpmPassword(&password)) {
    *OUT_password = NULL;
    return TRUE;
  }
  // Convert to UTF-8 for sending over DBus. In case the original string
  // contained only ASCII characters, the result will be identical to the
  // original password.
  SecureBlob utf8_password(
      base::SysWideToUTF8(std::wstring(password.begin(), password.end())));
  // Make sure we copy and NULL-terminate the entire UTF-8 string, even if
  // there are 00 bytes in the middle of it. strndup/g_strndup would have
  // stopped at the first 00. Can still be stripped later by DBus code, though.
  size_t ret_size = utf8_password.size();
  gchar* ret_str = g_new(gchar, ret_size + 1);
  if (ret_str) {
    memcpy(ret_str, utf8_password.char_data(), ret_size);
    ret_str[ret_size] = 0;
  }
  *OUT_password = ret_str;
  return TRUE;
}

gboolean Service::TpmIsOwned(gboolean* OUT_owned, GError** error) {
  *OUT_owned = tpm_init_->IsTpmOwned();
  return TRUE;
}

gboolean Service::TpmIsBeingOwned(gboolean* OUT_owning, GError** error) {
  *OUT_owning = tpm_init_->IsTpmBeingOwned();
  return TRUE;
}

gboolean Service::TpmCanAttemptOwnership(GError** error) {
  if (!tpm_init_->OwnershipRequested()) {
    ReportTimerStart(kTpmTakeOwnershipTimer);
    tpm_init_->AsyncTakeOwnership();
  }
  return TRUE;
}

gboolean Service::TpmClearStoredPassword(GError** error) {
  tpm_init_->ClearStoredTpmPassword();
  return TRUE;
}

gboolean Service::TpmGetVersion(gchar** OUT_result,
                                GError** error) {
  cryptohome::Tpm::TpmVersionInfo version_info;
  if (!tpm_init_->GetVersion(&version_info)) {
    LOG(ERROR) << "Could not get TPM version information.";
    *OUT_result = nullptr;
    return FALSE;
  }

  std::string vendor_specific =
      base::HexEncode(version_info.vendor_specific.data(),
                      version_info.vendor_specific.size());
  std::string info = base::StringPrintf("TPM family: %08" PRIx32 "\n"
                     "spec level: %016" PRIx64 "\n"
                     "manufacturer: %08" PRIx32 "\n"
                     "tpm_model: %08" PRIx32 "\n"
                     "firmware version: %016" PRIx64 "\n"
                     "vendor specific: %s\n",
                     version_info.family,
                     version_info.spec_level,
                     version_info.manufacturer,
                     version_info.tpm_model,
                     version_info.firmware_version,
                     vendor_specific.c_str());

  *OUT_result = g_strdup(info.c_str());
  return TRUE;
}

// Returns true if all Pkcs11 tokens are ready.
gboolean Service::Pkcs11IsTpmTokenReady(gboolean* OUT_ready, GError** error) {
  *OUT_ready = TRUE;
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    cryptohome::Mount* mount = mount_pair.second.get();
    bool ok = (mount->pkcs11_state() == cryptohome::Mount::kIsInitialized);
    *OUT_ready = *OUT_ready && ok;
  }
  mounts_lock_.Release();
  return TRUE;
}

gboolean Service::Pkcs11IsTpmTokenReadyForUser(gchar* username,
                                               gboolean* OUT_ready,
                                               GError** error) {
  // TODO(ellyjones): make this really check per user. crosbug.com/22127
  return Pkcs11IsTpmTokenReady(OUT_ready, error);
}

gboolean Service::Pkcs11GetTpmTokenInfo(gchar** OUT_label,
                                        gchar** OUT_user_pin,
                                        gint* OUT_slot,
                                        GError** error) {
  pkcs11_init_->GetTpmTokenInfo(OUT_label, OUT_user_pin);
  *OUT_slot = -1;
  CK_SLOT_ID slot;
  if (pkcs11_init_->GetTpmTokenSlotForPath(FilePath(kChapsSystemToken), &slot))
    *OUT_slot = slot;
  return TRUE;
}

gboolean Service::Pkcs11GetTpmTokenInfoForUser(gchar* username,
                                               gchar** OUT_label,
                                               gchar** OUT_user_pin,
                                               gint* OUT_slot,
                                               GError** error) {
  pkcs11_init_->GetTpmTokenInfoForUser(username, OUT_label, OUT_user_pin);
  *OUT_slot = -1;
  CK_SLOT_ID slot;
  FilePath token_path = homedirs_->GetChapsTokenDir(username);
  if (pkcs11_init_->GetTpmTokenSlotForPath(token_path, &slot))
    *OUT_slot = slot;
  return TRUE;
}

gboolean Service::Pkcs11Terminate(gchar* username, GError **error) {
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    mount_pair.second->RemovePkcs11Token();
  }
  mounts_lock_.Release();
  return TRUE;
}

gboolean Service::InstallAttributesGet(gchar* name,
                                       GArray** OUT_value,
                                       gboolean* OUT_successful,
                                       GError** error) {
  brillo::Blob value;
  *OUT_successful = install_attrs_->Get(name, &value);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_value = g_array_new(false, false, sizeof(value.front()));
  if (!(*OUT_value)) {
    return FALSE;
  }
  if (*OUT_successful) {
    g_array_append_vals(*OUT_value, value.data(), value.size());
  }
  return TRUE;
}

gboolean Service::InstallAttributesSet(gchar* name,
                                       GArray* value,
                                       gboolean* OUT_successful,
                                       GError** error) {
  // Convert from GArray to vector
  brillo::Blob value_blob;
  value_blob.assign(value->data, value->data + value->len);
  *OUT_successful = install_attrs_->Set(name, value_blob);
  return TRUE;
}

gboolean Service::InstallAttributesFinalize(gboolean* OUT_finalized,
                                            GError** error) {
  *OUT_finalized = install_attrs_->Finalize();

  // Check if the machine is enterprise owned and report this to mount_.
  DetectEnterpriseOwnership();

  return TRUE;
}

gboolean Service::InstallAttributesCount(gint* OUT_count, GError** error) {
  // TODO(wad) for all of these functions return error on uninit.
  // Follow the CHROMEOS_LOGIN_ERROR quark example in brillo/dbus/
  *OUT_count = install_attrs_->Count();
  return TRUE;
}

gboolean Service::InstallAttributesIsReady(gboolean* OUT_ready,
                                           GError** error) {
  *OUT_ready = (install_attrs_->IsReady() == true);
  return TRUE;
}

gboolean Service::InstallAttributesIsSecure(gboolean* OUT_is_secure,
                                            GError** error) {
  *OUT_is_secure = (install_attrs_->is_secure() == true);
  return TRUE;
}

gboolean Service::InstallAttributesIsInvalid(gboolean* OUT_is_invalid,
                                             GError** error) {
  // Is true after a failed init or prior to Init().
  *OUT_is_invalid = (install_attrs_->is_invalid() == true);
  return TRUE;
}

gboolean Service::InstallAttributesIsFirstInstall(
    gboolean* OUT_is_first_install,
    GError** error) {
  *OUT_is_first_install = (install_attrs_->is_first_install() == true);
  return TRUE;
}

gboolean Service::StoreEnrollmentState(GArray* enrollment_state,
                                       gboolean* OUT_success,
                                       GError** error) {
  *OUT_success = false;
  if (!enterprise_owned_) {
    LOG(ERROR) << "Not preserving enrollment state as we are not enrolled.";
    return TRUE;
  }
  SecureBlob data_blob(enrollment_state->data,
                       enrollment_state->data + enrollment_state->len);
  std::string encrypted_data;
  if (!crypto_->EncryptWithTpm(data_blob, &encrypted_data)) {
    return TRUE;
  }
  if (!platform_->WriteStringToFileAtomicDurable(
          kPreservedEnrollmentStatePath,
          encrypted_data,
          kPreservedEnrollmentStatePermissions)) {
    LOG(ERROR) << "Failed to write out enrollment state to "
               << kPreservedEnrollmentStatePath.value();
    return TRUE;
  }
  *OUT_success = true;
  return TRUE;
}

gboolean Service::LoadEnrollmentState(GArray** OUT_enrollment_state,
                                      gboolean* OUT_success,
                                      GError** error) {
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_enrollment_state = g_array_new(false, false, 1);
  *OUT_success = false;
  brillo::Blob enrollment_blob;
  if (!platform_->ReadFile(kPreservedEnrollmentStatePath,
                           &enrollment_blob)) {
    LOG(ERROR) << "Failed to read out enrollment state from "
               << kPreservedEnrollmentStatePath.value();
    return TRUE;
  }
  std::string enrollment_string(
      reinterpret_cast<const char*>(&enrollment_blob.front()),
      enrollment_blob.size());
  SecureBlob secure_data;
  if (!crypto_->DecryptWithTpm(enrollment_string,
                               &secure_data)) {
    return TRUE;
  }
  g_array_append_vals(*OUT_enrollment_state,
                      secure_data.char_data(), secure_data.size());
  *OUT_success = true;
  return TRUE;
}

void Service::DoSignBootLockbox(const brillo::SecureBlob& request,
                                DBusGMethodInvocation* context) {
  SignBootLockboxRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size()) ||
      !request_pb.has_data()) {
    SendInvalidArgsReply(context, "Bad SignBootLockboxRequest");
    return;
  }
  BaseReply reply;
  SecureBlob signature;
  if (!boot_lockbox_->Sign(SecureBlob(request_pb.data()), &signature)) {
    reply.set_error(CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN);
  } else {
    reply.MutableExtension(SignBootLockboxReply::reply)->set_signature(
        signature.to_string());
  }
  SendReply(context, reply);
}

gboolean Service::SignBootLockbox(const GArray* request,
                                  DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoSignBootLockbox, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoVerifyBootLockbox(const brillo::SecureBlob& request,
                                  DBusGMethodInvocation* context) {
  VerifyBootLockboxRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size()) ||
      !request_pb.has_data() ||
      !request_pb.has_signature()) {
    SendInvalidArgsReply(context, "Bad VerifyBootLockboxRequest");
    return;
  }
  BaseReply reply;
  if (!boot_lockbox_->Verify(SecureBlob(request_pb.data()),
                             SecureBlob(request_pb.signature()))) {
    reply.set_error(CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID);
  }
  SendReply(context, reply);
}

gboolean Service::VerifyBootLockbox(const GArray* request,
                                    DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoVerifyBootLockbox, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoFinalizeBootLockbox(const brillo::SecureBlob& request,
                                    DBusGMethodInvocation* context) {
  FinalizeBootLockboxRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad FinalizeBootLockboxRequest");
    return;
  }
  BaseReply reply;
  if (!boot_lockbox_->FinalizeBoot()) {
    reply.set_error(CRYPTOHOME_ERROR_TPM_COMM_ERROR);
  }
  SendReply(context, reply);
}

gboolean Service::FinalizeBootLockbox(const GArray* request,
                                      DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoFinalizeBootLockbox, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetBootAttribute(const brillo::SecureBlob& request,
                                 DBusGMethodInvocation* context) {
  GetBootAttributeRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetBootAttributeRequest");
    return;
  }
  BaseReply reply;
  std::string value;
  if (!boot_attributes_->Get(request_pb.name(), &value)) {
    reply.set_error(CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND);
  } else {
    reply.MutableExtension(GetBootAttributeReply::reply)->set_value(value);
  }
  SendReply(context, reply);
}

gboolean Service::GetBootAttribute(const GArray* request,
                                   DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoGetBootAttribute, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoSetBootAttribute(const brillo::SecureBlob& request,
                                 DBusGMethodInvocation* context) {
  SetBootAttributeRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad SetBootAttributeRequest");
    return;
  }
  BaseReply reply;
  boot_attributes_->Set(request_pb.name(), request_pb.value());
  SendReply(context, reply);
}

gboolean Service::SetBootAttribute(const GArray* request,
                                   DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoSetBootAttribute, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoFlushAndSignBootAttributes(const brillo::SecureBlob& request,
                                           DBusGMethodInvocation* context) {
  FlushAndSignBootAttributesRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad FlushAndSignBootAttributesRequest");
    return;
  }
  BaseReply reply;
  if (!boot_attributes_->FlushAndSign()) {
    reply.set_error(CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN);
  }
  SendReply(context, reply);
}

gboolean Service::FlushAndSignBootAttributes(const GArray* request,
                                             DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoFlushAndSignBootAttributes, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetLoginStatus(const brillo::SecureBlob& request,
                               DBusGMethodInvocation* context) {
  GetLoginStatusRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetLoginStatusRequest");
    return;
  }
  BaseReply reply;
  std::string owner;
  reply.MutableExtension(
      GetLoginStatusReply::reply)->set_owner_user_exists(
          homedirs_->GetPlainOwner(&owner));
  reply.MutableExtension(
      GetLoginStatusReply::reply)->set_boot_lockbox_finalized(
          boot_lockbox_->IsFinalized());
  SendReply(context, reply);
}

gboolean Service::GetLoginStatus(const GArray* request,
                                 DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoGetLoginStatus, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetTpmStatus(const brillo::SecureBlob& request,
                             DBusGMethodInvocation* context) {
  GetTpmStatusRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetTpmStatusRequest");
    return;
  }
  BaseReply reply;
  GetTpmStatusReply* extension = reply.MutableExtension(
      GetTpmStatusReply::reply);
  extension->set_enabled(tpm_init_->IsTpmEnabled());
  extension->set_owned(tpm_init_->IsTpmOwned());
  SecureBlob owner_password;
  if (tpm_init_->GetTpmPassword(&owner_password)) {
    extension->set_initialized(false);
    extension->set_owner_password(owner_password.to_string());
  } else {
    // Initialized is true only when the TPM is owned and the owner password has
    // already been destroyed.
    extension->set_initialized(extension->owned());
  }
  int counter;
  int threshold;
  bool lockout;
  int seconds_remaining;
  if (tpm_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                    &seconds_remaining)) {
    extension->set_dictionary_attack_counter(counter);
    extension->set_dictionary_attack_threshold(threshold);
    extension->set_dictionary_attack_lockout_in_effect(lockout);
    extension->set_dictionary_attack_lockout_seconds_remaining(
        seconds_remaining);
  }
  extension->set_install_lockbox_finalized(
      extension->owned() &&
      !install_attrs_->is_first_install() &&
      !install_attrs_->is_invalid() &&
      install_attrs_->is_initialized());
  extension->set_boot_lockbox_finalized(boot_lockbox_->IsFinalized());
  AttestationGetTpmStatus(extension);
  SendReply(context, reply);
}

gboolean Service::GetTpmStatus(const GArray* request,
                               DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoGetTpmStatus, base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetFirmwareManagementParameters(
    const brillo::SecureBlob& request,
    DBusGMethodInvocation* context) {
  GetFirmwareManagementParametersRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetFirmwareManagementParametersRequest");
    return;
  }
  BaseReply reply;
  GetFirmwareManagementParametersReply* extension =
    reply.MutableExtension(GetFirmwareManagementParametersReply::reply);

  if (!firmware_management_parameters_->Load()) {
    reply.set_error(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID);
    SendReply(context, reply);
    return;
  }

  uint32_t flags;
  if (firmware_management_parameters_->GetFlags(&flags)) {
    extension->set_flags(flags);
  }

  brillo::SecureBlob hash;
  if (firmware_management_parameters_->GetDeveloperKeyHash(&hash)) {
    extension->set_developer_key_hash(hash.to_string());
  }

  SendReply(context, reply);
}

gboolean Service::GetFirmwareManagementParameters(const GArray* request,
    DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoGetFirmwareManagementParameters,
                 base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoSetFirmwareManagementParameters(
    const brillo::SecureBlob& request,
    DBusGMethodInvocation* context) {
  SetFirmwareManagementParametersRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad SetFirmwareManagementParametersRequest");
    return;
  }

  BaseReply reply;
  if (!firmware_management_parameters_->Create()) {
    reply.set_error(
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE);
    SendReply(context, reply);
    return;
  }

  uint32_t flags = 0;
  if (request_pb.has_flags()) {
    flags = request_pb.flags();
  }

  std::unique_ptr<brillo::Blob> hash;
  if (request_pb.has_developer_key_hash()) {
    hash.reset(new SecureBlob(request_pb.developer_key_hash()));
  }

  if (!firmware_management_parameters_->Store(flags, hash.get())) {
    reply.set_error(
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE);
    SendReply(context, reply);
    return;
  }

  SendReply(context, reply);
}

gboolean Service::SetFirmwareManagementParameters(const GArray* request,
                          DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoSetFirmwareManagementParameters,
                 base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoRemoveFirmwareManagementParameters(
    const brillo::SecureBlob& request,
    DBusGMethodInvocation* context) {
  RemoveFirmwareManagementParametersRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context,
                         "Bad RemoveFirmwareManagementParametersRequest");
    return;
  }
  BaseReply reply;
  if (!firmware_management_parameters_->Destroy()) {
    reply.set_error(
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE);
    SendReply(context, reply);
    return;
  }

  SendReply(context, reply);
}

gboolean Service::RemoveFirmwareManagementParameters(const GArray* request,
                             DBusGMethodInvocation* context) {
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&Service::DoRemoveFirmwareManagementParameters,
                 base::Unretained(this),
                 SecureBlob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

gboolean Service::GetStatusString(gchar** OUT_status, GError** error) {
  base::DictionaryValue dv;
  auto mounts = base::MakeUnique<base::ListValue>();
  mounts_lock_.Acquire();
  for (const auto& mount_pair : mounts_) {
    mounts->Append(mount_pair.second->GetStatus());
  }
  mounts_lock_.Release();
  auto attrs = install_attrs_->GetStatus();

  Tpm::TpmStatusInfo tpm_status_info;
  tpm_->GetStatus(tpm_init_->GetCryptohomeKey(),
                  &tpm_status_info);
  auto tpm = base::MakeUnique<base::DictionaryValue>();
  tpm->SetBoolean("can_connect", tpm_status_info.can_connect);
  tpm->SetBoolean("can_load_srk", tpm_status_info.can_load_srk);
  tpm->SetBoolean("can_load_srk_pubkey",
                  tpm_status_info.can_load_srk_public_key);
  tpm->SetBoolean("has_cryptohome_key", tpm_status_info.has_cryptohome_key);
  tpm->SetBoolean("can_encrypt", tpm_status_info.can_encrypt);
  tpm->SetBoolean("can_decrypt", tpm_status_info.can_decrypt);
  tpm->SetBoolean("has_context", tpm_status_info.this_instance_has_context);
  tpm->SetBoolean("has_key_handle",
                  tpm_status_info.this_instance_has_key_handle);
  tpm->SetInteger("last_error", tpm_status_info.last_tpm_error);

  tpm->SetBoolean("enabled", tpm_->IsEnabled());
  tpm->SetBoolean("owned", tpm_->IsOwned());
  tpm->SetBoolean("being_owned", tpm_->IsBeingOwned());

  dv.Set("mounts", std::move(mounts));
  dv.Set("installattrs", std::move(attrs));
  dv.Set("tpm", std::move(tpm));
  std::string json;
  base::JSONWriter::WriteWithOptions(dv, base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                     &json);
  *OUT_status = g_strdup(json.c_str());
  return TRUE;
}

// Called on Mount thread.
void Service::AutoCleanupCallback() {
  static int ticks;

  // Update current user's activity timestamp every day.
  if (++ticks > update_user_activity_period_) {
    mounts_lock_.Acquire();
    for (const auto& mount_pair : mounts_) {
      mount_pair.second->UpdateCurrentUserActivityTimestamp(0);
    }
    mounts_lock_.Release();
    ticks = 0;
  }

  homedirs_->FreeDiskSpace();

  // Reset the dictionary attack counter if possible and necessary.
  ResetDictionaryAttackMitigation();

  // Schedule our next call. If the thread is terminating, we would
  // not be called. We use base::Unretained here because the Service object is
  // never destroyed.
  mount_thread_.message_loop()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Service::AutoCleanupCallback, base::Unretained(this)),
      base::TimeDelta::FromMilliseconds(auto_cleanup_period_));
}

// Called on Mount thread.
void Service::LowDiskCallback() {
  int64_t free_disk_space = homedirs_->AmountOfFreeDiskSpace();
  if (free_disk_space < 0)
    LOG(ERROR) << "Error getting free disk space, got: " << free_disk_space;
  else if (free_disk_space < kNotifyDiskSpaceThreshold)
    g_signal_emit(cryptohome_, low_disk_space_signal_,
                  0 /* signal detail (not used) */,
                  static_cast<uint64_t>(free_disk_space));

  mount_thread_.message_loop()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Service::LowDiskCallback, base::Unretained(this)),
      base::TimeDelta::FromMilliseconds(low_disk_notification_period_ms_));
}

void Service::ResetDictionaryAttackMitigation() {
  if (!use_tpm_) {
    return;
  }
  int counter = 0;
  int threshold;
  int seconds_remaining;
  bool lockout;
  if (!tpm_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                     &seconds_remaining)) {
    ReportDictionaryAttackResetStatus(kCounterQueryFailed);
    return;
  }
  ReportDictionaryAttackCounter(counter);
  if (counter == 0) {
    ReportDictionaryAttackResetStatus(kResetNotNecessary);
    return;
  }
  brillo::SecureBlob delegate_blob, delegate_secret;
  bool has_reset_lock_permissions = false;
  if (!AttestationGetDelegateCredentials(&delegate_blob,
                                         &delegate_secret,
                                         &has_reset_lock_permissions)) {
    ReportDictionaryAttackResetStatus(kDelegateNotAvailable);
    return;
  }
  if (!has_reset_lock_permissions) {
    ReportDictionaryAttackResetStatus(kDelegateNotAllowed);
    return;
  }
  if (!tpm_->ResetDictionaryAttackMitigation(delegate_blob, delegate_secret)) {
    ReportDictionaryAttackResetStatus(kResetAttemptFailed);
    return;
  }
  ReportDictionaryAttackResetStatus(kResetAttemptSucceeded);
}

void Service::DetectEnterpriseOwnership() {
  static const char true_str[] = "true";
  const brillo::Blob true_value(true_str, true_str + arraysize(true_str));
  brillo::Blob value;
  if (install_attrs_->Get("enterprise.owned", &value) && value == true_value) {
    enterprise_owned_ = true;
    // Update any active mounts with the state.
    mounts_lock_.Acquire();
    for (const auto& mount_pair : mounts_) {
      mount_pair.second->set_enterprise_owned(true);
    }
    mounts_lock_.Release();
    homedirs_->set_enterprise_owned(true);
  }
}

scoped_refptr<cryptohome::Mount> Service::GetOrCreateMountForUser(
    const std::string& username) {
  scoped_refptr<cryptohome::Mount> m;
  mounts_lock_.Acquire();
  if (mounts_.count(username) == 0U) {
    m = mount_factory_->New();
    m->Init(platform_, crypto_, user_timestamp_cache_.get());
    m->set_enterprise_owned(enterprise_owned_);
    m->set_legacy_mount(legacy_mount_);
    mounts_[username] = m;
  } else {
    m = mounts_[username];
  }
  mounts_lock_.Release();
  return m;
}

bool Service::RemoveMountForUser(const std::string& username) {
  bool ok = true;
  mounts_lock_.Acquire();
  if (mounts_.count(username) != 0) {
    ok = (1U == mounts_.erase(username));
  }
  mounts_lock_.Release();
  return ok;
}

void Service::RemoveMount(cryptohome::Mount* mount) {
  mounts_lock_.Acquire();
  for (auto it = mounts_.begin(); it != mounts_.end(); ++it) {
     if (it->second.get() == mount) {
       mounts_.erase(it);
       break;
     }
  }
  mounts_lock_.Release();
}


bool Service::RemoveAllMounts(bool unmount) {
  bool ok = true;
  mounts_lock_.Acquire();
  for (auto it = mounts_.begin(); it != mounts_.end(); ) {
    scoped_refptr<cryptohome::Mount> mount = it->second;
    if (unmount && mount->IsMounted()) {
      if (mount->pkcs11_state() == cryptohome::Mount::kIsBeingInitialized) {
        // Walk the open tasks.
        for (const auto& pkcs11_task_pair : pkcs11_tasks_) {
          scoped_refptr<MountTaskPkcs11Init> task = pkcs11_task_pair.second;
          if (task->mount().get() == mount.get()) {
            task->Cancel();
            LOG(INFO) << "Cancelling PKCS#11 Init on unmount.";
            break;
          }
        }
        // Reset the state.
        mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);
        // And also reset the global failure reported state.
        // TODO(wad,ellyjones,dkrahn) De-globalize this when Chaps support
        // multiple mounts.
        reported_pkcs11_init_fail_ = false;
      }
      ok = ok && mount->UnmountCryptohome();
    }
    mounts_.erase(it++);
  }
  mounts_lock_.Release();
  return ok;
}

bool Service::GetMountPointForUser(const std::string& username,
                                   FilePath* path) {
  scoped_refptr<cryptohome::Mount> mount;

  mount = GetMountForUser(username);
  if (!mount.get() || !mount->IsMounted())
    return false;
  *path = mount->mount_point();
  return true;
}

scoped_refptr<cryptohome::Mount> Service::GetMountForUser(
    const std::string& username) {
  scoped_refptr<cryptohome::Mount> mount = NULL;
  mounts_lock_.Acquire();
  if (mounts_.count(username) == 1) {
    mount = mounts_[username];
  }
  mounts_lock_.Release();
  return mount;
}

bool Service::CreateSystemSaltIfNeeded() {
  if (!system_salt_.empty())
    return true;
  FilePath saltfile = homedirs_->shadow_root().Append(kSaltFile);
  return crypto_->GetOrCreateSalt(saltfile, CRYPTOHOME_DEFAULT_SALT_LENGTH,
                                  false, &system_salt_);
}

bool Service::CreatePublicMountSaltIfNeeded() {
  if (!public_mount_salt_.empty())
    return true;
  FilePath saltfile(kPublicMountSaltFilePath);
  return crypto_->GetOrCreateSalt(saltfile, CRYPTOHOME_DEFAULT_SALT_LENGTH,
                                  false, &public_mount_salt_);
}

bool Service::GetPublicMountPassKey(const std::string& public_mount_id,
                                    std::string* public_mount_passkey) {
  if (!CreatePublicMountSaltIfNeeded())
    return false;
  SecureBlob passkey;
  Crypto::PasswordToPasskey(public_mount_id.c_str(),
                            public_mount_salt_,
                            &passkey);
  *public_mount_passkey = passkey.to_string();
  return true;
}

int Service::PostAsyncCallResult(MountTaskObserver* bridge,
                                 MountError return_code,
                                 bool return_status) {
  scoped_refptr<MountTaskNop> mount_task = new MountTaskNop(bridge);
  mount_task->result()->set_return_code(return_code);
  mount_task->result()->set_return_status(return_status);
  mount_thread_.message_loop()->PostTask(FROM_HERE,
      base::Bind(&MountTaskNop::Run, mount_task.get()));

  return mount_task->sequence_id();
}

void Service::PostAsyncCallResultForUser(const std::string& user_id,
                                         MountTaskMount* mount_task,
                                         MountError return_code,
                                         bool return_status) {
  // Create a ref-counted mount for async use and then throw it away.
  scoped_refptr<cryptohome::Mount> mount = GetOrCreateMountForUser(user_id);
  mount_task->set_mount(GetOrCreateMountForUser(user_id));
  // Drop it from the map now that the MountTask has a ref.
  if (!RemoveMountForUser(user_id))
    LOG(ERROR) << "Unexpectedly cannot drop unused mount from map.";

  SendLegacyAsyncReply(mount_task, return_code, return_status);
}

void Service::DispatchEvents() {
  event_source_.HandleDispatch();
}

gboolean Service::MigrateToDircrypto(const GArray* account_id,
                                     const GArray* migrate_request,
                                     GError** error) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);

  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    LOG(ERROR) << "Failed to parse identifier.";
    return FALSE;
  }

  MigrationType migration_type = MigrationType::FULL;
  // TODO(bug758837,pmarko): Currently, cryptohomed offers MigrateToDircrypto
  // and MigrateToDircryptoEx. Only the latter has |migrate_request|. With
  // bug758837, MigrateToDircrypto will have |migrate_request| and it will not
  // be optional here anymore.
  if (migrate_request) {
    std::unique_ptr<MigrateToDircryptoRequest> request =
        base::MakeUnique<MigrateToDircryptoRequest>();
    if (!request->ParseFromArray(migrate_request->data,
                                 migrate_request->len)) {
      LOG(ERROR) << "Failed to parse migrate_request.";
      return FALSE;
    }
    if (request->minimal_migration())
      migration_type = MigrationType::MINIMAL;
  }
  // This Dbus method just kicks the migration task on the mount thread,
  // and replies immediately.
  mount_thread_.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&Service::DoMigrateToDircrypto,
                 base::Unretained(this),
                 base::Owned(identifier.release()),
                 migration_type));
  return TRUE;
}

void Service::DoMigrateToDircrypto(
    AccountIdentifier* identifier,
    MigrationType migration_type) {
  scoped_refptr<cryptohome::Mount> mount =
      GetMountForUser(GetAccountId(*identifier));
  if (!mount.get()) {
    LOG(ERROR) << "Failed to get mount.";
    SendDircryptoMigrationProgressSignal(DIRCRYPTO_MIGRATION_FAILED, 0, 0);
    return;
  }
  LOG(INFO) << "Migrating to dircrypto.";
  if (!mount->MigrateToDircrypto(
          base::Bind(&Service::SendDircryptoMigrationProgressSignal,
                     base::Unretained(this)),
          migration_type)) {
    LOG(ERROR) << "Failed to migrate.";
    SendDircryptoMigrationProgressSignal(DIRCRYPTO_MIGRATION_FAILED, 0, 0);
    return;
  }
  LOG(INFO) << "Migration done.";
  SendDircryptoMigrationProgressSignal(DIRCRYPTO_MIGRATION_SUCCESS, 0, 0);
}

gboolean Service::NeedsDircryptoMigration(const GArray* account_id,
                                          gboolean* OUT_needs_migration,
                                          GError** error) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    LOG(ERROR) << "No user supplied.";
    return FALSE;
  }

  UsernamePasskey credentials(GetAccountId(*identifier).c_str(), SecureBlob());
  if (!homedirs_->Exists(credentials)) {
    LOG(ERROR) << "Unknown user.";
    return FALSE;
  }

  *OUT_needs_migration = !force_ecryptfs_ &&
      homedirs_->NeedsDircryptoMigration(credentials);
  return TRUE;
}

}  // namespace cryptohome
