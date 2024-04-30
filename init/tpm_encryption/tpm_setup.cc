/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This tool will attempt to mount or create the encrypted stateful partition,
 * and the various bind mountable subdirectories.
 *
 */

#include <fcntl.h>
#include <sys/time.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/files/file_util.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <libcrossystem/crossystem.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container_factory.h>

#include "init/metrics/metrics.h"
#include "init/tpm_encryption/encryption_key.h"
#include "init/tpm_encryption/tpm.h"
#include "init/tpm_encryption/tpm_setup.h"

using encryption::paths::cryptohome::kTpmOwned;

namespace encryption {

namespace {
constexpr char kBioCryptoInitPath[] = "/usr/bin/bio_crypto_init";
constexpr char kBioTpmSeedSalt[] = "biod";
constexpr char kBioTpmSeedTmpDir[] = "/run/bio_crypto_init";
constexpr char kBioTpmSeedFile[] = "seed";
constexpr char kFeaturedTpmSeedSalt[] = "featured";
constexpr char kFeaturedTpmSeedTmpDir[] = "/run/featured_seed";
constexpr char kFeaturedTpmSeedFile[] = "tpm_seed";
constexpr char kOldTpmOwnershipStateFile[] = ".tpm_owned";
constexpr char kNvramExport[] = "tmp/lockbox.nvram";

bool SendSecretToTmpFile(const encryption::EncryptionKey& key,
                         const std::string& salt,
                         const base::FilePath& tmp_dir,
                         const std::string& filename,
                         uid_t user_id,
                         gid_t group_id,
                         libstorage::Platform* platform) {
  brillo::SecureBlob tpm_seed = key.GetDerivedSystemKey(salt);
  if (tpm_seed.empty()) {
    LOG(ERROR) << "TPM Seed provided for " << filename << " is empty";
    return false;
  }

  if (!platform->SafeCreateDirAndSetOwnershipAndPermissions(
          tmp_dir,
          /* mode=700 */ S_IRUSR | S_IWUSR | S_IXUSR, user_id, group_id)) {
    PLOG(ERROR) << "Failed to CreateDir or SetOwnershipAndPermissions of "
                << tmp_dir;
    return false;
  }

  auto file = tmp_dir.Append(filename);
  if (!platform->WriteStringToFileAtomic(file, tpm_seed.to_string(),
                                         /* mode=600 */ S_IRUSR | S_IWUSR)) {
    PLOG(ERROR) << "Failed to write TPM seed to tmpfs file " << filename;
    return false;
  }

  if (!platform->SetOwnership(file, user_id, group_id, true)) {
    PLOG(ERROR) << "Failed to change ownership/perms of tmpfs file "
                << filename;
    // Remove the file as it contains the tpm seed with incorrect owner.
    PLOG_IF(ERROR, !platform->DeleteFile(file)) << "Unable to remove file!";
    return false;
  }

  return true;
}

// Send a secret derived from the system key to the biometric managers, if
// available, via a tmpfs file which will be read by bio_crypto_init. The tmpfs
// directory will be created if it doesn't exist.
bool SendSecretToBiodTmpFile(const encryption::EncryptionKey& key,
                             libstorage::Platform* platform) {
  // If there isn't a bio-sensor, don't bother.
  if (!platform->FileExists(base::FilePath(kBioCryptoInitPath))) {
    LOG(INFO)
        << "There is no bio_crypto_init binary, so skip sending TPM seed.";
    return true;
  }

  return SendSecretToTmpFile(
      key, std::string(kBioTpmSeedSalt), base::FilePath(kBioTpmSeedTmpDir),
      kBioTpmSeedFile, libstorage::kBiodUid, libstorage::kBiodGid, platform);
}

// Send a secret derived from the system key to featured, if available, via a
// tmpfs file which will be read by featured. The tmpfs directory will be
// created if it doesn't exist.
bool SendSecretToFeaturedTmpFile(const encryption::EncryptionKey& key,
                                 libstorage::Platform* platform) {
  return SendSecretToTmpFile(key, std::string(kFeaturedTpmSeedSalt),
                             base::FilePath(kFeaturedTpmSeedTmpDir),
                             kFeaturedTpmSeedFile, libstorage::kRootUid,
                             libstorage::kRootGid, platform);
}

}  // namespace

TpmSystemKey::TpmSystemKey(libstorage::Platform* platform,
                           hwsec_foundation::TlclWrapper* tlcl,
                           init_metrics::InitMetrics* metrics,
                           base::FilePath rootdir,
                           base::FilePath stateful_mount)
    : platform_(platform),
      tlcl_(tlcl),
      metrics_(metrics),
      rootdir_(rootdir),
      stateful_mount_(stateful_mount),
      tpm_(tlcl_),
      loader_(encryption::SystemKeyLoader::Create(
          platform_, &tpm_, rootdir_, stateful_mount_)) {}

bool TpmSystemKey::Set(base::FilePath key_material_file) {
  if (!tpm_.is_tpm2()) {
    LOG(WARNING) << "Custom system key is not supported in TPM 1.2.";
    return false;
  }

  brillo::SecureBlob key_material;
  if (!platform_->ReadFileToSecureBlob(key_material_file, &key_material)) {
    LOG(ERROR) << "Failed to read custom system key material from file "
               << key_material_file.value();
    return false;
  }

  bool rc = loader_->Initialize(key_material, nullptr);
  if (!rc) {
    LOG(ERROR) << "Failed to initialize system key NV space contents.";
    return false;
  }

  rc = loader_->Persist();
  if (!rc) {
    LOG(ERROR) << "Failed to persist custom system key material in NVRAM.";
    return false;
  }

  return true;
}

std::optional<encryption::EncryptionKey> TpmSystemKey::Load(
    bool safe_mount, base::FilePath backup) {
  bool rc;

  if (!MigrateTpmOwnerShipStateFile()) {
    LOG(ERROR) << "Failed to migrate tpm owership state file to" << kTpmOwned;
  }

  encryption::EncryptionKey key(platform_, loader_.get(), rootdir_,
                                stateful_mount_);
  if (ShallUseTpmForSystemKey() && safe_mount) {
    if (!tpm_.available()) {
      // The TPM should be available before we load the system_key.
      LOG(ERROR) << "TPM not available.";
      // We shouldn't continue to load the system_key.
      return std::nullopt;
    }
    rc = key.LoadChromeOSSystemKey(backup);
  } else {
    rc = key.SetInsecureFallbackSystemKey();
  }
  metrics_->ReportSystemKeyStatus(key.system_key_status());
  if (!rc) {
    return std::nullopt;
  }

  rc = key.LoadEncryptionKey();
  metrics_->ReportEncryptionKeyStatus(key.encryption_key_status());
  if (!rc) {
    return std::nullopt;
  }

  /* Log errors during sending seed to biod, but don't stop execution. */
  if (HasChromeFw()) {
    LOG_IF(ERROR, !SendSecretToBiodTmpFile(key, platform_))
        << "Failed to send TPM secret to biod.";
  } else {
    LOG(ERROR) << "biod won't get a TPM seed without chromefw.";
  }

  /* Log errors during sending seed to featured, but don't stop
   * execution. */
  if (ShallUseTpmForSystemKey()) {
    LOG_IF(ERROR, !SendSecretToFeaturedTmpFile(key, platform_))
        << "Failed to send TPM secret to featured.";
  } else {
    LOG(ERROR) << "Failed to load TPM system key, featured won't "
                  "get a TPM seed.";
  }

  return key;
}

void TpmSystemKey::ReportInfo() {
  printf("TPM: %s\n", tpm_.available() ? "yes" : "no");
  if (tpm_.available()) {
    bool owned = false;
    printf("TPM Owned: %s\n",
           tpm_.IsOwned(&owned) ? (owned ? "yes" : "no") : "fail");
  }
  printf("ChromeOS: %s\n", HasChromeFw() ? "yes" : "no");
  printf("TPM2: %s\n", tpm_.is_tpm2() ? "yes" : "no");
  if (ShallUseTpmForSystemKey()) {
    brillo::SecureBlob system_key;
    bool rc = loader_->Load(&system_key);
    if (!rc) {
      printf("NVRAM: missing.\n");
    } else {
      printf("NVRAM: available.\n");
    }
  } else {
    printf("NVRAM: not present\n");
  }
}

bool TpmSystemKey::Export() {
  /* Only check the lockbox when we are using TPM for system key. */
  if (ShallUseTpmForSystemKey()) {
    bool lockbox_valid = false;
    if (loader_->CheckLockbox(&lockbox_valid)) {
      encryption::NvramSpace* lockbox_space = tpm_.GetLockboxSpace();
      if (lockbox_valid && lockbox_space->is_valid()) {
        LOG(INFO) << "Lockbox is valid, exporting.";
        return platform_->WriteSecureBlobToFile(rootdir_.Append(kNvramExport),
                                                lockbox_space->contents());
      }
    } else {
      LOG(ERROR) << "Lockbox validity check error.";
    }
  }
  LOG(INFO) << "Done.";
  return true;
}

bool TpmSystemKey::HasChromeFw() {
  if (!has_chromefw_) {
    auto fw = platform_->GetCrosssystem()->VbGetSystemPropertyString(
        crossystem::Crossystem::kMainFirmwareType);
    if (!fw)
      has_chromefw_ = false;
    else
      has_chromefw_ = (fw != crossystem::Crossystem::kMainfwTypeNonchrome);
  }
  return *has_chromefw_;
}

bool TpmSystemKey::ShallUseTpmForSystemKey() {
  if (!USE_TPM_INSECURE_FALLBACK) {
    return true;
  }

  if (HasChromeFw()) {
    return true;
  }

  /* Don't use tpm for system key if we are using runtime TPM selection. */
  if (USE_TPM_DYNAMIC) {
    return false;
  }

  /* Assume we have tpm for system_key when we are using vtpm tpm2 simulator. */
  return USE_TPM2_SIMULATOR && USE_VTPM_PROXY;
}

bool TpmSystemKey::MigrateTpmOwnerShipStateFile() {
  base::FilePath tpm_owned =
      stateful_mount_.Append("unencrypted/tpm_manager/tpm_owned");
  base::FilePath old_tpm_state =
      stateful_mount_.Append(kOldTpmOwnershipStateFile);

  if (!platform_->CreateDirectory(tpm_owned.DirName())) {
    LOG(ERROR) << "Failed to create dir for TPM pwnership state file.";
    return false;
  }

  if (platform_->FileExists(old_tpm_state)) {
    LOG(INFO) << old_tpm_state.value() << " exists. " << "Moving it to "
              << tpm_owned.value();
    return platform_->Rename(old_tpm_state, tpm_owned);
  }
  return true;
}

}  // namespace encryption
