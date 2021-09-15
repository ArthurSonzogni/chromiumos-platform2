// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_H_
#define CRYPTOHOME_USER_SECRET_STASH_H_

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <stdint.h>

#include <memory>

#include "cryptohome/flatbuffer_secure_allocator_bridge.h"

namespace cryptohome {

// This wraps the UserSecretStash flatbuffer message, and is the only way that
// the UserSecretStash is accessed. Don't pass the raw flatbuffer around.
class UserSecretStash {
 public:
  // Sets up a UserSecretStash with a random file system key, and a random reset
  // secret.
  static std::unique_ptr<UserSecretStash> CreateRandom();
  // This deserializes the |flatbuffer| into a UserSecretStashContainer table.
  // Besides unencrypted data, that table contains a ciphertext, which is
  // decrypted with the |main_key| using AES-GCM-256. It doesn't return the
  // plaintext, it populates the fields of the class with the encrypted message.
  static std::unique_ptr<UserSecretStash> FromEncryptedContainer(
      const brillo::SecureBlob& flatbuffer, const brillo::SecureBlob& main_key);

  virtual ~UserSecretStash() = default;

  // Because this class contains raw secrets, it should never be copy-able.
  UserSecretStash(const UserSecretStash&) = delete;
  UserSecretStash& operator=(const UserSecretStash&) = delete;

  const brillo::SecureBlob& GetFileSystemKey() const;
  void SetFileSystemKey(const brillo::SecureBlob& key);

  const brillo::SecureBlob& GetResetSecret() const;
  void SetResetSecret(const brillo::SecureBlob& secret);

  // This uses the |main_key|, which should be 256-bit as of right now, to
  // encrypted this UserSecretStash class. The object is converted to a
  // UserSecretStashProto, serialized, and then encrypted with AES-GCM-256.
  base::Optional<brillo::SecureBlob> GetEncryptedContainer(
      const brillo::SecureBlob& main_key);

 private:
  UserSecretStash(const brillo::SecureBlob& file_system_key,
                  const brillo::SecureBlob& reset_secret);

  // A key registered with the kernel to decrypt files.
  brillo::SecureBlob file_system_key_;
  // The reset secret used for any PinWeaver backed credentials.
  brillo::SecureBlob reset_secret_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_H_
