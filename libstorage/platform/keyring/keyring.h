// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_PLATFORM_KEYRING_KEYRING_H_
#define LIBSTORAGE_PLATFORM_KEYRING_KEYRING_H_

#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

class Keyring {
 public:
  enum class KeyType {
    kEcryptfsKey,
    kFscryptV1Key,
    kFscryptV2Key,
    kDmcryptKey,
  };

  Keyring() = default;
  Keyring(const Keyring&) = delete;
  Keyring& operator=(const Keyring&) = delete;

  virtual ~Keyring() = default;

  // Adds a key to the appropriate kernel structure for the key type. Depending
  // on the key type, the key reference is either consumed as is or modified by
  // the kernel API. Clients should use the reference as-is after the call.
  virtual bool AddKey(KeyType type,
                      const FileSystemKey& key,
                      FileSystemKeyReference* key_reference) = 0;

  // Removes a key from the appropriate kernel structure.
  virtual bool RemoveKey(KeyType type,
                         const FileSystemKeyReference& key_reference) = 0;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_PLATFORM_KEYRING_KEYRING_H_
