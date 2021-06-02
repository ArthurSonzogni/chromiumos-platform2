// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_SHA_H_
#define CRYPTOHOME_CRYPTO_SHA_H_

#include <brillo/secure_blob.h>

namespace cryptohome {

// TODO(jorgelo,crbug.com/728047): Review current usage of these functions and
// consider making the functions that take a plain Blob also return a plain
// Blob.
brillo::Blob Sha1(const brillo::Blob& data);
brillo::SecureBlob Sha1ToSecureBlob(const brillo::Blob& data);
brillo::SecureBlob Sha1(const brillo::SecureBlob& data);

brillo::Blob Sha256(const brillo::Blob& data);
brillo::SecureBlob Sha256ToSecureBlob(const brillo::Blob& data);
brillo::SecureBlob Sha256(const brillo::SecureBlob& data);
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_SHA_H_
