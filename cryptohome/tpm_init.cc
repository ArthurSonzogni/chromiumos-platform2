// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class TpmInit

#include "cryptohome/tpm_init.h"

#include <stdint.h>

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "cryptohome/attestation.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/interface.h"

using base::FilePath;
using base::PlatformThread;
using base::PlatformThreadHandle;
using brillo::SecureBlob;

namespace cryptohome {

const int kMaxTimeoutRetries = 5;

const FilePath kMiscTpmCheckEnabledFile("/sys/class/misc/tpm0/device/enabled");
const FilePath kMiscTpmCheckOwnedFile("/sys/class/misc/tpm0/device/owned");
const FilePath kTpmTpmCheckEnabledFile("/sys/class/tpm/tpm0/device/enabled");
const FilePath kTpmTpmCheckOwnedFile("/sys/class/tpm/tpm0/device/owned");
extern const FilePath kTpmOwnedFile("/mnt/stateful_partition/.tpm_owned");
const FilePath kTpmStatusFile("/mnt/stateful_partition/.tpm_status");
const FilePath kOpenCryptokiPath("/var/lib/opencryptoki");
const FilePath kDefaultCryptohomeKeyFile("/home/.shadow/cryptohome.key");

const int kOwnerPasswordLength = 12;
const unsigned int kDefaultTpmRsaKeyBits = 2048;

// TpmInitTask is a private class used to handle asynchronous initialization of
// the TPM.
class TpmInitTask : public PlatformThread::Delegate {
 public:
  TpmInitTask() : tpm_(NULL), init_(NULL) {}
  virtual ~TpmInitTask() {}

  void Init(TpmInit* init) {
    init_ = init;
    if (tpm_) {
      init->SetupTpm(false);
    }
  }

  virtual void ThreadMain() {
    if (init_) {
      init_->ThreadMain();
    }
  }

  void set_tpm(Tpm* tpm) { tpm_ = tpm; }
  Tpm* get_tpm() { return tpm_; }

