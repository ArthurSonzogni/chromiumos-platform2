/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This tool will attempt to mount or create the encrypted stateful partition,
 * and the various bind mountable subdirectories.
 *
 */
#define _FILE_OFFSET_BITS 64
#define CHROMEOS_ENVIRONMENT

#include <fcntl.h>
#include <sys/time.h>
#include <vboot/crossystem.h>
#include <vboot/tlcl.h>

#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>

#include <metrics/metrics_library.h>

#include "cryptohome/mount_encrypted.h"
#include "cryptohome/mount_encrypted/encrypted_fs.h"
#include "cryptohome/mount_encrypted/encryption_key.h"
#include "cryptohome/mount_encrypted/tpm.h"
#include "cryptohome/mount_helpers.h"

#define PROP_SIZE 64

#if DEBUG_ENABLED
struct timeval tick = {};
struct timeval tick_start = {};
#endif

static const gchar* const kNvramExport = "/tmp/lockbox.nvram";
static gchar* rootdir = NULL;

static const char kMountEncryptedMetricsPath[] = "/run/metrics.mount-encrypted";

namespace metrics {
const char kSystemKeyStatus[] = "Platform.MountEncrypted.SystemKeyStatus";
const char kEncryptionKeyStatus[] =
    "Platform.MountEncrypted.EncryptionKeyStatus";
}

static result_code get_system_property(const char* prop, char* buf,
                                       size_t length) {
  const char* rc;

  DEBUG("Fetching System Property '%s'", prop);
  rc = VbGetSystemPropertyString(prop, buf, length);
  DEBUG("Got System Property 'mainfw_type': %s", rc ? buf : "FAIL");

  return rc != NULL ? RESULT_SUCCESS : RESULT_FAIL_FATAL;
}

static int has_chromefw(void) {
  static int state = -1;
  char fw[PROP_SIZE];

  /* Cache the state so we don't have to perform the query again. */
  if (state != -1)
    return state;

  if (get_system_property("mainfw_type", fw, sizeof(fw)) != RESULT_SUCCESS)
    state = 0;
  else
    state = strcmp(fw, "nonchrome") != 0;
  return state;
}


// This triggers the live encryption key to be written to disk, encrypted by the
// system key. It is intended to be called by Cryptohome once the TPM is done
// being set up. If the system key is passed as an argument, use it, otherwise
// attempt to query the TPM again.
static result_code finalize_from_cmdline(
    const cryptohome::EncryptedFs& encrypted_fs,
    char* key) {
  // Load the system key.
  brillo::SecureBlob system_key;
  if (!base::HexStringToBytes(key, &system_key) ||
      system_key.size() != DIGEST_LENGTH) {
    LOG(ERROR) << "Failed to parse system key.";
    return RESULT_FAIL_FATAL;
  }

  FixedSystemKeyLoader loader(system_key);
  EncryptionKey key_manager(&loader, base::FilePath(rootdir));
  result_code rc = key_manager.SetTpmSystemKey();
  if (rc != RESULT_SUCCESS) {
    return rc;
  }

  // If there already is an encrypted system key on disk, there is nothing to
  // do. This also covers cases where the system key is not derived from the
  // lockbox space contents (e.g. TPM 2.0 devices, TPM 1.2 devices with
  // encrypted stateful space, factory keys, etc.), for which it is not
  // appropriate to replace the system key. For cases where finalization is
  // unfinished, we clear any stale system keys from disk to make sure we pass
  // the check here.
  if (base::PathExists(key_manager.key_path())) {
    return RESULT_SUCCESS;
  }

  // Load the encryption key.
  char* encryption_key = encrypted_fs.get_mount_key();
  if (!encryption_key) {
    ERROR("Could not get mount encryption key");
    return RESULT_FAIL_FATAL;
  }
  brillo::SecureBlob encryption_key_blob;
  if (!base::HexStringToBytes(encryption_key, &encryption_key_blob)) {
    ERROR("Failed to decode encryption key.");
    return RESULT_FAIL_FATAL;
  }

  // Persist the encryption key to disk.
  key_manager.PersistEncryptionKey(encryption_key_blob);

  return RESULT_SUCCESS;
}

