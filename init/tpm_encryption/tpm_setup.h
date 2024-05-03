/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Private header file for mount-encrypted helper tool.
 */
#ifndef INIT_TPM_ENCRYPTION_TPM_SETUP_H_
#define INIT_TPM_ENCRYPTION_TPM_SETUP_H_

#include <memory>
#include <optional>
#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <libstorage/platform/platform.h>

#include "init/metrics/metrics.h"
#include "init/tpm_encryption/tpm.h"

namespace encryption {

// Interface to communicate with outside world
class TpmSystemKey {
 public:
  TpmSystemKey(libstorage::Platform* platform,
               hwsec_foundation::TlclWrapper* tlcl,
               init_metrics::InitMetrics* metrics,
               base::FilePath rootdir,
               base::FilePath stateful_mount);

  // Reads key material from the file |key_material_file|, creates a system key
  // using the material, and persists the system key in NVRAM.
  //
  // This function only supports TPM 2.0 and should be called ONLY for testing
  // purposes.
  //
  // Doesn't take ownership of |platform|.
  // Return code indicates if every thing is successful.
  bool Set(const base::FilePath key_material_file);

  // Load key from TPM, spread to susbsystem that need it.
  // If |safe_mount| is set, fails if the TPM is not avaible when needed.
  std::optional<encryption::EncryptionKey> Load(bool safe_mount);

  // Print encrypted data information.
  void ReportInfo();

  // Exports NVRAM contents to tmpfs for use by install attributes.
  bool Export();

 private:
  libstorage::Platform* platform_;
  hwsec_foundation::TlclWrapper* tlcl_;
  init_metrics::InitMetrics* metrics_;
  base::FilePath rootdir_;
  base::FilePath stateful_mount_;
  Tpm tpm_;
  std::unique_ptr<SystemKeyLoader> loader_;
  bool has_chromefw_;

  // To check if the device is a chromebook.
  bool HasChromeFw();

  // Return true when a TPM is required to store the system key.
  bool ShallUseTpmForSystemKey();

  // Originally .tpm_owned file is located in /mnt/stateful_partition. Since the
  // directory can only be written by root, .tpm_owned won't be able to get
  // touched by tpm_managerd if we run it in minijail. Therefore, we need to
  // migrate the files from /mnt/stateful_partition to the files into
  // /mnt/stateful_partition/unencrypted/tpm_manager. The migration is written
  // here since mount-encrypted is started before tpm_managerd.
  bool MigrateTpmOwnerShipStateFile();
};

}  // namespace encryption
#endif  // INIT_TPM_ENCRYPTION_TPM_SETUP_H_
