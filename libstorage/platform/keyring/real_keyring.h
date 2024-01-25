// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_PLATFORM_KEYRING_REAL_KEYRING_H_
#define LIBSTORAGE_PLATFORM_KEYRING_REAL_KEYRING_H_

#include "libstorage/platform/keyring/keyring.h"

#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

class BRILLO_EXPORT RealKeyring : public Keyring {
 public:
  RealKeyring() = default;
  RealKeyring(const RealKeyring&) = delete;
  RealKeyring& operator=(const RealKeyring&) = delete;

  ~RealKeyring() override = default;

  bool AddKey(Keyring::KeyType type,
              const FileSystemKey& key,
              FileSystemKeyReference* key_reference) override;
  bool RemoveKey(Keyring::KeyType type,
                 const FileSystemKeyReference& key_reference) override;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_PLATFORM_KEYRING_REAL_KEYRING_H_