static result_code report_info(const cryptohome::EncryptedFs& encrypted_fs) {
  Tpm tpm;
  printf("TPM: %s\n", tpm.available() ? "yes" : "no");
  if (tpm.available()) {
    bool owned = false;
    printf("TPM Owned: %s\n", tpm.IsOwned(&owned) == RESULT_SUCCESS
                                  ? (owned ? "yes" : "no")
                                  : "fail");
  }
  printf("ChromeOS: %s\n", has_chromefw() ? "yes" : "no");
  printf("TPM2: %s\n", tpm.is_tpm2() ? "yes" : "no");
  if (has_chromefw()) {
    brillo::SecureBlob system_key;
    auto loader = SystemKeyLoader::Create(&tpm, base::FilePath(rootdir));
    result_code rc = loader->Load(&system_key);
    if (rc != RESULT_SUCCESS) {
      printf("NVRAM: missing.\n");
    } else {
      printf("NVRAM: available.\n");
    }
  } else {
    printf("NVRAM: not present\n");
  }

  // Report info from the encrypted mount.
  encrypted_fs.report_mount_info();

  return RESULT_SUCCESS;
}

/* Exports NVRAM contents to tmpfs for use by install attributes */
void nvram_export(const brillo::SecureBlob& contents) {
  int fd;
  DEBUG("Export NVRAM contents");
  fd = open(kNvramExport, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    perror("open(nvram_export)");
    return;
  }
  if (write(fd, contents.data(), contents.size()) != contents.size()) {
    /* Don't leave broken files around */
    unlink(kNvramExport);
  }
  close(fd);
}

template <typename Enum>
void RecordEnumeratedHistogram(MetricsLibrary* metrics,
                               const char* name,
                               Enum val) {
  metrics->SendEnumToUMA(name, static_cast<int>(val),
                         static_cast<int>(Enum::kCount));
}

int main(int argc, char* argv[]) {
  result_code rc;
  rootdir = getenv("MOUNT_ENCRYPTED_ROOT");
  cryptohome::EncryptedFs encrypted_fs;

  MetricsLibrary metrics;
  metrics.Init();
  metrics.SetOutputFile(kMountEncryptedMetricsPath);

  INFO_INIT("Starting.");
  rc = encrypted_fs.prepare_paths(rootdir);
  if (rc != RESULT_SUCCESS)
    return rc;

  bool use_factory_system_key = false;
  if (argc > 1) {
    if (!strcmp(argv[1], "umount")) {
      return encrypted_fs.teardown_mount();
    } else if (!strcmp(argv[1], "info")) {
      return report_info(encrypted_fs);
    } else if (!strcmp(argv[1], "finalize")) {
      return finalize_from_cmdline(encrypted_fs, argc > 2 ? argv[2] : NULL);
    } else if (!strcmp(argv[1], "factory")) {
      use_factory_system_key = true;
    } else {
      fprintf(stderr, "Usage: %s [info|finalize|umount|factory]\n", argv[0]);
      return RESULT_FAIL_FATAL;
    }
  }

  /* For the mount operation at boot, return RESULT_FAIL_FATAL to trigger
   * chromeos_startup do the stateful wipe.
   */
  rc = encrypted_fs.check_mount_states();
  if (rc != RESULT_SUCCESS)
    return rc;

  Tpm tpm;
  auto loader = SystemKeyLoader::Create(&tpm, base::FilePath(rootdir));
  EncryptionKey key(loader.get(), base::FilePath(rootdir));
  if (use_factory_system_key) {
    rc = key.SetFactorySystemKey();
  } else if (has_chromefw()) {
    rc = key.LoadChromeOSSystemKey();
  } else {
    rc = key.SetInsecureFallbackSystemKey();
  }
  RecordEnumeratedHistogram(&metrics, metrics::kSystemKeyStatus,
                            key.system_key_status());
  if (rc != RESULT_SUCCESS) {
    return rc;
  }

  rc = key.LoadEncryptionKey();
  RecordEnumeratedHistogram(&metrics, metrics::kEncryptionKeyStatus,
                            key.encryption_key_status());
  if (rc != RESULT_SUCCESS) {
    return rc;
  }

  std::string encryption_key_hex =
      base::HexEncode(key.encryption_key().data(), key.encryption_key().size());
  rc = encrypted_fs.setup_encrypted(encryption_key_hex.c_str(),
                                    key.is_fresh());
  if (rc == RESULT_SUCCESS) {
    bool lockbox_valid = false;
    if (loader->CheckLockbox(&lockbox_valid) == RESULT_SUCCESS) {
      NvramSpace* lockbox_space = tpm.GetLockboxSpace();
      if (lockbox_valid && lockbox_space->is_valid()) {
        LOG(INFO) << "Lockbox is valid, exporting.";
        nvram_export(lockbox_space->contents());
      }
    } else {
      LOG(ERROR) << "Lockbox validity check error.";
    }
  }

  INFO_DONE("Done.");

  /* Continue boot. */
  return rc;
}
