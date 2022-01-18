// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ecryptfs_container.h"

#include <base/files/file_path.h>
#include <base/logging.h>

#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/keyring/keyring.h"

namespace cryptohome {

EcryptfsContainer::EcryptfsContainer(
    const base::FilePath& backing_dir,
    const FileSystemKeyReference& key_reference,
    Platform* platform,
    Keyring* keyring)
    : backing_dir_(backing_dir),
      key_reference_(key_reference),
      platform_(platform),
      keyring_(keyring) {}

bool EcryptfsContainer::Purge() {
  return platform_->DeletePathRecursively(backing_dir_);
}

bool EcryptfsContainer::Exists() {
  return platform_->DirectoryExists(backing_dir_);
}

bool EcryptfsContainer::Setup(const FileSystemKey& encryption_key) {
  if (!platform_->DirectoryExists(backing_dir_)) {
    if (!platform_->CreateDirectory(backing_dir_)) {
      LOG(ERROR) << "Failed to create backing directory";
      return false;
    }
  }
  return keyring_->AddKey(Keyring::KeyType::kEcryptfsKey, encryption_key,
                          &key_reference_);
}

bool EcryptfsContainer::SetLazyTeardownWhenUnused() {
  LOG(WARNING) << "ecryptfs containers do not support lazy teardown";
  return false;
}

bool EcryptfsContainer::Teardown() {
  return keyring_->RemoveKey(Keyring::KeyType::kEcryptfsKey, key_reference_);
}

base::FilePath EcryptfsContainer::GetBackingLocation() const {
  return backing_dir_;
}

}  // namespace cryptohome