 private:
  Tpm* tpm_;
  TpmInit* init_;
};

TpmInit::TpmInit(Tpm* tpm, Platform* platform)
    : tpm_init_task_(new TpmInitTask()), platform_(platform) {
  set_tpm(tpm);
}

TpmInit::~TpmInit() {
  if (!init_thread_.is_null()) {
    // Must wait for tpm init thread to complete, because when the main thread
    // exits some libtspi data structures are freed.
    PlatformThread::Join(init_thread_);
    init_thread_ = PlatformThreadHandle();
  }
}

void TpmInit::set_tpm(Tpm* value) {
  if (tpm_init_task_.get())
    tpm_init_task_->set_tpm(value);
}

Tpm* TpmInit::get_tpm() {
  if (tpm_init_task_.get())
    return tpm_init_task_->get_tpm();
  return NULL;
}

void TpmInit::Init(OwnershipCallback ownership_callback) {
  ownership_callback_ = ownership_callback;
  tpm_init_task_->Init(this);
}

bool TpmInit::AsyncTakeOwnership() {
  take_ownership_called_ = true;
  if (!PlatformThread::Create(0, tpm_init_task_.get(), &init_thread_)) {
    LOG(ERROR) << "Unable to create background thread to take TPM ownership.";
    return false;
  }
  return true;
}

bool TpmInit::IsTpmReady() {
  // The TPM is "ready" if it is enabled, owned, and not being owned.
  return (tpm_init_task_->get_tpm()->IsEnabled() &&
          tpm_init_task_->get_tpm()->IsOwned() &&
          !tpm_init_task_->get_tpm()->IsBeingOwned() &&
          platform_->FileExists(kTpmOwnedFile));
}

bool TpmInit::IsTpmEnabled() {
  return tpm_init_task_->get_tpm()->IsEnabled();
}

bool TpmInit::IsTpmOwned() {
  return tpm_init_task_->get_tpm()->IsOwned();
}

void TpmInit::SetTpmOwned(bool owned) {
  tpm_init_task_->get_tpm()->SetIsOwned(owned);
}

bool TpmInit::IsTpmBeingOwned() {
  return tpm_init_task_->get_tpm()->IsBeingOwned();
}

void TpmInit::SetTpmBeingOwned(bool being_owned) {
  tpm_init_task_->get_tpm()->SetIsBeingOwned(being_owned);
}

bool TpmInit::OwnershipRequested() {
  return take_ownership_called_;
}

bool TpmInit::GetTpmPassword(brillo::Blob* password) {
  return tpm_init_task_->get_tpm()->GetOwnerPassword(password);
}

void TpmInit::ClearStoredTpmPassword() {
  TpmStatus tpm_status;
  if (LoadTpmStatus(&tpm_status)) {
    int32_t dependency_flags = TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER |
                               TpmStatus::ATTESTATION_NEEDS_OWNER;
    if (tpm_status.flags() & dependency_flags) {
      // The password is still needed, do not clear.
      return;
    }
    if (tpm_status.has_owner_password()) {
      tpm_status.clear_owner_password();
      StoreTpmStatus(tpm_status);
    }
  }
  get_tpm()->ClearStoredPassword();
}

void TpmInit::ThreadMain() {
  base::TimeTicks start = base::TimeTicks::Now();
  bool ownership_result = TakeOwnership(&took_ownership_);
  base::TimeDelta delta = (base::TimeTicks::Now() - start);
  initialization_time_ = delta.InMilliseconds();
  if (took_ownership_) {
    LOG(ERROR) << "Taking TPM ownership took " << initialization_time_ << "ms";
  }
  if (!ownership_callback_.is_null()) {
    ownership_callback_.Run(ownership_result, took_ownership_);
  }
}

bool TpmInit::SetupTpm(bool load_key) {
  const bool was_initialized = get_tpm()->IsInitialized();
  if (!was_initialized) {
    get_tpm()->SetIsInitialized(true);
    RestoreTpmStateFromStorage();
  }

  // Collect version statistics.
  if (!statistics_reported_) {
    Tpm::TpmVersionInfo version_info;
    if (get_tpm()->GetVersionInfo(&version_info)) {
      ReportVersionFingerprint(version_info.GetFingerprint());
      statistics_reported_ = true;
    }
  }

  // In case of interrupted initialization, continue it.
  if (IsTpmOwned() && !platform_->FileExists(kTpmOwnedFile)) {
    LOG(WARNING) << "Taking ownership was interrupted, continuing.";
    AsyncTakeOwnership();
  }

  if (load_key) {
    // load cryptohome key
    LoadOrCreateCryptohomeKey(&cryptohome_key_);
  }
  return !was_initialized;
}

void TpmInit::RestoreTpmStateFromStorage() {
  // Checking disabled and owned either via sysfs or via TSS calls will block if
  // ownership is being taken by another thread or process.  So for this to work
  // well, SetupTpm() needs to be called before TakeOwnership() is called.  At
  // that point, the public API for Tpm only checks these booleans, so other
  // threads can check without being blocked.  TakeOwnership() will reset the
  // TPM's is_owned_ bit on success.
  bool is_enabled = false;
  bool is_owned = false;
  bool successful_check = false;
  if (platform_->FileExists(kTpmTpmCheckEnabledFile)) {
    is_enabled = IsEnabledCheckViaSysfs(kTpmTpmCheckEnabledFile);
    is_owned = IsOwnedCheckViaSysfs(kTpmTpmCheckOwnedFile);
    successful_check = true;
  } else if (platform_->FileExists(kMiscTpmCheckEnabledFile)) {
    is_enabled = IsEnabledCheckViaSysfs(kMiscTpmCheckEnabledFile);
    is_owned = IsOwnedCheckViaSysfs(kMiscTpmCheckOwnedFile);
    successful_check = true;
  } else {
    if (get_tpm()->PerformEnabledOwnedCheck(&is_enabled, &is_owned)) {
      successful_check = true;
    }
  }

  get_tpm()->SetIsOwned(is_owned);
  get_tpm()->SetIsEnabled(is_enabled);

  if (successful_check && !is_owned) {
    platform_->DeleteFileDurable(kOpenCryptokiPath, true);
    platform_->DeleteFileDurable(kTpmOwnedFile, false);
    platform_->DeleteFileDurable(kTpmStatusFile, false);
  }

  TpmStatus tpm_status;
  if (LoadTpmStatus(&tpm_status)) {
    if (tpm_status.has_owner_password()) {
      SecureBlob local_owner_password;
      if (LoadOwnerPassword(tpm_status, &local_owner_password)) {
        get_tpm()->SetOwnerPassword(local_owner_password);
      }
    }
  }
}

bool TpmInit::TakeOwnership(bool* OUT_took_ownership) {
  TpmStatus tpm_status;

  if (!LoadTpmStatus(&tpm_status)) {
    tpm_status.Clear();
    tpm_status.set_flags(TpmStatus::NONE);
  }

  if (OUT_took_ownership) {
    *OUT_took_ownership = false;
  }

  if (!IsTpmEnabled()) {
    return false;
  }

  SecureBlob default_owner_password(sizeof(kTpmWellKnownPassword));
  memcpy(default_owner_password.data(), kTpmWellKnownPassword,
         sizeof(kTpmWellKnownPassword));

  bool took_ownership = false;
  if (!IsTpmOwned()) {
    SetTpmBeingOwned(true);
    platform_->DeleteFileDurable(kOpenCryptokiPath, true);
    platform_->DeleteFileDurable(kTpmOwnedFile, false);
    platform_->DeleteFileDurable(kTpmStatusFile, false);

    if (!get_tpm()->IsEndorsementKeyAvailable()) {
      if (!get_tpm()->CreateEndorsementKey()) {
        LOG(ERROR) << "Failed to create endorsement key";
        SetTpmBeingOwned(false);
        return false;
      }
    }

    if (!get_tpm()->IsEndorsementKeyAvailable()) {
      LOG(ERROR) << "Endorsement key is not available";
      SetTpmBeingOwned(false);
      return false;
    }

    if (!get_tpm()->TakeOwnership(kMaxTimeoutRetries, default_owner_password)) {
      LOG(ERROR) << "Take Ownership failed";
      SetTpmBeingOwned(false);
      return false;
    }

    SetTpmOwned(true);
    took_ownership = true;

    tpm_status.set_flags(TpmStatus::OWNED_BY_THIS_INSTALL |
                         TpmStatus::USES_WELL_KNOWN_OWNER |
                         TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER |
                         TpmStatus::ATTESTATION_NEEDS_OWNER);
    tpm_status.clear_owner_password();
    StoreTpmStatus(tpm_status);
  }

  if (OUT_took_ownership) {
    *OUT_took_ownership = took_ownership;
  }

  // If we can open the TPM with the default password, then we still need to
  // zero the SRK password and unrestrict it, then change the owner password.
  if (!platform_->FileExists(kTpmOwnedFile) &&
      get_tpm()->TestTpmAuth(default_owner_password)) {
    if (!get_tpm()->InitializeSrk(default_owner_password)) {
      LOG(ERROR) << "Couldn't initialize the SRK";
      SetTpmBeingOwned(false);
      return false;
    }

    SecureBlob owner_password;
    CreateOwnerPassword(&owner_password);

    tpm_status.set_flags(TpmStatus::OWNED_BY_THIS_INSTALL |
                         TpmStatus::USES_RANDOM_OWNER |
                         TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER |
                         TpmStatus::ATTESTATION_NEEDS_OWNER);
    if (!StoreOwnerPassword(owner_password, &tpm_status)) {
      LOG(ERROR) << "Couldn't store the owner password.";
      tpm_status.clear_owner_password();
      return false;
    }
    StoreTpmStatus(tpm_status);

    if ((!get_tpm()->ChangeOwnerPassword(default_owner_password,
                                         owner_password))) {
      LOG(ERROR) << "Couldn't change the owner password.";
      return false;
    }
    get_tpm()->SetOwnerPassword(owner_password);
  }

  // If we fall through here, either (1) we successfully completed the
  // initialization, or (2) then the TPM owned file doesn't exist, but we
  // couldn't auth with the well-known password. In the second case, we must
  // assume that the TPM has already been owned and set to a random password.
  // In any case, it's time to touch the TPM owned file to indicate that we
  // don't need to re-attempt completing initialization on the next boot.
  if (!platform_->FileExists(kTpmOwnedFile)) {
    platform_->TouchFileDurable(kTpmOwnedFile);
  }

  SetTpmBeingOwned(false);
  return true;
}

bool TpmInit::LoadTpmStatus(TpmStatus* serialized) {
  if (!platform_->FileExists(kTpmStatusFile)) {
    return false;
  }
  SecureBlob file_data;
  if (!platform_->ReadFile(kTpmStatusFile, &file_data)) {
    return false;
  }
  if (!serialized->ParseFromArray(file_data.data(), file_data.size())) {
    return false;
  }
  return true;
}

bool TpmInit::StoreTpmStatus(const TpmStatus& serialized) {
  if (platform_->FileExists(kTpmStatusFile)) {
    // Shred old status file, not very useful on SSD. :(
    do {
      int64_t file_size;
      if (!platform_->GetFileSize(kTpmStatusFile, &file_size)) {
        break;
      }
      SecureBlob random;
      if (!get_tpm()->GetRandomData(file_size, &random)) {
        break;
      }
      platform_->WriteFile(kTpmStatusFile, random);
      platform_->DataSyncFile(kTpmStatusFile);
    } while (false);
    platform_->DeleteFile(kTpmStatusFile, false);
  }

  SecureBlob final_blob(serialized.ByteSize());
  serialized.SerializeWithCachedSizesToArray(
      static_cast<google::protobuf::uint8*>(final_blob.data()));
  bool ok = platform_->WriteFileAtomicDurable(kTpmStatusFile, final_blob, 0600);
  return ok;
}

void TpmInit::CreateOwnerPassword(SecureBlob* password) {
  // Generate a random owner password.  The default is a 12-character,
  // hex-encoded password created from 6 bytes of random data.
  SecureBlob random(kOwnerPasswordLength / 2);
  CryptoLib::GetSecureRandom(random.data(), random.size());
  SecureBlob tpm_password(kOwnerPasswordLength);
  CryptoLib::BlobToHexToBuffer(random,
                               tpm_password.data(),
                               tpm_password.size());
  password->swap(tpm_password);
}

bool TpmInit::LoadOwnerPassword(const TpmStatus& tpm_status,
                                brillo::Blob* owner_password) {
  if (!(tpm_status.flags() & TpmStatus::OWNED_BY_THIS_INSTALL)) {
    return false;
  }
  if ((tpm_status.flags() & TpmStatus::USES_WELL_KNOWN_OWNER)) {
    SecureBlob default_owner_password(sizeof(kTpmWellKnownPassword));
    memcpy(default_owner_password.data(), kTpmWellKnownPassword,
           sizeof(kTpmWellKnownPassword));
    owner_password->swap(default_owner_password);
    return true;
  }
  if (!(tpm_status.flags() & TpmStatus::USES_RANDOM_OWNER) ||
      !tpm_status.has_owner_password()) {
    return false;
  }

  SecureBlob local_owner_password(tpm_status.owner_password().length());
  tpm_status.owner_password().copy(
      local_owner_password.char_data(),
      tpm_status.owner_password().length(), 0);
  if (!get_tpm()->Unseal(local_owner_password, owner_password)) {
    LOG(ERROR) << "Failed to unseal the owner password.";
    return false;
  }
  return true;
}

bool TpmInit::StoreOwnerPassword(const brillo::Blob& owner_password,
                                 TpmStatus* tpm_status) {
  // Use PCR0 when sealing the data so that the owner password is only
  // available in the current boot mode.  This helps protect the password from
  // offline attacks until it has been presented and cleared.
  SecureBlob sealed_password;
  if (!get_tpm()->SealToPCR0(owner_password, &sealed_password)) {
    LOG(ERROR) << "StoreOwnerPassword: Failed to seal owner password.";
    return false;
  }
  tpm_status->set_owner_password(sealed_password.data(),
                                 sealed_password.size());
  return true;
}

void TpmInit::RemoveTpmOwnerDependency(Tpm::TpmOwnerDependency dependency) {
  int32_t flag_to_clear = TpmStatus::NONE;
  switch (dependency) {
    case Tpm::TpmOwnerDependency::kInstallAttributes:
      flag_to_clear = TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER;
      break;
    case Tpm::TpmOwnerDependency::kAttestation:
      flag_to_clear = TpmStatus::ATTESTATION_NEEDS_OWNER;
      break;
    default:
      CHECK(false);
  }
  if (!get_tpm()->RemoveOwnerDependency(dependency)) {
    return;
  }
  TpmStatus tpm_status;
  if (!LoadTpmStatus(&tpm_status))
    return;
  if (tpm_status.flags() & flag_to_clear) {
    tpm_status.set_flags(tpm_status.flags() & ~flag_to_clear);
    StoreTpmStatus(tpm_status);
  }
}

bool TpmInit::CheckSysfsForOne(const FilePath& file_name) const {
  std::string contents;
  if (!platform_->ReadFileToString(file_name, &contents)) {
    return false;
  }
  if (contents.size() < 1) {
    return false;
  }
  return (contents[0] == '1');
}

bool TpmInit::IsEnabledCheckViaSysfs(const FilePath& enabled_file) {
  return CheckSysfsForOne(enabled_file);
}

bool TpmInit::IsOwnedCheckViaSysfs(const FilePath& owned_file) {
  return CheckSysfsForOne(owned_file);
}

bool TpmInit::CreateCryptohomeKey() {
  if (!IsTpmReady()) {
    LOG(WARNING) << "Canceled creating cryptohome key - TPM is not ready.";
    return false;
  }
  SecureBlob n;
  SecureBlob p;
  if (!CryptoLib::CreateRsaKey(kDefaultTpmRsaKeyBits, &n, &p)) {
    LOG(ERROR) << "Error creating RSA key";
    return false;
  }
  brillo::SecureBlob wrapped_key;
  if (!get_tpm()->WrapRsaKey(n, p, &wrapped_key)) {
    LOG(ERROR) << "Couldn't wrap cryptohome key";
    return false;
  }

  if (!SaveCryptohomeKey(wrapped_key)) {
    LOG(ERROR) << "Couldn't save cryptohome key";
    return false;
  }

  LOG(INFO) << "Created new cryptohome key.";
  return true;
}

bool TpmInit::SaveCryptohomeKey(const brillo::SecureBlob& raw_key) {
  bool ok = platform_->WriteFileAtomicDurable(kDefaultCryptohomeKeyFile,
                                              raw_key, 0600);
  if (!ok)
    LOG(ERROR) << "Error writing key file of desired size: " << raw_key.size();
  return ok;
}

Tpm::TpmRetryAction TpmInit::LoadCryptohomeKey(ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // First, try loading the key from the key file
  {
    SecureBlob raw_key;
    if (platform_->ReadFile(kDefaultCryptohomeKeyFile, &raw_key)) {
      Tpm::TpmRetryAction retry_action = get_tpm()->LoadWrappedKey(
          raw_key, key_handle);
      if (retry_action == Tpm::kTpmRetryNone ||
          get_tpm()->IsTransient(retry_action)) {
        return retry_action;
      }
    }
  }

  // Then try loading the key by the UUID (this is a legacy upgrade path)
  SecureBlob raw_key;
  if (!get_tpm()->LegacyLoadCryptohomeKey(key_handle,
                                          &raw_key)) {
    return Tpm::kTpmRetryFailNoRetry;
  }

  // Save the cryptohome key to the well-known location
  if (!SaveCryptohomeKey(raw_key)) {
    LOG(ERROR) << "Couldn't save cryptohome key";
    return Tpm::kTpmRetryFailNoRetry;
  }
  return Tpm::kTpmRetryNone;
}

bool TpmInit::LoadOrCreateCryptohomeKey(ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // Try to load the cryptohome key.
  Tpm::TpmRetryAction retry_action = LoadCryptohomeKey(key_handle);
  if (retry_action != Tpm::kTpmRetryNone &&
      !get_tpm()->IsTransient(retry_action)) {
    // The key couldn't be loaded, and it wasn't due to a transient error,
    // so we must create the key.
    if (CreateCryptohomeKey()) {
      retry_action = LoadCryptohomeKey(key_handle);
    }
  }
  return retry_action == Tpm::kTpmRetryNone;
}

bool TpmInit::HasCryptohomeKey() {
  return (cryptohome_key_.value() != kInvalidKeyHandle);
}

TpmKeyHandle TpmInit::GetCryptohomeKey() {
  return cryptohome_key_.value();
}

bool TpmInit::ReloadCryptohomeKey() {
  CHECK(HasCryptohomeKey());
  // Release the handle first, we know this handle doesn't contain a loaded key
  // since ReloadCryptohomeKey only called after we failed to use it.
  // Otherwise we may flush the newly loaded key and fail to use it again,
  // if it is loaded to the same handle.
  // TODO(crbug.com/687330): change to closing the handle and ignoring errors
  // once checking for stale virtual handles is implemented in trunksd.
  cryptohome_key_.release();
  if (LoadCryptohomeKey(&cryptohome_key_) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error reloading Cryptohome key.";
    return false;
  }
  return true;
}

bool TpmInit::GetVersion(Tpm::TpmVersionInfo* version_info) {
  return get_tpm() && get_tpm()->GetVersionInfo(version_info);
}

}  // namespace cryptohome
