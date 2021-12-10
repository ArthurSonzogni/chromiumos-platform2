// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash_storage.h"

#include <sys/stat.h>

#include <string>

#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"

namespace cryptohome {

// Use rw------- for the USS files.
constexpr mode_t kUserSecretStashFilePermissions = 0600;

UserSecretStashStorage::UserSecretStashStorage(Platform* platform)
    : platform_(platform) {}

UserSecretStashStorage::~UserSecretStashStorage() = default;

bool UserSecretStashStorage::Persist(
    const brillo::SecureBlob& uss_container_flatbuffer,
    const std::string& obfuscated_username) {
  if (!platform_->WriteSecureBlobToFileAtomicDurable(
          UserSecretStashPath(obfuscated_username), uss_container_flatbuffer,
          kUserSecretStashFilePermissions)) {
    LOG(ERROR) << "Failed to store the UserSecretStash file for "
               << obfuscated_username;
    return false;
  }
  return true;
}

base::Optional<brillo::SecureBlob> UserSecretStashStorage::LoadPersisted(
    const std::string& obfuscated_username) {
  brillo::SecureBlob uss_container_flatbuffer;
  if (!platform_->ReadFileToSecureBlob(UserSecretStashPath(obfuscated_username),
                                       &uss_container_flatbuffer)) {
    LOG(ERROR) << "Failed to load the UserSecretStash file for "
               << obfuscated_username;
    return base::nullopt;
  }
  return uss_container_flatbuffer;
}

}  // namespace cryptohome
