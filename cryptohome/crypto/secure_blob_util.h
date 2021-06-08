// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_SECURE_BLOB_UTIL_H_
#define CRYPTOHOME_CRYPTO_SECURE_BLOB_UTIL_H_

#include <string>

#include <brillo/secure_blob.h>

namespace cryptohome {

void GetSecureRandom(unsigned char* bytes, size_t len);
brillo::SecureBlob CreateSecureRandomBlob(size_t length);

// Encodes a binary blob to hex-ascii. Similar to base::HexEncode but
// produces lowercase letters for hex digits.
//
// Parameters
//   blob - The binary blob to convert
std::string BlobToHex(const brillo::Blob& blob);
std::string SecureBlobToHex(const brillo::SecureBlob& blob);

// Parameters
//   blob - The binary blob to convert
//   buffer (IN/OUT) - Where to store the converted blob
//   buffer_length - The size of the buffer
void BlobToHexToBuffer(const brillo::Blob& blob,
                       void* buffer,
                       size_t buffer_length);
void SecureBlobToHexToBuffer(const brillo::SecureBlob& blob,
                             void* buffer,
                             size_t buffer_length);

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_SECURE_BLOB_UTIL_H_
