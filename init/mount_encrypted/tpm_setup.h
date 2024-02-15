/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Private header file for mount-encrypted helper tool.
 */
#ifndef INIT_MOUNT_ENCRYPTED_TPM_SETUP_H_
#define INIT_MOUNT_ENCRYPTED_TPM_SETUP_H_

#include <memory>
#include <optional>
#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <libstorage/platform/platform.h>

#include "init/metrics/metrics.h"
#include "init/mount_encrypted/tpm.h"

namespace mount_encrypted {

// Interface to communicate with outside world
class TpmSystemKey {
 public:
  TpmSystemKey(libstorage::Platform* platform,
               init_metrics::InitMetrics* metrics,
               base::FilePath rootdir);

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
  std::optional<mount_encrypted::EncryptionKey> Load(bool safe_mount);

  // Print encrypted data information.
  void ReportInfo();

  // Exports NVRAM contents to tmpfs for use by install attributes.
  bool Export();

 private:
  libstorage::Platform* platform_;
  init_metrics::InitMetrics* metrics_;
  base::FilePath rootdir_;
  Tpm tpm_;
  std::unique_ptr<SystemKeyLoader> loader_;
  bool has_chromefw_;

  // To check if the device is a chromebook.
  bool HasChromeFw();

  // Return true when a TPM is required to store the system key.
  bool ShallUseTpmForSystemKey();
};

}  // namespace mount_encrypted
#endif  // INIT_MOUNT_ENCRYPTED_TPM_SETUP_H_
