// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/ecryptfs_container.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

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

bool EcryptfsContainer::Reset() {
  // Reset should never be called for eCryptFs containers.
  LOG(ERROR) << "Reset not supported on eCryptFs containers";
  return false;
}

bool EcryptfsContainer::Teardown() {
  return keyring_->RemoveKey(Keyring::KeyType::kEcryptfsKey, key_reference_);
}

base::FilePath EcryptfsContainer::GetBackingLocation() const {
  return backing_dir_;
}

}  // namespace libstorage
