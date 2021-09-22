// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_STORAGE_H_
#define CRYPTOHOME_USER_SECRET_STASH_STORAGE_H_

#include <string>

#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/platform.h"

namespace cryptohome {

// Persists the serialized USS container, as created by
// `UserSecretStash::GetEncryptedContainer()`, in the given user's directory in
// the shadow root. Returns false on failure.
bool PersistUserSecretStash(const brillo::SecureBlob& uss_container_flatbuffer,
                            const std::string& obfuscated_username,
                            Platform* platform);
// Loads the serialized USS container flatbuffer (to be used with
// `UserSecretStash::FromEncryptedContainer()`) from the given user's directory
// in the shadow root. Returns nullopt on failure.
base::Optional<brillo::SecureBlob> LoadPersistedUserSecretStash(
    const std::string& obfuscated_username, Platform* platform);

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_STORAGE_H_
