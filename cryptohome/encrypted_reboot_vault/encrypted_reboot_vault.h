// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ENCRYPTED_REBOOT_VAULT_ENCRYPTED_REBOOT_VAULT_H_
#define CRYPTOHOME_ENCRYPTED_REBOOT_VAULT_ENCRYPTED_REBOOT_VAULT_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libstorage/platform/dircrypto_util.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/storage_container.h>

class EncryptedRebootVault {
 public:
  EncryptedRebootVault();
  ~EncryptedRebootVault() = default;
  // Check if the encrypted reboot vault is setup correctly.
  bool Validate();
  // Unconditionally reset vault.
  bool CreateVault();
  // Setup existing vault; purge on failure.
  bool UnlockVault();
  // Purge vault.
  bool PurgeVault();

 private:
  base::FilePath vault_path_;
  libstorage::Platform platform_;
  std::unique_ptr<libstorage::Keyring> keyring_;
  std::unique_ptr<libstorage::StorageContainer> encrypted_container_;
};

#endif  // CRYPTOHOME_ENCRYPTED_REBOOT_VAULT_ENCRYPTED_REBOOT_VAULT_H_
