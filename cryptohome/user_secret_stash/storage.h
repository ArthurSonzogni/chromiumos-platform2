// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_STORAGE_H_
#define CRYPTOHOME_USER_SECRET_STASH_STORAGE_H_

#include <utility>

#include <brillo/secure_blob.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/username.h"

namespace cryptohome {

class UssStorage final {
 public:
  explicit UssStorage(libstorage::Platform* platform);

  UssStorage(const UssStorage&) = delete;
  UssStorage& operator=(const UssStorage&) = delete;

  ~UssStorage();

  // Persists the serialized USS container, as created by
  // `UserSecretStash::GetEncryptedContainer()`, in the given user's
  // directory in the shadow root. Returns a status on failure.
  CryptohomeStatus Persist(const brillo::Blob& uss_container_flatbuffer,
                           const ObfuscatedUsername& obfuscated_username);
  // Loads the serialized USS container flatbuffer (to be used with
  // `UserSecretStash::FromEncryptedContainer()`) from the given user's
  // directory in the shadow root. Returns nullopt on failure.
  CryptohomeStatusOr<brillo::Blob> LoadPersisted(
      const ObfuscatedUsername& obfuscated_username) const;

 private:
  libstorage::Platform* const platform_;
};

// Wrapper around UssStorage that binds it to a specific user. Individual
// instances of USS are generally tied to a user and so it's useful to have a
// single object to pass around.
class UserUssStorage final {
 public:
  UserUssStorage(UssStorage& storage, ObfuscatedUsername username)
      : storage_(&storage), username_(std::move(username)) {}

  UserUssStorage(const UserUssStorage&) = default;
  UserUssStorage& operator=(const UserUssStorage&) = default;

  // These functions are the same was as the UssStorage versions minus the
  // username parameter.
  CryptohomeStatus Persist(const brillo::Blob& uss_container_flatbuffer) {
    return storage_->Persist(uss_container_flatbuffer, username_);
  }
  CryptohomeStatusOr<brillo::Blob> LoadPersisted() const {
    return storage_->LoadPersisted(username_);
  }

 private:
  UssStorage* storage_;
  ObfuscatedUsername username_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_STORAGE_H_
