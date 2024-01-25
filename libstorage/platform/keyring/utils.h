// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_PLATFORM_KEYRING_UTILS_H_
#define LIBSTORAGE_PLATFORM_KEYRING_UTILS_H_

#include <string>

#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>

#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

namespace ecryptfs {

// Creates an ecryptfs auth token and installs it in the kernel keyring.
bool AddEcryptfsAuthToken(const brillo::SecureBlob& key,
                          const std::string& key_sig,
                          const brillo::SecureBlob& salt);

// Creates an ecryptfs auth token and installs it in the kernel keyring.
bool RemoveEcryptfsAuthToken(const std::string& key_sig);

}  // namespace ecryptfs

namespace dmcrypt {
// Generate the key reference to be used by keyring related functions.
BRILLO_EXPORT FileSystemKeyReference
GenerateKeyringDescription(const brillo::SecureBlob& key_reference);

// Generates the key descriptor to be used in the device mapper table if the
// kernel keyring is supported.
BRILLO_EXPORT brillo::SecureBlob GenerateDmcryptKeyDescriptor(
    const brillo::SecureBlob key_reference, uint64_t key_size);

// For dm-crypt, we use the process keyring to ensure that the key is unlinked
// if the process exits/crashes before it is cleared.
bool AddLogonKey(const brillo::SecureBlob& key,
                 const brillo::SecureBlob& key_reference);

// Removes the key from the keyring.
bool UnlinkLogonKey(const brillo::SecureBlob& key_reference);

}  // namespace dmcrypt

}  // namespace libstorage

#endif  // LIBSTORAGE_PLATFORM_KEYRING_UTILS_H_
